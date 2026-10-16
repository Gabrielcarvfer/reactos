/*
 * PROJECT:     ReactOS Console Server DLL
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Virtual terminal sequences (ENABLE_VIRTUAL_TERMINAL_PROCESSING
 *              and ENABLE_VIRTUAL_TERMINAL_INPUT), as the Windows 10 console
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira
 */

/*
 * Output: the text written to a screen buffer in VT mode goes through a
 * parser (ECMA-48 / xterm subset, as documented for the Windows console in
 * "Console Virtual Terminal Sequences"). Plain text is written as usual by
 * ConioWriteConsole(); the sequences act on the screen buffer directly.
 * Coordinates of the sequences are relative to the console window (the view).
 *
 * Input: with ENABLE_VIRTUAL_TERMINAL_INPUT, keys without a character (arrows,
 * function keys...) become the sequences that terminals send.
 */

/* INCLUDES *******************************************************************/

#include <consrv.h>

#define NDEBUG
#include <debug.h>

/* From terminal.c */
NTSTATUS
ConioWriteConsole(PFRONTEND FrontEnd,
                  PTEXTMODE_SCREEN_BUFFER Buff,
                  PWCHAR Buffer,
                  DWORD Length,
                  BOOL Attrib);

/* GLOBALS ********************************************************************/

#define VT_MAX_PARAMS   16
#define VT_MAX_OSC      256
#define VT_DEFAULT      0xFFFF

typedef enum _VT_PARSE_STATE
{
    VtGround,
    VtEscape,           /* After ESC */
    VtEscapeCharset,    /* After ESC ( ) * + : the character set */
    VtEscapeOther,      /* After ESC and another intermediate byte */
    VtCsi,              /* After ESC [ */
    VtOsc,              /* After ESC ] */
    VtOscEscape,        /* ESC inside an OSC: maybe ST (ESC \) */
    VtString            /* DCS, SOS, PM, APC: ignored up to ST */
} VT_PARSE_STATE;

typedef struct _VT_STATE
{
    VT_PARSE_STATE State;
    WCHAR   Private;                /* ? > = < before the CSI parameters */
    WCHAR   Intermediate;           /* SP ! " $ ' before the CSI final byte */
    UCHAR   NumParams;
    USHORT  Params[VT_MAX_PARAMS];
    USHORT  OscLength;
    WCHAR   Osc[VT_MAX_OSC];
    WCHAR   CharsetSlot;            /* ( for G0, ) for G1 */

    BOOLEAN LineDrawingG0;          /* G0 is the DEC Special Graphics set */
    BOOLEAN LineDrawingG1;
    BOOLEAN ShiftOut;               /* SO: using G1 */
    BOOLEAN WrapPending;            /* Written in the last column; wrap on the next character */
    BOOLEAN OriginMode;             /* DECOM: rows relative to the scrolling region */

    /* SGR state; the attribute is put in ScreenDefaultAttrib */
    USHORT  BaseAttrib;             /* SGR 0: the attribute when VT mode started */
    UCHAR   Fore, Back;             /* Console colour indexes (0-15) */
    BOOLEAN Bold, Underline, Reverse;

    /* DECSC / DECRC */
    BOOLEAN HasSaved;
    COORD   SavedCursor;
    UCHAR   SavedFore, SavedBack;
    BOOLEAN SavedBold, SavedUnderline, SavedReverse, SavedLineDrawingG0, SavedOriginMode;

    /* DECSTBM: rows of the view, 0-based; Bottom < 0: the whole view */
    SHORT   ScrollTop, ScrollBottom;

    /* Alternate screen buffer (?1049): a copy of the main one */
    PCHAR_INFO AltSaved;
    COORD   AltSavedSize;               /* The buffer may be resized meanwhile */
    COORD   AltSavedCursor;

    BOOLEAN Dirty;                  /* Cells or cursor changed: redraw the view */
} VT_STATE, *PVT_STATE;

/* ANSI colour (red=1 green=2 blue=4) <-> console colour (blue=1 green=2 red=4) */
#define AnsiToConsoleColor(c)   ((((c) & 1) << 2) | ((c) & 2) | (((c) & 4) >> 2) | ((c) & 8))

/* PRIVATE FUNCTIONS **********************************************************/

static PVT_STATE
VtGetState(PTEXTMODE_SCREEN_BUFFER Buff)
{
    PVT_STATE Vt = Buff->Vt;

    if (Vt) return Vt;

    Vt = ConsoleAllocHeap(HEAP_ZERO_MEMORY, sizeof(VT_STATE));
    if (!Vt) return NULL;

    Vt->State = VtGround;
    Vt->BaseAttrib = Buff->ScreenDefaultAttrib & 0xFF;
    Vt->Fore = Vt->BaseAttrib & 0x0F;
    Vt->Back = (Vt->BaseAttrib >> 4) & 0x0F;
    Vt->ScrollTop = 0;
    Vt->ScrollBottom = -1;
    Buff->Vt = Vt;
    return Vt;
}

VOID
VtFreeState(PTEXTMODE_SCREEN_BUFFER Buff)
{
    PVT_STATE Vt = Buff->Vt;

    if (!Vt) return;
    if (Vt->AltSaved) ConsoleFreeHeap(Vt->AltSaved);
    ConsoleFreeHeap(Vt);
    Buff->Vt = NULL;
}

static VOID
VtUpdateAttrib(PTEXTMODE_SCREEN_BUFFER Buff, PVT_STATE Vt)
{
    USHORT Fore = Vt->Fore, Attrib;

    /* As the Windows console: bold makes the 8 dark colours bright */
    if (Vt->Bold && Fore < 8) Fore |= FOREGROUND_INTENSITY;

    Attrib = Fore | (Vt->Back << 4);
    if (Vt->Underline) Attrib |= COMMON_LVB_UNDERSCORE;
    if (Vt->Reverse) Attrib |= COMMON_LVB_REVERSE_VIDEO;
    Buff->ScreenDefaultAttrib = Attrib;
}

