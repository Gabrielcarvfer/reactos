/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for the console's virtual terminal sequences (Windows 10)
 *
 * ENABLE_VIRTUAL_TERMINAL_PROCESSING / _INPUT, as used by Node.js, Python
 * and the like: colours, cursor moves, erase, insert / delete, scrolling
 * regions, DEC line drawing, delayed wrap, DSR replies, the alternate screen.
 * The checks write to the screen and read it back with ReadConsoleOutput, in
 * a child with a console of its own; its results come back through a file.
 */

#include "precomp.h"

#ifndef SW_SHOWMINNOACTIVE
#define SW_SHOWMINNOACTIVE 7
#endif

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#ifndef ENABLE_VIRTUAL_TERMINAL_INPUT
#define ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200
#endif
#ifndef COMMON_LVB_REVERSE_VIDEO
#define COMMON_LVB_REVERSE_VIDEO 0x4000
#endif
#ifndef COMMON_LVB_UNDERSCORE
#define COMMON_LVB_UNDERSCORE 0x8000
#endif

static HANDLE hOut, hIn;

static VOID
Write(
    _In_ PCSTR Text)
{
    DWORD Written;
    WriteConsoleA(hOut, Text, (DWORD)strlen(Text), &Written, NULL);
}

/* The character and attribute at (x, y) of the window (0-based) */
static CHAR_INFO
Cell(
    _In_ SHORT X,
    _In_ SHORT Y)
{
    CONSOLE_SCREEN_BUFFER_INFO Info;
    CHAR_INFO Char;
    COORD Size = { 1, 1 }, Origin = { 0, 0 };
    SMALL_RECT Rect;

    ZeroMemory(&Char, sizeof(Char));
    GetConsoleScreenBufferInfo(hOut, &Info);
    Rect.Left = Rect.Right = X;
    Rect.Top = Rect.Bottom = Info.srWindow.Top + Y;
    ReadConsoleOutputW(hOut, &Char, Size, Origin, &Rect);
    return Char;
}

static COORD
Cursor(VOID)
{
    CONSOLE_SCREEN_BUFFER_INFO Info;
    COORD Position;

    GetConsoleScreenBufferInfo(hOut, &Info);
    Position.X = Info.dwCursorPosition.X;
    Position.Y = Info.dwCursorPosition.Y - Info.srWindow.Top;
    return Position;
}