/* Nearest of the 16 console colours to an RGB colour */
static UCHAR
VtNearestColor(PCONSRV_CONSOLE Console, UCHAR r, UCHAR g, UCHAR b)
{
    ULONG Best = MAXULONG;
    UCHAR i, Index = 0;

    for (i = 0; i < 16; i++)
    {
        LONG dr = (LONG)GetRValue(Console->Colors[i]) - r;
        LONG dg = (LONG)GetGValue(Console->Colors[i]) - g;
        LONG db = (LONG)GetBValue(Console->Colors[i]) - b;
        ULONG Distance = dr * dr + dg * dg + db * db;
        if (Distance < Best)
        {
            Best = Distance;
            Index = i;
        }
    }
    return Index;
}

/* An xterm 256-colour index as a console colour */
static UCHAR
Vt256Color(PCONSRV_CONSOLE Console, USHORT Index)
{
    static const UCHAR Levels[6] = { 0, 95, 135, 175, 215, 255 };

    if (Index < 16)
        return AnsiToConsoleColor(Index);
    if (Index < 232)
    {
        Index -= 16;
        return VtNearestColor(Console, Levels[Index / 36], Levels[(Index / 6) % 6], Levels[Index % 6]);
    }
    if (Index < 256)
    {
        UCHAR Gray = (UCHAR)(8 + (Index - 232) * 10);
        return VtNearestColor(Console, Gray, Gray, Gray);
    }
    return 0;
}

static USHORT
VtParam(PVT_STATE Vt, UCHAR i, USHORT Default)
{
    if (i >= Vt->NumParams || Vt->Params[i] == VT_DEFAULT)
        return Default;
    return Vt->Params[i];
}

/* A count parameter: missing or 0 means 1 */
static SHORT
VtCount(PVT_STATE Vt, UCHAR i)
{
    USHORT Value = VtParam(Vt, i, 1);
    return (SHORT)(Value ? min(Value, 0x7FFF) : 1);
}

static SHORT
VtScrollTop(PTEXTMODE_SCREEN_BUFFER Buff, PVT_STATE Vt)
{
    return Buff->ViewOrigin.Y + min(Vt->ScrollTop, Buff->ViewSize.Y - 1);
}

static SHORT
VtScrollBottom(PTEXTMODE_SCREEN_BUFFER Buff, PVT_STATE Vt)
{
    SHORT Bottom = (Vt->ScrollBottom < 0) ? Buff->ViewSize.Y - 1 : min(Vt->ScrollBottom, Buff->ViewSize.Y - 1);
    return min(Buff->ViewOrigin.Y + Bottom, Buff->ScreenBufferSize.Y - 1);
}

static VOID
VtMoveCursor(PTEXTMODE_SCREEN_BUFFER Buff, PVT_STATE Vt, LONG X, LONG Y)
{
    X = max(0, min(X, Buff->ScreenBufferSize.X - 1));
    Y = max(0, min(Y, Buff->ScreenBufferSize.Y - 1));
    Buff->CursorPosition.X = (SHORT)X;
    Buff->CursorPosition.Y = (SHORT)Y;
    Vt->WrapPending = FALSE;
    Vt->Dirty = TRUE;
}

static VOID
VtBlankCells(PTEXTMODE_SCREEN_BUFFER Buff, PCHAR_INFO Ptr, LONG Count)
{
    /* Erasing uses the current colours, without underline or reverse */
    USHORT Attrib = Buff->ScreenDefaultAttrib & ~(COMMON_LVB_UNDERSCORE | COMMON_LVB_REVERSE_VIDEO | COMMON_LVB_SBCSDBCS);

    if (Buff->ScreenDefaultAttrib & COMMON_LVB_REVERSE_VIDEO)
        Attrib = ((Attrib & 0x0F) << 4) | ((Attrib >> 4) & 0x0F);
    while (Count-- > 0)
    {
        Ptr->Char.UnicodeChar = L' ';
        Ptr->Attributes = Attrib;
        Ptr++;
    }
}

/* Erases the cells [X1, X2] of row Y */
static VOID
VtEraseCells(PTEXTMODE_SCREEN_BUFFER Buff, PVT_STATE Vt, SHORT Y, SHORT X1, SHORT X2)
{
    X1 = max(X1, 0);
    X2 = min(X2, Buff->ScreenBufferSize.X - 1);
    if (Y < 0 || Y >= Buff->ScreenBufferSize.Y || X1 > X2) return;
    VtBlankCells(Buff, ConioCoordToPointer(Buff, X1, Y), X2 - X1 + 1);
    Vt->Dirty = TRUE;
}

/* Scrolls the rows [Top, Bottom] up (Count > 0) or down (Count < 0) */
static VOID
VtScrollRows(PTEXTMODE_SCREEN_BUFFER Buff, PVT_STATE Vt, SHORT Top, SHORT Bottom, LONG Count)
{
    LONG Rows = Bottom - Top + 1, y;
    SIZE_T RowSize = Buff->ScreenBufferSize.X * sizeof(CHAR_INFO);

    if (Rows <= 0 || Count == 0) return;
    if (Count >= Rows || -Count >= Rows)
    {
        for (y = Top; y <= Bottom; y++)
            VtEraseCells(Buff, Vt, (SHORT)y, 0, Buff->ScreenBufferSize.X - 1);
        return;
    }

    /* Row by row: the buffer is circular, its rows are not all contiguous */
    if (Count > 0)
    {
        for (y = Top; y <= Bottom - Count; y++)
            RtlMoveMemory(ConioCoordToPointer(Buff, 0, y), ConioCoordToPointer(Buff, 0, y + Count), RowSize);
        for (; y <= Bottom; y++)
            VtEraseCells(Buff, Vt, (SHORT)y, 0, Buff->ScreenBufferSize.X - 1);
    }
    else
    {
        Count = -Count;
        for (y = Bottom; y >= Top + Count; y--)
            RtlMoveMemory(ConioCoordToPointer(Buff, 0, y), ConioCoordToPointer(Buff, 0, y - Count), RowSize);
        for (; y >= Top; y--)
            VtEraseCells(Buff, Vt, (SHORT)y, 0, Buff->ScreenBufferSize.X - 1);
    }
    Vt->Dirty = TRUE;
}

/* LF / IND: one row down, scrolling the region at its bottom */
static VOID
VtIndex(PFRONTEND FrontEnd, PTEXTMODE_SCREEN_BUFFER Buff, PVT_STATE Vt, BOOLEAN CarriageReturn)
{
    SHORT Bottom = VtScrollBottom(Buff, Vt);
    SHORT X = CarriageReturn ? 0 : Buff->CursorPosition.X;

    Vt->WrapPending = FALSE;
    if (Buff->CursorPosition.Y == Bottom &&
        (Vt->ScrollTop > 0 || Vt->ScrollBottom >= 0))
    {
        /* With a scrolling region: only the region scrolls */
        VtScrollRows(Buff, Vt, VtScrollTop(Buff, Vt), Bottom, 1);
        Buff->CursorPosition.X = X;
        return;
    }

    /* Else as a plain LF (the window follows and may scroll the buffer) */
    ConioWriteConsole(FrontEnd, Buff, L"\n", 1, TRUE);
    Buff->CursorPosition.X = X;
    Vt->Dirty = TRUE;
}

/* RI: one row up, scrolling the region down at its top */
static VOID
VtReverseIndex(PTEXTMODE_SCREEN_BUFFER Buff, PVT_STATE Vt)
{
    SHORT Top = VtScrollTop(Buff, Vt);

    Vt->WrapPending = FALSE;
    if (Buff->CursorPosition.Y == Top)
        VtScrollRows(Buff, Vt, Top, VtScrollBottom(Buff, Vt), -1);
    else if (Buff->CursorPosition.Y > 0)
        Buff->CursorPosition.Y--;
    Vt->Dirty = TRUE;
}

static VOID
VtSaveCursor(PTEXTMODE_SCREEN_BUFFER Buff, PVT_STATE Vt)
{
    Vt->HasSaved = TRUE;
    Vt->SavedCursor = Buff->CursorPosition;
    Vt->SavedFore = Vt->Fore;
    Vt->SavedBack = Vt->Back;
    Vt->SavedBold = Vt->Bold;
    Vt->SavedUnderline = Vt->Underline;
    Vt->SavedReverse = Vt->Reverse;
    Vt->SavedLineDrawingG0 = Vt->LineDrawingG0;
    Vt->SavedOriginMode = Vt->OriginMode;
}

static VOID
VtRestoreCursor(PTEXTMODE_SCREEN_BUFFER Buff, PVT_STATE Vt)
{
    if (!Vt->HasSaved)
    {
        VtMoveCursor(Buff, Vt, 0, Buff->ViewOrigin.Y);
        return;
    }
    VtMoveCursor(Buff, Vt, Vt->SavedCursor.X, Vt->SavedCursor.Y);
    Vt->Fore = Vt->SavedFore;
    Vt->Back = Vt->SavedBack;
    Vt->Bold = Vt->SavedBold;
    Vt->Underline = Vt->SavedUnderline;
    Vt->Reverse = Vt->SavedReverse;
    Vt->LineDrawingG0 = Vt->SavedLineDrawingG0;
    Vt->OriginMode = Vt->SavedOriginMode;
    VtUpdateAttrib(Buff, Vt);
}

static VOID
VtEraseDisplay(PTEXTMODE_SCREEN_BUFFER Buff, PVT_STATE Vt, USHORT Mode)
{
    SHORT Top = Buff->ViewOrigin.Y;
    SHORT Bottom = min(Buff->ViewOrigin.Y + Buff->ViewSize.Y - 1, Buff->ScreenBufferSize.Y - 1);
    SHORT y, CursorY = Buff->CursorPosition.Y;

    switch (Mode)
    {
    case 0: /* From the cursor to the end */
        VtEraseCells(Buff, Vt, CursorY, Buff->CursorPosition.X, Buff->ScreenBufferSize.X - 1);
        for (y = CursorY + 1; y <= Bottom; y++)
            VtEraseCells(Buff, Vt, y, 0, Buff->ScreenBufferSize.X - 1);
        break;
    case 1: /* From the beginning to the cursor */
        for (y = Top; y < CursorY; y++)
            VtEraseCells(Buff, Vt, y, 0, Buff->ScreenBufferSize.X - 1);
        VtEraseCells(Buff, Vt, CursorY, 0, Buff->CursorPosition.X);
        break;
    case 2: /* The whole window */
        for (y = Top; y <= Bottom; y++)
            VtEraseCells(Buff, Vt, y, 0, Buff->ScreenBufferSize.X - 1);
        break;
    case 3: /* The whole buffer, including what scrolled off */
        for (y = 0; y < Buff->ScreenBufferSize.Y; y++)
            VtEraseCells(Buff, Vt, y, 0, Buff->ScreenBufferSize.X - 1);
        break;
    }
}

static VOID
VtEraseLine(PTEXTMODE_SCREEN_BUFFER Buff, PVT_STATE Vt, USHORT Mode)
{
    SHORT Y = Buff->CursorPosition.Y, X = Buff->CursorPosition.X;

    if (Mode == 0)
        VtEraseCells(Buff, Vt, Y, X, Buff->ScreenBufferSize.X - 1);
    else if (Mode == 1)
        VtEraseCells(Buff, Vt, Y, 0, X);
    else if (Mode == 2)
        VtEraseCells(Buff, Vt, Y, 0, Buff->ScreenBufferSize.X - 1);
}