static VOID
Child(VOID)
{
    CONSOLE_SCREEN_BUFFER_INFO Info;
    DWORD Mode = 0, InMode = 0;
    CHAR_INFO Char, Other;
    COORD Position;
    int i;

    hOut = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL, OPEN_EXISTING, 0, NULL);
    hIn = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                      NULL, OPEN_EXISTING, 0, NULL);
    if (hOut == INVALID_HANDLE_VALUE || hIn == INVALID_HANDLE_VALUE)
    {
        skip("No console (error %lu)\n", GetLastError());
        return;
    }
    GetConsoleScreenBufferInfo(hOut, &Info);
    if (Info.srWindow.Bottom - Info.srWindow.Top + 1 < 25 || Info.dwSize.X < 60 ||
        Info.srWindow.Right - Info.srWindow.Left + 1 != Info.dwSize.X || Info.dwSize.X >= 400)
    {
        skip("Console window %dx%d (buffer width %d) is not usable\n",
             Info.srWindow.Right - Info.srWindow.Left + 1, Info.srWindow.Bottom - Info.srWindow.Top + 1, Info.dwSize.X);
        goto Cleanup;
    }

    /* The modes */
    GetConsoleMode(hOut, &Mode);
    ok(SetConsoleMode(hOut, Mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING),
       "SetConsoleMode(VT processing) failed with %lu\n", GetLastError());
    GetConsoleMode(hOut, &Mode);
    ok(Mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING, "GetConsoleMode lost VT processing (0x%lx)\n", Mode);
    if (!(Mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING))
    {
        skip("No VT processing\n");
        goto Cleanup;
    }
    GetConsoleMode(hIn, &InMode);
    ok(SetConsoleMode(hIn, InMode | ENABLE_VIRTUAL_TERMINAL_INPUT),
       "SetConsoleMode(VT input) failed with %lu\n", GetLastError());
    SetConsoleMode(hIn, InMode);

    /* Clear, home, title */
    Write("\x1b[2J\x1b[H\x1b]0;VT test window\x07");
    Position = Cursor();
    ok(Position.X == 0 && Position.Y == 0, "ESC[2J ESC[H: cursor at %d,%d, expected home\n", Position.X, Position.Y);
    {
        CHAR Title[100] = "";
        GetConsoleTitleA(Title, sizeof(Title));
        ok(!strcmp(Title, "VT test window"), "OSC 0: window title '%s'\n", Title);
    }

    /* Colours: 16 backgrounds on row 1 */
    Write("\x1b[1;1HColours:");
    for (i = 0; i < 8; i++)
    {
        CHAR Text[40];
        StringCbPrintfA(Text, sizeof(Text), "\x1b[%dm  \x1b[%dm  ", 40 + i, 100 + i);
        Write(Text);
    }
    Write("\x1b[0m");
    Char = Cell(8, 0);
    ok((Char.Attributes & 0xF0) == 0x00, "SGR 40: attributes 0x%04x, expected a black background\n", Char.Attributes);
    Char = Cell(10, 0);
    ok((Char.Attributes & 0xF0) == 0x80, "SGR 100: attributes 0x%04x, expected a grey background\n", Char.Attributes);
    Char = Cell(12, 0);
    ok((Char.Attributes & 0xF0) == 0x40, "SGR 41: attributes 0x%04x, expected a red background\n", Char.Attributes);

    /* Foreground, bold, underline, reverse, 256 colours, RGB */
    Write("\x1b[2;1H\x1b[31mred\x1b[0m \x1b[1;32mbold green\x1b[0m \x1b[4munderline\x1b[0m \x1b[7mreverse\x1b[0m "
          "\x1b[38;5;196m256:196\x1b[0m \x1b[38;2;0;0;255mRGB blue\x1b[0m");
    Char = Cell(0, 1);
    ok(Char.Char.UnicodeChar == 'r' && (Char.Attributes & 0x0F) == 0x04,
       "SGR 31: '%c' attributes 0x%04x, expected red 'r'\n", (char)Char.Char.UnicodeChar, Char.Attributes);
    Char = Cell(4, 1);
    ok((Char.Attributes & 0x0F) == 0x0A, "SGR 1;32: attributes 0x%04x, expected bright green\n", Char.Attributes);
    Char = Cell(15, 1);
    ok(Char.Attributes & COMMON_LVB_UNDERSCORE, "SGR 4: attributes 0x%04x, expected underline\n", Char.Attributes);
    Char = Cell(25, 1);
    ok(Char.Attributes & COMMON_LVB_REVERSE_VIDEO, "SGR 7: attributes 0x%04x, expected reverse\n", Char.Attributes);
    Char = Cell(33, 1);
    ok((Char.Attributes & 0x0F) == 0x0C || (Char.Attributes & 0x0F) == 0x04,
       "SGR 38;5;196: attributes 0x%04x, expected red\n", Char.Attributes);
    Char = Cell(41, 1);
    ok((Char.Attributes & 0x0F) == 0x01 || (Char.Attributes & 0x0F) == 0x09,
       "SGR 38;2;0;0;255: attributes 0x%04x, expected blue\n", Char.Attributes);

    /* A box with DEC line drawing at an absolute position */
    Write("\x1b[4;10H\x1b(0lqqqqqqqqk\x1b[5;10Hx\x1b[5;19Hx\x1b[6;10Hmqqqqqqqqj\x1b(B\x1b[5;12Hbox");
    Char = Cell(9, 3);
    ok(Char.Char.UnicodeChar == 0x250C, "ESC(0 l: U+%04X at row 4 col 10, expected U+250C\n", Char.Char.UnicodeChar);
    Char = Cell(10, 3);
    ok(Char.Char.UnicodeChar == 0x2500, "ESC(0 q: U+%04X, expected U+2500\n", Char.Char.UnicodeChar);
    Char = Cell(11, 4);
    ok(Char.Char.UnicodeChar == 'b', "CUP 5;12: '%c', expected 'b'\n", (char)Char.Char.UnicodeChar);

    /* Relative moves */
    Write("\x1b[8;1H");
    Write("\x1b[5C\x1b[1B\x1b[2DX");
    Position = Cursor();
    Char = Cell(3, 8);
    ok(Char.Char.UnicodeChar == 'X' && Position.X == 4 && Position.Y == 8,
       "CUF CUD CUB: cell '%c', cursor %d,%d\n", (char)Char.Char.UnicodeChar, Position.X, Position.Y);

    /* Erase in line */
    Write("\x1b[10;1H0123456789\x1b[10;5H\x1b[K");
    Char = Cell(3, 9);
    Other = Cell(5, 9);
    ok(Char.Char.UnicodeChar == '3' && Other.Char.UnicodeChar == ' ',
       "EL 0: '%c' '%c', expected '3' ' '\n", (char)Char.Char.UnicodeChar, (char)Other.Char.UnicodeChar);

    /* Insert / delete characters */
    Write("\x1b[11;1Habcdef\x1b[11;2H\x1b[2P");
    Char = Cell(1, 10);
    ok(Char.Char.UnicodeChar == 'd', "DCH 2: '%c', expected 'd'\n", (char)Char.Char.UnicodeChar);
    Write("\x1b[11;1H\x1b[1@");
    Char = Cell(1, 10);
    ok(Char.Char.UnicodeChar == 'a', "ICH 1: '%c', expected 'a'\n", (char)Char.Char.UnicodeChar);

    /* Scrolling region: rows 13-15, a LF at its bottom scrolls only them */
    Write("\x1b[13;1Hrow13\x1b[14;1Hrow14\x1b[15;1Hrow15\x1b[16;1Hrow16");
    Write("\x1b[13;15r\x1b[15;1H\n");
    Char = Cell(3, 12);
    Other = Cell(3, 15);
    ok(Char.Char.UnicodeChar == '1' && Cell(4, 12).Char.UnicodeChar == '4' && Other.Char.UnicodeChar == '1',
       "DECSTBM: row 13 now '%c', row 16 '%c'\n", (char)Char.Char.UnicodeChar, (char)Other.Char.UnicodeChar);
    Write("\x1b[r");

    /* Save / restore the cursor */
    Write("\x1b[18;7H\x1b" "7\x1b[1;1H\x1b" "8Z");
    Char = Cell(6, 17);
    ok(Char.Char.UnicodeChar == 'Z', "DECSC / DECRC: '%c', expected 'Z'\n", (char)Char.Char.UnicodeChar);

    /* Delayed wrap: a full line, then CR LF, does not leave an empty line */
    {
        CHAR Line[400];

        memset(Line, '=', Info.dwSize.X);
        Line[Info.dwSize.X] = 0;
        Write("\x1b[20;1H");
        Write(Line);
        Write("\r\nnext");
        Char = Cell(0, 20);
        ok(Char.Char.UnicodeChar == 'n', "Delayed wrap: row 21 starts with '%c' (width %d)\n",
           (char)Char.Char.UnicodeChar, Info.dwSize.X);
    }

    /* Hide / show the cursor */
    {
        CONSOLE_CURSOR_INFO CursorInfo;

        Write("\x1b[?25l");
        GetConsoleCursorInfo(hOut, &CursorInfo);
        ok(!CursorInfo.bVisible, "?25l did not hide the cursor\n");
        Write("\x1b[?25h");
        GetConsoleCursorInfo(hOut, &CursorInfo);
        ok(CursorInfo.bVisible, "?25h did not show the cursor\n");
    }

    /* DSR: the cursor position comes back as input */
    {
        INPUT_RECORD Records[32];
        DWORD Count = 0, k;
        CHAR Reply[40] = "";
        int Length = 0;

        GetConsoleMode(hIn, &InMode);
        SetConsoleMode(hIn, InMode | ENABLE_VIRTUAL_TERMINAL_INPUT);
        FlushConsoleInputBuffer(hIn);
        Write("\x1b[22;5H\x1b[6n");
        Sleep(200);
        if (PeekConsoleInputW(hIn, Records, _countof(Records), &Count) && Count)
        {
            ReadConsoleInputW(hIn, Records, Count, &Count);
            for (k = 0; k < Count && Length < 39; k++)
            {
                if (Records[k].EventType == KEY_EVENT && Records[k].Event.KeyEvent.bKeyDown)
                    Reply[Length++] = (CHAR)Records[k].Event.KeyEvent.uChar.UnicodeChar;
            }
        }
        Reply[Length] = 0;
        SetConsoleMode(hIn, InMode);
        ok(!strcmp(Reply, "\x1b[22;5R"), "DSR 6n reply '%s', expected 'ESC[22;5R'\n",
           Reply[0] == 0x1b ? Reply + 1 : Reply);
    }

    /* Alternate screen: switch, write, switch back: the main text is back */
    Write("\x1b[24;1Hmain screen text");
    Write("\x1b[?1049h\x1b[Halternate");
    Char = Cell(0, 23);
    ok(Char.Char.UnicodeChar == ' ', "?1049h: '%c' in the alternate screen, expected blank\n", (char)Char.Char.UnicodeChar);
    Write("\x1b[?1049l");
    Char = Cell(0, 23);
    ok(Char.Char.UnicodeChar == 'm', "?1049l: '%c', expected the main screen's 'm'\n", (char)Char.Char.UnicodeChar);

Cleanup:
    CloseHandle(hIn);
    CloseHandle(hOut);
}

/* Runs "kernel32_apitest <Test> child" in a new console, its output in a
   temporary file; each failure it reports becomes a failure here */
static VOID
RunChildInNewConsole(
    _In_ PCWSTR Test)
{
    SECURITY_ATTRIBUTES Inheritable = { sizeof(Inheritable), NULL, TRUE };
    WCHAR FileName[MAX_PATH], CommandLine[MAX_PATH + 64], TempDir[MAX_PATH], TempFile[MAX_PATH];
    STARTUPINFOW StartupInfo;
    PROCESS_INFORMATION ProcessInfo;
    DWORD Wait, ExitCode = 0, Size, Read = 0, Failures = 0;
    HANDLE hOutput;
    PCHAR Buffer, Line, Next;

    GetTempPathW(_countof(TempDir), TempDir);
    if (!GetTempFileNameW(TempDir, L"ccc", 0, TempFile))
    {
        skip("GetTempFileName failed with %lu\n", GetLastError());
        return;
    }
    hOutput = CreateFileW(TempFile, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                          &Inheritable, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (hOutput == INVALID_HANDLE_VALUE)
    {
        skip("Cannot create %ls (error %lu)\n", TempFile, GetLastError());
        DeleteFileW(TempFile);
        return;
    }

    GetModuleFileNameW(NULL, FileName, _countof(FileName));
    StringCbPrintfW(CommandLine, sizeof(CommandLine), L"\"%ls\" %ls child", FileName, Test);
    ZeroMemory(&StartupInfo, sizeof(StartupInfo));
    StartupInfo.cb = sizeof(StartupInfo);
    StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    StartupInfo.wShowWindow = SW_SHOWMINNOACTIVE;
    StartupInfo.hStdOutput = hOutput;
    StartupInfo.hStdError = hOutput;
    if (!CreateProcessW(FileName, CommandLine, NULL, NULL, TRUE, CREATE_NEW_CONSOLE, NULL, NULL,
                        &StartupInfo, &ProcessInfo))
    {
        skip("CreateProcess failed with %lu\n", GetLastError());
        CloseHandle(hOutput);
        DeleteFileW(TempFile);
        return;
    }
    Wait = WaitForSingleObject(ProcessInfo.hProcess, 60000);
    ok(Wait == WAIT_OBJECT_0, "The child did not end (wait %lu)\n", Wait);
    if (Wait != WAIT_OBJECT_0)
    {
        TerminateProcess(ProcessInfo.hProcess, 255);
        WaitForSingleObject(ProcessInfo.hProcess, 5000);
    }
    GetExitCodeProcess(ProcessInfo.hProcess, &ExitCode);
    CloseHandle(ProcessInfo.hThread);
    CloseHandle(ProcessInfo.hProcess);

    Size = GetFileSize(hOutput, NULL);
    Buffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Size + 1);
    if (Buffer && Size != INVALID_FILE_SIZE)
    {
        SetFilePointer(hOutput, 0, NULL, FILE_BEGIN);
        ReadFile(hOutput, Buffer, Size, &Read, NULL);
        for (Line = Buffer; Line && *Line; Line = Next)
        {
            Next = strchr(Line, '\n');
            if (Next)
                *Next++ = 0;
            if (*Line && Line[strlen(Line) - 1] == '\r')
                Line[strlen(Line) - 1] = 0;
            if (strstr(Line, "Test failed"))
            {
                ok(0, "child: %s\n", Line);
                Failures++;
            }
            else if (strstr(Line, "Tests skipped"))
            {
                skip("child: %s\n", Line);
            }
        }
    }
    if (Buffer)
        HeapFree(GetProcessHeap(), 0, Buffer);
    ok(ExitCode == Failures, "Child exit code %lu, but %lu failures reported\n", ExitCode, Failures);
    CloseHandle(hOutput);
    DeleteFileW(TempFile);
}

START_TEST(ConsoleVT)
{
    char **argv;
    int argc;

    argc = winetest_get_mainargs(&argv);
    if (argc >= 3 && !strcmp(argv[2], "child"))
    {
        Child();
        return;
    }
    RunChildInNewConsole(L"ConsoleVT");
}