/* ICH (Count > 0) / DCH (Count < 0) on the cursor row */
static VOID
VtShiftCells(PTEXTMODE_SCREEN_BUFFER Buff, PVT_STATE Vt, LONG Count)
{
    SHORT X = Buff->CursorPosition.X, Width = Buff->ScreenBufferSize.X;
    PCHAR_INFO Row = ConioCoordToPointer(Buff, 0, Buff->CursorPosition.Y);
    LONG Moved;

    if (Count > 0)
    {
        Count = min(Count, Width - X);
        Moved = Width - X - Count;
        if (Moved > 0) RtlMoveMemory(&Row[X + Count], &Row[X], Moved * sizeof(CHAR_INFO));
        VtBlankCells(Buff, &Row[X], Count);
    }
    else
    {
        Count = min(-Count, Width - X);
        Moved = Width - X - Count;
        if (Moved > 0) RtlMoveMemory(&Row[X], &Row[X + Count], Moved * sizeof(CHAR_INFO));
        VtBlankCells(Buff, &Row[Width - Count], Count);
    }
    Vt->WrapPending = FALSE;
    Vt->Dirty = TRUE;
}

/* Puts a reply (DSR, DA) in the input buffer, as if it was typed */
static VOID
VtReply(PCONSRV_CONSOLE Console, PCWSTR Reply)
{
    INPUT_RECORD Records[32];
    ULONG Count = 0, Written;

    while (*Reply && Count < ARRAYSIZE(Records))
    {
        RtlZeroMemory(&Records[Count], sizeof(INPUT_RECORD));
        Records[Count].EventType = KEY_EVENT;
        Records[Count].Event.KeyEvent.bKeyDown = TRUE;
        Records[Count].Event.KeyEvent.wRepeatCount = 1;
        Records[Count].Event.KeyEvent.uChar.UnicodeChar = *Reply++;
        Count++;
    }
    ConioAddInputEvents(Console, Records, Count, &Written, TRUE);
}

static VOID
VtSetGraphicsRendition(PCONSRV_CONSOLE Console, PTEXTMODE_SCREEN_BUFFER Buff, PVT_STATE Vt)
{
    UCHAR i;

    if (Vt->NumParams == 0)
    {
        Vt->NumParams = 1;
        Vt->Params[0] = 0;
    }

    for (i = 0; i < Vt->NumParams; i++)
    {
        USHORT p = VtParam(Vt, i, 0);

        if (p == 0)
        {
            Vt->Fore = Vt->BaseAttrib & 0x0F;
            Vt->Back = (Vt->BaseAttrib >> 4) & 0x0F;
            Vt->Bold = Vt->Underline = Vt->Reverse = FALSE;
        }
        else if (p == 1) Vt->Bold = TRUE;
        else if (p == 22) Vt->Bold = FALSE;
        else if (p == 4) Vt->Underline = TRUE;
        else if (p == 24) Vt->Underline = FALSE;
        else if (p == 7) Vt->Reverse = TRUE;
        else if (p == 27) Vt->Reverse = FALSE;
        else if (p >= 30 && p <= 37) Vt->Fore = AnsiToConsoleColor(p - 30);
        else if (p == 39) Vt->Fore = Vt->BaseAttrib & 0x0F;
        else if (p >= 40 && p <= 47) Vt->Back = AnsiToConsoleColor(p - 40);
        else if (p == 49) Vt->Back = (Vt->BaseAttrib >> 4) & 0x0F;
        else if (p >= 90 && p <= 97) Vt->Fore = AnsiToConsoleColor(p - 90 + 8);
        else if (p >= 100 && p <= 107) Vt->Back = AnsiToConsoleColor(p - 100 + 8);
        else if (p == 38 || p == 48)
        {
            /* 38;5;n (256 colours) or 38;2;r;g;b (RGB): the nearest console colour */
            UCHAR Color;
            USHORT Kind = VtParam(Vt, i + 1, 0);

            if (Kind == 5)
            {
                Color = Vt256Color(Console, VtParam(Vt, i + 2, 0));
                i += 2;
            }
            else if (Kind == 2)
            {
                Color = VtNearestColor(Console, (UCHAR)VtParam(Vt, i + 2, 0),
                                       (UCHAR)VtParam(Vt, i + 3, 0), (UCHAR)VtParam(Vt, i + 4, 0));
                i += 4;
            }
            else
            {
                break;
            }
            if (p == 38) Vt->Fore = Color;
            else Vt->Back = Color;
        }
        /* Italic, blink, faint, conceal, crossed-out...: not shown */
    }
    VtUpdateAttrib(Buff, Vt);
}

static VOID
VtSwitchScreen(PFRONTEND FrontEnd, PTEXTMODE_SCREEN_BUFFER Buff, PVT_STATE Vt, BOOLEAN Alternate)
{
    SIZE_T Size = Buff->ScreenBufferSize.X * Buff->ScreenBufferSize.Y * sizeof(CHAR_INFO);

    if (Alternate && !Vt->AltSaved)
    {
        /* Keep the main screen and start from a blank one */
        Vt->AltSaved = ConsoleAllocHeap(0, Size);
        if (!Vt->AltSaved) return;
        RtlCopyMemory(Vt->AltSaved, Buff->Buffer, Size);
        Vt->AltSavedSize = Buff->ScreenBufferSize;
        Vt->AltSavedCursor = Buff->CursorPosition;
        VtEraseDisplay(Buff, Vt, 2);
    }
    else if (!Alternate && Vt->AltSaved)
    {
        if (Vt->AltSavedSize.X == Buff->ScreenBufferSize.X &&
            Vt->AltSavedSize.Y == Buff->ScreenBufferSize.Y)
        {
            RtlCopyMemory(Buff->Buffer, Vt->AltSaved, Size);
        }
        ConsoleFreeHeap(Vt->AltSaved);
        Vt->AltSaved = NULL;
        VtMoveCursor(Buff, Vt, Vt->AltSavedCursor.X, Vt->AltSavedCursor.Y);
    }
    Vt->Dirty = TRUE;
}

static VOID
VtSetMode(PFRONTEND FrontEnd, PTEXTMODE_SCREEN_BUFFER Buff, PVT_STATE Vt, BOOLEAN Set)
{
    PCONSRV_CONSOLE Console = FrontEnd->Console;
    UCHAR i;

    if (Vt->Private != L'?') return;    /* ANSI modes (IRM...) are not supported */

    for (i = 0; i < Vt->NumParams; i++)
    {
        switch (VtParam(Vt, i, 0))
        {
        case 1:     /* DECCKM: application cursor keys */
            Console->VtApplicationCursorKeys = Set;
            break;
        case 6:     /* DECOM: origin mode */
            Vt->OriginMode = Set;
            VtMoveCursor(Buff, Vt, 0, Set ? VtScrollTop(Buff, Vt) : Buff->ViewOrigin.Y);
            break;
        case 7:     /* DECAWM: wrap at the end of lines */
            if (Set) Buff->Mode |= ENABLE_WRAP_AT_EOL_OUTPUT;
            else Buff->Mode &= ~ENABLE_WRAP_AT_EOL_OUTPUT;
            break;
        case 25:    /* DECTCEM: show the cursor */
            Buff->CursorInfo.bVisible = Set;
            Vt->Dirty = TRUE;
            break;
        case 47:
        case 1047:
        case 1049:  /* The alternate screen buffer */
            if (Set) VtSaveCursor(Buff, Vt);
            VtSwitchScreen(FrontEnd, Buff, Vt, Set);
            if (!Set) VtRestoreCursor(Buff, Vt);
            break;
        /* Cursor blinking (12), mouse reporting (1000...), bracketed paste (2004),
           win32-input-mode (9001): not supported, ignored */
        }
    }
}

static VOID
VtExecuteCsi(PFRONTEND FrontEnd, PTEXTMODE_SCREEN_BUFFER Buff, PVT_STATE Vt, WCHAR Final)
{
    PCONSRV_CONSOLE Console = FrontEnd->Console;
    SHORT ViewTop = Buff->ViewOrigin.Y;
    SHORT X = Buff->CursorPosition.X, Y = Buff->CursorPosition.Y;
    WCHAR Reply[64];

    if (Vt->Intermediate == L' ' && Final == L'q')
    {
        /* DECSCUSR: cursor shape */
        USHORT Shape = VtParam(Vt, 0, 0);
        Buff->CursorInfo.dwSize = (Shape <= 2) ? 100 : (Shape <= 4) ? 25 : 50;
        Vt->Dirty = TRUE;
        return;
    }
    if (Vt->Intermediate == L'!' && Final == L'p')
    {
        /* DECSTR: soft reset */
        Buff->CursorInfo.bVisible = TRUE;
        Buff->Mode |= ENABLE_WRAP_AT_EOL_OUTPUT;
        Vt->OriginMode = FALSE;
        Vt->ScrollTop = 0;
        Vt->ScrollBottom = -1;
        Vt->LineDrawingG0 = Vt->LineDrawingG1 = Vt->ShiftOut = FALSE;
        Vt->NumParams = 0;
        VtSetGraphicsRendition(Console, Buff, Vt);
        Console->VtApplicationCursorKeys = FALSE;
        Vt->HasSaved = FALSE;
        return;
    }
    if (Vt->Intermediate) return;

    switch (Final)
    {
    case L'A': VtMoveCursor(Buff, Vt, X, max(Y - VtCount(Vt, 0), min(Y, VtScrollTop(Buff, Vt)))); break;
    case L'B': VtMoveCursor(Buff, Vt, X, min(Y + VtCount(Vt, 0), max(Y, VtScrollBottom(Buff, Vt)))); break;
    case L'C': VtMoveCursor(Buff, Vt, X + VtCount(Vt, 0), Y); break;
    case L'D': VtMoveCursor(Buff, Vt, X - VtCount(Vt, 0), Y); break;
    case L'E': VtMoveCursor(Buff, Vt, 0, min(Y + VtCount(Vt, 0), max(Y, VtScrollBottom(Buff, Vt)))); break;
    case L'F': VtMoveCursor(Buff, Vt, 0, max(Y - VtCount(Vt, 0), min(Y, VtScrollTop(Buff, Vt)))); break;
    case L'G':
    case L'`': VtMoveCursor(Buff, Vt, VtCount(Vt, 0) - 1, Y); break;
    case L'd':
        VtMoveCursor(Buff, Vt, X, (Vt->OriginMode ? VtScrollTop(Buff, Vt) : ViewTop) + VtCount(Vt, 0) - 1);
        break;
    case L'H':
    case L'f':
        VtMoveCursor(Buff, Vt, VtCount(Vt, 1) - 1,
                     (Vt->OriginMode ? VtScrollTop(Buff, Vt) : ViewTop) + VtCount(Vt, 0) - 1);
        break;
    case L'J': if (!Vt->Private) VtEraseDisplay(Buff, Vt, VtParam(Vt, 0, 0)); break;
    case L'K': if (!Vt->Private) VtEraseLine(Buff, Vt, VtParam(Vt, 0, 0)); break;
    case L'X': VtEraseCells(Buff, Vt, Y, X, X + VtCount(Vt, 0) - 1); break;
    case L'@': VtShiftCells(Buff, Vt, VtCount(Vt, 0)); break;
    case L'P': VtShiftCells(Buff, Vt, -VtCount(Vt, 0)); break;
    case L'L':
    case L'M':
        /* IL / DL: inside the scrolling region only */
        if (Y >= VtScrollTop(Buff, Vt) && Y <= VtScrollBottom(Buff, Vt))
        {
            VtScrollRows(Buff, Vt, Y, VtScrollBottom(Buff, Vt),
                         (Final == L'L') ? -VtCount(Vt, 0) : VtCount(Vt, 0));
            VtMoveCursor(Buff, Vt, 0, Y);
        }
        break;
    case L'S': VtScrollRows(Buff, Vt, VtScrollTop(Buff, Vt), VtScrollBottom(Buff, Vt), VtCount(Vt, 0)); break;
    case L'T': VtScrollRows(Buff, Vt, VtScrollTop(Buff, Vt), VtScrollBottom(Buff, Vt), -VtCount(Vt, 0)); break;
    case L'm': if (!Vt->Private) VtSetGraphicsRendition(Console, Buff, Vt); break;
    case L'h': VtSetMode(FrontEnd, Buff, Vt, TRUE); break;
    case L'l': VtSetMode(FrontEnd, Buff, Vt, FALSE); break;
    case L'r':
    {
        /* DECSTBM: the scrolling region */
        SHORT Top = VtCount(Vt, 0) - 1;
        SHORT Bottom = (SHORT)VtParam(Vt, 1, 0);
        Bottom = (Bottom == 0) ? Buff->ViewSize.Y - 1 : min(Bottom, Buff->ViewSize.Y) - 1;
        if (Top < Bottom)
        {
            Vt->ScrollTop = Top;
            Vt->ScrollBottom = (Top == 0 && Bottom == Buff->ViewSize.Y - 1) ? -1 : Bottom;
            VtMoveCursor(Buff, Vt, 0, Vt->OriginMode ? VtScrollTop(Buff, Vt) : ViewTop);
        }
        break;
    }
    case L's': if (!Vt->Private) VtSaveCursor(Buff, Vt); break;
    case L'u': if (!Vt->Private) VtRestoreCursor(Buff, Vt); break;
    case L'n':
        /* DSR: status / cursor position */
        if (VtParam(Vt, 0, 0) == 5)
        {
            VtReply(Console, L"\x1b[0n");
        }
        else if (VtParam(Vt, 0, 0) == 6)
        {
            _snwprintf(Reply, ARRAYSIZE(Reply), L"\x1b[%d;%dR", Y - ViewTop + 1, X + 1);
            VtReply(Console, Reply);
        }
        break;
    case L'c':
        /* DA: a VT100 with advanced video, as the Windows console */
        if (VtParam(Vt, 0, 0) == 0)
            VtReply(Console, (Vt->Private == L'>') ? L"\x1b[>0;10;1c" : L"\x1b[?1;0c");
        break;
    case L't':
        /* Window operations: only the size report */
        if (VtParam(Vt, 0, 0) == 18)
        {
            _snwprintf(Reply, ARRAYSIZE(Reply), L"\x1b[8;%d;%dt", Buff->ViewSize.Y, Buff->ViewSize.X);
            VtReply(Console, Reply);
        }
        break;
    }
}

static VOID
VtExecuteOsc(PFRONTEND FrontEnd, PVT_STATE Vt)
{
    PCONSRV_CONSOLE Console = FrontEnd->Console;
    PWCHAR Title, Buffer;
    SIZE_T Length;

    /* OSC 0 / 2 ; title: the window title */
    Vt->Osc[min(Vt->OscLength, VT_MAX_OSC - 1)] = UNICODE_NULL;
    if (!((Vt->Osc[0] == L'0' || Vt->Osc[0] == L'2') && Vt->Osc[1] == L';'))
        return;

    Title = &Vt->Osc[2];
    Length = wcslen(Title) * sizeof(WCHAR);
    Buffer = ConsoleAllocHeap(HEAP_ZERO_MEMORY, Length + sizeof(WCHAR));
    if (!Buffer) return;
    RtlCopyMemory(Buffer, Title, Length);

    if (Console->Title.Buffer) ConsoleFreeHeap(Console->Title.Buffer);
    Console->Title.Buffer = Buffer;
    Console->Title.Length = (USHORT)Length;
    Console->Title.MaximumLength = (USHORT)(Length + sizeof(WCHAR));
    TermChangeTitle(Console);
}

static WCHAR
VtLineDrawingChar(WCHAR Char)
{
    /* DEC Special Graphics, 0x5F-0x7E */
    static const WCHAR Map[] =
    {
        0x00A0, 0x25C6, 0x2592, 0x2409, 0x240C, 0x240D, 0x240A, 0x00B0, /* _ ` a b c d e f */
        0x00B1, 0x2424, 0x240B, 0x2518, 0x2510, 0x250C, 0x2514, 0x253C, /* g h i j k l m n */
        0x23BA, 0x23BB, 0x2500, 0x23BC, 0x23BD, 0x251C, 0x2524, 0x2534, /* o p q r s t u v */
        0x252C, 0x2502, 0x2264, 0x2265, 0x03C0, 0x2260, 0x00A3, 0x00B7  /* w x y z { | } ~ */
    };

    if (Char >= 0x5F && Char <= 0x7E)
        return Map[Char - 0x5F];
    return Char;
}

/* Writes printable characters, with the xterm "delayed wrap" at the last column */
static VOID
VtWriteText(PFRONTEND FrontEnd, PTEXTMODE_SCREEN_BUFFER Buff, PVT_STATE Vt, PWCHAR Text, ULONG Length)
{
    PCONSRV_CONSOLE Console = FrontEnd->Console;
    BOOLEAN LineDrawing = Vt->ShiftOut ? Vt->LineDrawingG1 : Vt->LineDrawingG0;
    WCHAR Converted[128];
    ULONG i;

    while (Length > 0)
    {
        ULONG Chunk = min(Length, ARRAYSIZE(Converted)), Room;
        PWCHAR Chars = Text;

        if (LineDrawing)
        {
            for (i = 0; i < Chunk; i++) Converted[i] = VtLineDrawingChar(Text[i]);
            Chars = Converted;
        }
        Text += Chunk;
        Length -= Chunk;

        /* CJK full-width characters: no delayed wrap */
        if (Console->IsCJK)
        {
            ConioWriteConsole(FrontEnd, Buff, Chars, Chunk, TRUE);
            continue;
        }

        while (Chunk > 0)
        {
            if (Vt->WrapPending)
            {
                Vt->WrapPending = FALSE;
                if (Buff->Mode & ENABLE_WRAP_AT_EOL_OUTPUT)
                    VtIndex(FrontEnd, Buff, Vt, TRUE);
            }

            Room = Buff->ScreenBufferSize.X - Buff->CursorPosition.X;
            if (Chunk < Room)
            {
                ConioWriteConsole(FrontEnd, Buff, Chars, Chunk, TRUE);
                break;
            }

            /* Up to the last column; the cursor stays on it */
            if (Room > 1)
                ConioWriteConsole(FrontEnd, Buff, Chars, Room - 1, TRUE);
            {
                PCHAR_INFO Ptr = ConioCoordToPointer(Buff, Buff->CursorPosition.X, Buff->CursorPosition.Y);
                Ptr->Char.UnicodeChar = Chars[Room - 1];
                Ptr->Attributes = Buff->ScreenDefaultAttrib;
            }
            Vt->WrapPending = TRUE;
            Vt->Dirty = TRUE;
            Chars += Room;
            Chunk -= Room;
        }
    }
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
VtWriteConsole(PFRONTEND FrontEnd,
               PTEXTMODE_SCREEN_BUFFER Buff,
               PWCHAR Buffer,
               DWORD Length)
{
    PCONSRV_CONSOLE Console = FrontEnd->Console;
    PVT_STATE Vt = VtGetState(Buff);
    DWORD i, Start;

    if (!Vt)
        return ConioWriteConsole(FrontEnd, Buff, Buffer, Length, TRUE);

    Vt->Dirty = FALSE;
    for (i = 0; i < Length; )
    {
        WCHAR c = Buffer[i];

        switch (Vt->State)
        {
        case VtGround:
            if (c >= L' ' && c != 0x7F)
            {
                /* A run of printable characters */
                for (Start = i; i < Length && Buffer[i] >= L' ' && Buffer[i] != 0x7F; i++);
                VtWriteText(FrontEnd, Buff, Vt, &Buffer[Start], i - Start);
                continue;
            }
            switch (c)
            {
            case 0x1B: Vt->State = VtEscape; break;
            case L'\n':
            case L'\v':
            case L'\f':
                VtIndex(FrontEnd, Buff, Vt, !(Buff->Mode & DISABLE_NEWLINE_AUTO_RETURN));
                break;
            case L'\r':
            case L'\b':
            case L'\t':
                Vt->WrapPending = FALSE;
                /* Fall through */
            case L'\a':
                ConioWriteConsole(FrontEnd, Buff, &Buffer[i], 1, TRUE);
                break;
            case 0x0E: Vt->ShiftOut = TRUE; break;     /* SO: G1 */
            case 0x0F: Vt->ShiftOut = FALSE; break;    /* SI: G0 */
            /* Other C0 controls are ignored, as in the Windows console */
            }
            i++;
            break;

        case VtEscape:
            i++;
            Vt->State = VtGround;
            switch (c)
            {
            case L'[':
                Vt->State = VtCsi;
                Vt->Private = Vt->Intermediate = 0;
                Vt->NumParams = 0;
                break;
            case L']':
                Vt->State = VtOsc;
                Vt->OscLength = 0;
                break;
            case L'P': case L'X': case L'^': case L'_':
                Vt->State = VtString;
                break;
            case L'(': case L')': case L'*': case L'+':
                Vt->State = VtEscapeCharset;
                Vt->CharsetSlot = c;
                break;
            case L'7': VtSaveCursor(Buff, Vt); break;
            case L'8': VtRestoreCursor(Buff, Vt); break;
            case L'D': VtIndex(FrontEnd, Buff, Vt, FALSE); break;
            case L'E': VtIndex(FrontEnd, Buff, Vt, TRUE); break;
            case L'M': VtReverseIndex(Buff, Vt); break;
            case L'c':
                /* RIS: full reset */
                Vt->NumParams = 0;
                VtSetGraphicsRendition(Console, Buff, Vt);
                Vt->ScrollTop = 0;
                Vt->ScrollBottom = -1;
                Vt->OriginMode = Vt->LineDrawingG0 = Vt->LineDrawingG1 = Vt->ShiftOut = FALSE;
                Buff->CursorInfo.bVisible = TRUE;
                Buff->Mode |= ENABLE_WRAP_AT_EOL_OUTPUT;
                Console->VtApplicationCursorKeys = FALSE;
                VtEraseDisplay(Buff, Vt, 2);
                VtMoveCursor(Buff, Vt, 0, Buff->ViewOrigin.Y);
                break;
            case L' ': case L'!': case L'"': case L'#': case L'$': case L'%': case L'&': case L'\'':
                Vt->State = VtEscapeOther;
                break;
            /* ESC = and ESC > (keypad modes) and others: ignored */
            }
            break;

        case VtEscapeCharset:
            i++;
            Vt->State = VtGround;
            if (Vt->CharsetSlot == L'(') Vt->LineDrawingG0 = (c == L'0');
            else if (Vt->CharsetSlot == L')') Vt->LineDrawingG1 = (c == L'0');
            break;

        case VtEscapeOther:
            /* ESC # 8 (DECALN) and the like: their final byte ends them */
            i++;
            if (c >= 0x30) Vt->State = VtGround;
            break;

        case VtCsi:
            i++;
            if (c >= L'0' && c <= L'9')
            {
                if (Vt->NumParams == 0) Vt->Params[Vt->NumParams++] = VT_DEFAULT;
                if (Vt->Params[Vt->NumParams - 1] == VT_DEFAULT) Vt->Params[Vt->NumParams - 1] = 0;
                if (Vt->Params[Vt->NumParams - 1] < 10000)
                    Vt->Params[Vt->NumParams - 1] = Vt->Params[Vt->NumParams - 1] * 10 + (c - L'0');
            }
            else if (c == L';' || c == L':')
            {
                if (Vt->NumParams == 0) Vt->Params[Vt->NumParams++] = VT_DEFAULT;
                if (Vt->NumParams < VT_MAX_PARAMS) Vt->Params[Vt->NumParams++] = VT_DEFAULT;
            }
            else if (c >= L'<' && c <= L'?')
            {
                Vt->Private = c;
            }
            else if (c >= L' ' && c <= L'/')
            {
                Vt->Intermediate = c;
            }
            else if (c >= L'@' && c <= L'~')
            {
                Vt->State = VtGround;
                VtExecuteCsi(FrontEnd, Buff, Vt, c);
            }
            else if (c == 0x1B)
            {
                Vt->State = VtEscape;   /* Cancelled by a new sequence */
            }
            else if (c == 0x18 || c == 0x1A)
            {
                Vt->State = VtGround;   /* CAN / SUB */
            }
            break;

        case VtOsc:
            i++;
            if (c == 0x07)
            {
                Vt->State = VtGround;
                VtExecuteOsc(FrontEnd, Vt);
            }
            else if (c == 0x1B)
            {
                Vt->State = VtOscEscape;
            }
            else if (Vt->OscLength < VT_MAX_OSC - 1)
            {
                Vt->Osc[Vt->OscLength++] = c;
            }
            break;

        case VtOscEscape:
            /* ESC \ (ST) ends the OSC; another ESC starts a new sequence */
            VtExecuteOsc(FrontEnd, Vt);
            if (c == L'\\')
            {
                Vt->State = VtGround;
                i++;
            }
            else
            {
                Vt->State = VtEscape;   /* c is the byte after that ESC */
            }
            break;

        case VtString:
            i++;
            if (c == 0x07) Vt->State = VtGround;
            else if (c == 0x1B) Vt->State = VtEscapeOther;
            break;
        }
    }

    /* Show what the sequences changed */
    if (Vt->Dirty && (PCONSOLE_SCREEN_BUFFER)Buff == Console->ActiveBuffer)
    {
        SMALL_RECT View;
        View.Left = 0;
        View.Top = Buff->ViewOrigin.Y;
        View.Right = Buff->ScreenBufferSize.X - 1;
        View.Bottom = min(Buff->ViewOrigin.Y + Buff->ViewSize.Y, Buff->ScreenBufferSize.Y) - 1;
        FrontEnd->Vtbl->WriteStream(FrontEnd, &View, Buff->CursorPosition.X, Buff->CursorPosition.Y,
                                    0, Buffer, Length);
        TermSetCursorInfo(Console, (PCONSOLE_SCREEN_BUFFER)Buff);
    }

    return STATUS_SUCCESS;
}

/*
 * ENABLE_VIRTUAL_TERMINAL_INPUT: the sequence of a key without a character
 * (as a terminal sends it), or NULL. Modifier n: 1 + Shift + 2 Alt + 4 Ctrl.
 */
BOOLEAN
VtTranslateKey(PCONSRV_CONSOLE Console,
               PKEY_EVENT_RECORD Key,
               PWCHAR Sequence,
               ULONG SequenceSize)
{
    DWORD State = Key->dwControlKeyState;
    UINT Modifier = 1 + ((State & SHIFT_PRESSED) ? 1 : 0)
                      + ((State & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) ? 2 : 0)
                      + ((State & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) ? 4 : 0);
    WCHAR Final = 0;
    UINT Number = 0;

    switch (Key->wVirtualKeyCode)
    {
    case VK_UP:     Final = L'A'; break;
    case VK_DOWN:   Final = L'B'; break;
    case VK_RIGHT:  Final = L'C'; break;
    case VK_LEFT:   Final = L'D'; break;
    case VK_HOME:   Final = L'H'; break;
    case VK_END:    Final = L'F'; break;
    case VK_F1:     Final = L'P'; break;
    case VK_F2:     Final = L'Q'; break;
    case VK_F3:     Final = L'R'; break;
    case VK_F4:     Final = L'S'; break;
    case VK_INSERT: Number = 2; break;
    case VK_DELETE: Number = 3; break;
    case VK_PRIOR:  Number = 5; break;
    case VK_NEXT:   Number = 6; break;
    case VK_F5:     Number = 15; break;
    case VK_F6:     Number = 17; break;
    case VK_F7:     Number = 18; break;
    case VK_F8:     Number = 19; break;
    case VK_F9:     Number = 20; break;
    case VK_F10:    Number = 21; break;
    case VK_F11:    Number = 23; break;
    case VK_F12:    Number = 24; break;
    case VK_TAB:
        if (!(State & SHIFT_PRESSED)) return FALSE;
        wcsncpy(Sequence, L"\x1b[Z", SequenceSize);
        return TRUE;
    case VK_BACK:
        /* DEL, and BS with Ctrl, as the Windows console */
        _snwprintf(Sequence, SequenceSize, L"%s%c",
                   (State & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) ? L"\x1b" : L"",
                   (State & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) ? 0x08 : 0x7F);
        return TRUE;
    default:
        /* Alt + a character: ESC and the character */
        if ((State & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) &&
            !(State & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) &&
            Key->uChar.UnicodeChar)
        {
            _snwprintf(Sequence, SequenceSize, L"\x1b%c", Key->uChar.UnicodeChar);
            return TRUE;
        }
        return FALSE;
    }

    if (Final)
    {
        BOOLEAN Ss3 = (Key->wVirtualKeyCode >= VK_F1 && Key->wVirtualKeyCode <= VK_F4) ||
                      (Console->VtApplicationCursorKeys && Final >= L'A' && Final <= L'H');
        if (Modifier > 1)
            _snwprintf(Sequence, SequenceSize, L"\x1b[1;%u%c", Modifier, Final);
        else
            _snwprintf(Sequence, SequenceSize, Ss3 ? L"\x1bO%c" : L"\x1b[%c", Final);
    }
    else if (Modifier > 1)
    {
        _snwprintf(Sequence, SequenceSize, L"\x1b[%u;%u~", Number, Modifier);
    }
    else
    {
        _snwprintf(Sequence, SequenceSize, L"\x1b[%u~", Number);
    }
    Sequence[SequenceSize - 1] = UNICODE_NULL;
    return TRUE;
}

/* EOF */
