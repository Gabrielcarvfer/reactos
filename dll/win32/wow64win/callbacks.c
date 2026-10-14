/*
 * PROJECT:     ReactOS WoW64
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     The callbacks of win32k to the 32-bit user32: the 64-bit
 *              ntdll calls these (the 64-bit KernelCallbackTable); they
 *              convert the arguments, run the 32-bit callback, and convert
 *              its result back
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

#include "wow64win_private.h"
#include "win32k_structs.h"

/* From wow64.dll: runs callback Id of the 32-bit user32 (Wine's) */
NTSTATUS WINAPI Wow64KiUserCallbackDispatcher(ULONG Id, void *Args, ULONG Len, void **RetPtr, ULONG *RetLen);

#define MAX_CALLBACK_BUFFER 0x10000

static
NTSTATUS
Call32(ULONG Id, void *Args, ULONG Len, void **Ret, ULONG *RetLen)
{
    *Ret = NULL;
    *RetLen = 0;
    return Wow64KiUserCallbackDispatcher(Id, Args, Len, Ret, RetLen);
}

/* A callback that returns a handle or an LRESULT: 4 bytes in 32-bit code */
static
NTSTATUS
ReturnPointerSized(NTSTATUS Status, void *Ret, ULONG RetLen)
{
    LONG_PTR Result = 0;

    if (Ret && RetLen >= sizeof(LONG)) Result = *(LONG *)Ret;
    return ZwCallbackReturn(&Result, sizeof(Result), Status);
}

/* A callback whose result has the same layout in 32 and 64 bits */
static
NTSTATUS
ReturnAsIs(NTSTATUS Status, void *Ret, ULONG RetLen)
{
    return ZwCallbackReturn(Ret, RetLen, Status);
}

/*
 * Arguments made of a structure followed by data (strings...), which the
 * structure refers to with offsets from its start: convert the structure,
 * move the data, and shift the offsets (by the difference of the sizes).
 */
static
ULONG
ConvertWithData(void *Dst32, ULONG Size32, const void *Src64, ULONG Size64, ULONG Length,
                void (*To32)(void *, const void *))
{
    To32(Dst32, Src64);
    if (Length > Size64)
    {
        if (Size32 + Length - Size64 > MAX_CALLBACK_BUFFER) return Size32;
        memcpy((PBYTE)Dst32 + Size32, (const BYTE *)Src64 + Size64, Length - Size64);
        return Size32 + Length - Size64;
    }
    return Size32;
}

#define SHIFT_OFFSET(Field32, Size32, Size64) \
    do { if (Field32) Field32 = Field32 - (Size64) + (Size32); } while (0)

/* MESSAGES *****************************************************************/

/* The data that lParam points to, for the messages where its layout differs */
static
INT
MessageData64To32(UINT Msg, WPARAM wParam, const BYTE *Data, INT Size, BYTE *Data32)
{
    switch (Msg)
    {
        case WM_CREATE:
        case WM_NCCREATE:
        {
            CREATESTRUCTW_32 *Cs32 = (CREATESTRUCTW_32 *)Data32;
            ULONG Length;

            if (Size < (INT)sizeof(CREATESTRUCTW)) break;
            Length = ConvertWithData(Cs32, sizeof(*Cs32), Data, sizeof(CREATESTRUCTW), Size,
                                     (void *)CREATESTRUCTW_64to32);
            /* The names follow the structure: their offsets move with it */
            SHIFT_OFFSET(Cs32->lpszName, sizeof(*Cs32), sizeof(CREATESTRUCTW));
            SHIFT_OFFSET(Cs32->lpszClass, sizeof(*Cs32), sizeof(CREATESTRUCTW));
            return Length;
        }

        case WM_NCCALCSIZE:
            if (wParam && Size >= (INT)(sizeof(NCCALCSIZE_PARAMS) + sizeof(WINDOWPOS)))
            {
                NCCALCSIZE_PARAMS_64to32((NCCALCSIZE_PARAMS_32 *)Data32, (const NCCALCSIZE_PARAMS *)Data);
                WINDOWPOS_64to32((WINDOWPOS_32 *)(Data32 + sizeof(NCCALCSIZE_PARAMS_32)),
                                 (const WINDOWPOS *)(Data + sizeof(NCCALCSIZE_PARAMS)));
                return sizeof(NCCALCSIZE_PARAMS_32) + sizeof(WINDOWPOS_32);
            }
            break;

        case WM_WINDOWPOSCHANGED:
        case WM_WINDOWPOSCHANGING:
            WINDOWPOS_64to32((WINDOWPOS_32 *)Data32, (const WINDOWPOS *)Data);
            return sizeof(WINDOWPOS_32);

        case WM_MEASUREITEM:
            MEASUREITEMSTRUCT_64to32((MEASUREITEMSTRUCT_32 *)Data32, (const MEASUREITEMSTRUCT *)Data);
            return sizeof(MEASUREITEMSTRUCT_32);

        case WM_DRAWITEM:
            DRAWITEMSTRUCT_64to32((DRAWITEMSTRUCT_32 *)Data32, (const DRAWITEMSTRUCT *)Data);
            return sizeof(DRAWITEMSTRUCT_32);

        case WM_HELP:
            HELPINFO_64to32((HELPINFO_32 *)Data32, (const HELPINFO *)Data);
            ((HELPINFO_32 *)Data32)->cbSize = sizeof(HELPINFO_32);
            return sizeof(HELPINFO_32);

        case WM_NEXTMENU:
            MDINEXTMENU_64to32((MDINEXTMENU_32 *)Data32, (const MDINEXTMENU *)Data);
            return sizeof(MDINEXTMENU_32);

        case WM_COPYDATA:
        {
            COPYDATASTRUCT_32 *Cds32 = (COPYDATASTRUCT_32 *)Data32;
            ULONG Length = ConvertWithData(Cds32, sizeof(*Cds32), Data, sizeof(COPYDATASTRUCT), Size,
                                           (void *)COPYDATASTRUCT_64to32);
            /* (user32 points lpData to the data that follows) */
            return Length;
        }
    }

    /* The same layout */
    memcpy(Data32, Data, Size);
    return Size;
}

/* And back, for the messages whose data win32k reads after the call */
static
VOID
MessageData32To64(UINT Msg, WPARAM wParam, BYTE *Data, INT Size, const BYTE *Data32, INT Size32)
{
    switch (Msg)
    {
        case WM_CREATE:
        case WM_NCCREATE:
            if (Size < (INT)sizeof(CREATESTRUCTW)) return;
            /* Only the structure: the names stay */
            CREATESTRUCTW_32to64((CREATESTRUCTW *)Data, (const CREATESTRUCTW_32 *)Data32);
            return;

        case WM_NCCALCSIZE:
            if (wParam && Size >= (INT)(sizeof(NCCALCSIZE_PARAMS) + sizeof(WINDOWPOS)))
            {
                NCCALCSIZE_PARAMS *Params = (NCCALCSIZE_PARAMS *)Data;

                NCCALCSIZE_PARAMS_32to64(Params, (const NCCALCSIZE_PARAMS_32 *)Data32);
                WINDOWPOS_32to64((WINDOWPOS *)(Params + 1),
                                 (const WINDOWPOS_32 *)(Data32 + sizeof(NCCALCSIZE_PARAMS_32)));
                return;
            }
            break;

        case WM_WINDOWPOSCHANGED:
        case WM_WINDOWPOSCHANGING:
            WINDOWPOS_32to64((WINDOWPOS *)Data, (const WINDOWPOS_32 *)Data32);
            return;

        case WM_MEASUREITEM:
            MEASUREITEMSTRUCT_32to64((MEASUREITEMSTRUCT *)Data, (const MEASUREITEMSTRUCT_32 *)Data32);
            return;

        case WM_DRAWITEM:
            DRAWITEMSTRUCT_32to64((DRAWITEMSTRUCT *)Data, (const DRAWITEMSTRUCT_32 *)Data32);
            return;

        case WM_HELP:
            HELPINFO_32to64((HELPINFO *)Data, (const HELPINFO_32 *)Data32);
            ((HELPINFO *)Data)->cbSize = sizeof(HELPINFO);
            return;

        case WM_NEXTMENU:
            MDINEXTMENU_32to64((MDINEXTMENU *)Data, (const MDINEXTMENU_32 *)Data32);
            return;

        case WM_COPYDATA:
            return;
    }

    memcpy(Data, Data32, min(Size, Size32));
}

/* THE CALLBACKS ************************************************************/

static
NTSTATUS
WINAPI
CbWindowProc(PVOID Arguments, ULONG Length)
{
    WINDOWPROC_CALLBACK_ARGUMENTS *Args = Arguments;
    WINDOWPROC_CALLBACK_ARGUMENTS_32 *Args32, *Ret32;
    BYTE *Data = (BYTE *)(Args + 1);
    INT Size = Args->lParamBufferSize, Size32 = -1;
    ULONG RetLen;
    void *Ret;
    NTSTATUS Status;

    if (Length < sizeof(*Args)) return ZwCallbackReturn(NULL, 0, STATUS_INFO_LENGTH_MISMATCH);

    /* The converted data can be longer (strings move as is, but a structure
     * never grows when it shrinks to 32 bits) */
    Args32 = RtlAllocateHeap(RtlGetProcessHeap(), 0, sizeof(*Args32) + max(Size, 0) + 16);
    if (!Args32) return ZwCallbackReturn(NULL, 0, STATUS_NO_MEMORY);
    WINDOWPROC_CALLBACK_ARGUMENTS_64to32(Args32, Args);
    if (Size >= 0)
        Size32 = MessageData64To32(Args->Msg, Args->wParam, Data, Size, (BYTE *)(Args32 + 1));
    Args32->lParamBufferSize = Size32;

    Status = Call32(USER32_CALLBACK_WINDOWPROC, Args32, sizeof(*Args32) + max(Size32, 0), &Ret, &RetLen);

    Ret32 = Ret;
    if (Ret32 && RetLen >= sizeof(*Ret32))
    {
        Args->Result = (LONG)Ret32->Result;
        if (Size >= 0)
            MessageData32To64(Args->Msg, Args->wParam, Data, Size, (const BYTE *)(Ret32 + 1), Size32);
    }
    RtlFreeHeap(RtlGetProcessHeap(), 0, Args32);
    return ZwCallbackReturn(Args, Length, Status);
}

static
NTSTATUS
WINAPI
CbSendAsyncProc(PVOID Arguments, ULONG Length)
{
    SENDASYNCPROC_CALLBACK_ARGUMENTS_32 Args32;
    ULONG RetLen;
    void *Ret;
    NTSTATUS Status;

    SENDASYNCPROC_CALLBACK_ARGUMENTS_64to32(&Args32, Arguments);
    Status = Call32(USER32_CALLBACK_SENDASYNCPROC, &Args32, sizeof(Args32), &Ret, &RetLen);
    return ZwCallbackReturn(NULL, 0, Status);
}

/* No arguments, or the same ones; a handle (or an LRESULT) comes back */
static
NTSTATUS
WINAPI
CbLoadSysMenuTemplate(PVOID Arguments, ULONG Length)
{
    ULONG RetLen;
    void *Ret;
    NTSTATUS Status = Call32(USER32_CALLBACK_LOADSYSMENUTEMPLATE, Arguments, Length, &Ret, &RetLen);
    return ReturnPointerSized(Status, Ret, RetLen);
}

static
NTSTATUS
WINAPI
CbLoadDefaultCursors(PVOID Arguments, ULONG Length)
{
    ULONG RetLen;
    void *Ret;
    NTSTATUS Status = Call32(USER32_CALLBACK_LOADDEFAULTCURSORS, Arguments, Length, &Ret, &RetLen);
    return ReturnPointerSized(Status, Ret, RetLen);
}

/* The data of WH_CALLWNDPROC(RET) after the structure: the message's lParam
 * data (for WM_(NC)CREATE, the bare CREATESTRUCT: its names are pointers) */
static
INT
HookMessageData64To32(UINT Msg, WPARAM wParam, const BYTE *Data, INT Size, BYTE *Data32)
{
    if ((Msg == WM_CREATE || Msg == WM_NCCREATE) && Size >= (INT)sizeof(CREATESTRUCTW))
    {
        CREATESTRUCTW_64to32((CREATESTRUCTW_32 *)Data32, (const CREATESTRUCTW *)Data);
        return sizeof(CREATESTRUCTW_32);
    }
    return MessageData64To32(Msg, wParam, Data, Size, Data32);
}

/*
 * A window hook of the 32-bit program (SetWindowsHookEx). The arguments are a
 * HOOKPROC_CALLBACK_ARGUMENTS followed by the data of the hook's kind, which
 * lParam points to as an offset from the start (see co_IntCallHookProc).
 */
static
NTSTATUS
WINAPI
CbHookProc(PVOID Arguments, ULONG Length)
{
    HOOKPROC_CALLBACK_ARGUMENTS *Common = Arguments;
    HOOKPROC_CALLBACK_ARGUMENTS_32 *Common32, *Ret32;
    BYTE *Extra = (BYTE *)(Common + 1), *Extra32;
    ULONG Size32, RetLen;
    void *Ret;
    NTSTATUS Status;

    if (Length < sizeof(*Common)) return ZwCallbackReturn(NULL, 0, STATUS_INFO_LENGTH_MISMATCH);

    /* A global hook of a 64-bit program (its procedure is above 4 GB): it
     * cannot run in a 32-bit process, as on Windows; the hook is skipped */
    if ((ULONG_PTR)Common->Proc > MAXULONG)
    {
        Common->Result = 0;
        return ZwCallbackReturn(Arguments, Length, STATUS_SUCCESS);
    }

    /* (the 32-bit data is never larger) */
    Common32 = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, Length + 64);
    if (!Common32) return ZwCallbackReturn(NULL, 0, STATUS_NO_MEMORY);
    HOOKPROC_CALLBACK_ARGUMENTS_64to32(Common32, Common);
    Extra32 = (BYTE *)(Common32 + 1);
    Size32 = sizeof(*Common32);

#define EXTRA(Type) \
    do { \
        Type##_64to32((Type##_32 *)Extra32, (const Type *)Extra); \
        Common32->lParam = sizeof(*Common32); \
        Size32 += sizeof(Type##_32); \
    } while (0)

    switch (Common->HookId)
    {
        case WH_CBT:
            switch (Common->Code)
            {
                case HCBT_CREATEWND:
                    EXTRA(HOOKPROC_CBT_CREATEWND_EXTRA_ARGUMENTS);
                    break;
                case HCBT_MOVESIZE:
                    memcpy(Extra32, Extra, sizeof(RECTL));
                    Common32->lParam = sizeof(*Common32);
                    Size32 += sizeof(RECTL);
                    break;
                case HCBT_ACTIVATE:
                    EXTRA(CBTACTIVATESTRUCT);
                    break;
                case HCBT_CLICKSKIPPED:
                    EXTRA(MOUSEHOOKSTRUCT);
                    break;
                /* the others have plain values */
            }
            break;

        case WH_KEYBOARD_LL:
            EXTRA(KBDLLHOOKSTRUCT);
            break;
        case WH_MOUSE_LL:
            EXTRA(MSLLHOOKSTRUCT);
            break;
        case WH_MOUSE:
            EXTRA(MOUSEHOOKSTRUCT);
            break;
        case WH_MSGFILTER:
        case WH_SYSMSGFILTER:
        case WH_GETMESSAGE:
            EXTRA(MSG);
            break;

        case WH_CALLWNDPROC:
        {
            const CWP_Struct *Cwp = Arguments;
            CWPSTRUCT_32 *Cwp32 = (CWPSTRUCT_32 *)Extra32;
            INT DataSize = 0;

            CWPSTRUCT_64to32(Cwp32, &Cwp->cwps);
            if (Common->lParamSize)
                DataSize = HookMessageData64To32(Cwp->cwps.message, Cwp->cwps.wParam, (const BYTE *)&Cwp->Extra,
                                                 Common->lParamSize, (BYTE *)(Cwp32 + 1));
            Common32->lParamSize = max(DataSize, 0);
            /* (user32 reads the data at the "Extra" field, right after) */
            Size32 += sizeof(*Cwp32) + max(DataSize, 0) + 4 * sizeof(ULONG);
            break;
        }
        case WH_CALLWNDPROCRET:
        {
            const CWPR_Struct *Cwpr = Arguments;
            CWPRETSTRUCT_32 *Cwpr32 = (CWPRETSTRUCT_32 *)Extra32;
            INT DataSize = 0;

            CWPRETSTRUCT_64to32(Cwpr32, &Cwpr->cwprs);
            if (Common->lParamSize)
                DataSize = HookMessageData64To32(Cwpr->cwprs.message, Cwpr->cwprs.wParam, (const BYTE *)&Cwpr->Extra,
                                                 Common->lParamSize, (BYTE *)(Cwpr32 + 1));
            Common32->lParamSize = max(DataSize, 0);
            Size32 += sizeof(*Cwpr32) + max(DataSize, 0) + 4 * sizeof(ULONG);
            break;
        }
    }
#undef EXTRA

    Status = Call32(USER32_CALLBACK_HOOKPROC, Common32, Size32, &Ret, &RetLen);

    /* The result, and what the hook may change */
    Ret32 = Ret;
    if (Ret32 && RetLen >= sizeof(*Ret32))
    {
        const BYTE *RetExtra32 = (const BYTE *)(Ret32 + 1);

        Common->Result = (LONG)Ret32->Result;
        switch (Common->HookId)
        {
            case WH_CBT:
                if (Common->Code == HCBT_CREATEWND && RetLen >= Size32)
                {
                    const HOOKPROC_CBT_CREATEWND_EXTRA_ARGUMENTS_32 *Cw32 = (const void *)RetExtra32;
                    HOOKPROC_CBT_CREATEWND_EXTRA_ARGUMENTS *Cw = (void *)Extra;

                    Cw->WndInsertAfter = LongToHandle(Cw32->WndInsertAfter);
                    Cw->Cs.x = Cw32->Cs.x;
                    Cw->Cs.y = Cw32->Cs.y;
                    Cw->Cs.cx = Cw32->Cs.cx;
                    Cw->Cs.cy = Cw32->Cs.cy;
                }
                else if (Common->Code == HCBT_MOVESIZE && RetLen >= Size32)
                {
                    memcpy(Extra, RetExtra32, sizeof(RECTL));
                }
                break;
            case WH_GETMESSAGE:
                if (RetLen >= Size32)
                    MSG_32to64((MSG *)Extra, (const MSG_32 *)RetExtra32);
                break;
        }
    }
    RtlFreeHeap(RtlGetProcessHeap(), 0, Common32);
    return ZwCallbackReturn(Arguments, Length, Status);
}

static
NTSTATUS
WINAPI
CbEventProc(PVOID Arguments, ULONG Length)
{
    EVENTPROC_CALLBACK_ARGUMENTS_32 Args32;
    ULONG RetLen;
    void *Ret;
    NTSTATUS Status;

    EVENTPROC_CALLBACK_ARGUMENTS_64to32(&Args32, Arguments);
    Status = Call32(USER32_CALLBACK_EVENTPROC, &Args32, sizeof(Args32), &Ret, &RetLen);
    return ZwCallbackReturn(NULL, 0, Status);
}

static
NTSTATUS
WINAPI
CbLoadMenu(PVOID Arguments, ULONG Length)
{
    BYTE Buffer[MAX_PATH * sizeof(WCHAR) + sizeof(LOADMENU_CALLBACK_ARGUMENTS_32)];
    ULONG Length32, RetLen;
    void *Ret;
    NTSTATUS Status;

    if (Length - sizeof(LOADMENU_CALLBACK_ARGUMENTS) + sizeof(LOADMENU_CALLBACK_ARGUMENTS_32) > sizeof(Buffer))
        return ZwCallbackReturn(NULL, 0, STATUS_BUFFER_OVERFLOW);
    Length32 = ConvertWithData(Buffer, sizeof(LOADMENU_CALLBACK_ARGUMENTS_32), Arguments,
                               sizeof(LOADMENU_CALLBACK_ARGUMENTS), Length, (void *)LOADMENU_CALLBACK_ARGUMENTS_64to32);
    /* (MenuName is an array at the end: its characters follow) */
    memcpy(((LOADMENU_CALLBACK_ARGUMENTS_32 *)Buffer)->MenuName,
           ((LOADMENU_CALLBACK_ARGUMENTS *)Arguments)->MenuName,
           Length - FIELD_OFFSET(LOADMENU_CALLBACK_ARGUMENTS, MenuName));
    Length32 = FIELD_OFFSET(LOADMENU_CALLBACK_ARGUMENTS_32, MenuName) +
               Length - FIELD_OFFSET(LOADMENU_CALLBACK_ARGUMENTS, MenuName);
    Status = Call32(USER32_CALLBACK_LOADMENU, Buffer, Length32, &Ret, &RetLen);
    return ReturnPointerSized(Status, Ret, RetLen);
}

static
NTSTATUS
WINAPI
CbClientThreadStartup(PVOID Arguments, ULONG Length)
{
    ULONG RetLen;
    void *Ret;
    NTSTATUS Status = Call32(USER32_CALLBACK_CLIENTTHREADSTARTUP, Arguments, Length, &Ret, &RetLen);
    return ZwCallbackReturn(NULL, 0, Status);
}

static
NTSTATUS
WINAPI
CbClientLoadLibrary(PVOID Arguments, ULONG Length)
{
    CLIENT_LOAD_LIBRARY_ARGUMENTS_32 *Args32;
    ULONG Length32, RetLen;
    void *Ret;
    NTSTATUS Status;

    if (Length > MAX_CALLBACK_BUFFER) return ZwCallbackReturn(NULL, 0, STATUS_BUFFER_OVERFLOW);
    Args32 = RtlAllocateHeap(RtlGetProcessHeap(), 0, Length);
    if (!Args32) return ZwCallbackReturn(NULL, 0, STATUS_NO_MEMORY);
    Length32 = ConvertWithData(Args32, sizeof(*Args32), Arguments, sizeof(CLIENT_LOAD_LIBRARY_ARGUMENTS),
                               Length, (void *)CLIENT_LOAD_LIBRARY_ARGUMENTS_64to32);
    /* The names follow: offsets from the start */
    SHIFT_OFFSET(Args32->strLibraryName.Buffer, sizeof(*Args32), sizeof(CLIENT_LOAD_LIBRARY_ARGUMENTS));
    SHIFT_OFFSET(Args32->strInitFuncName.Buffer, sizeof(*Args32), sizeof(CLIENT_LOAD_LIBRARY_ARGUMENTS));
    Status = Call32(USER32_CALLBACK_CLIENTLOADLIBRARY, Args32, Length32, &Ret, &RetLen);
    RtlFreeHeap(RtlGetProcessHeap(), 0, Args32);
    return ReturnPointerSized(Status, Ret, RetLen);
}

static
NTSTATUS
WINAPI
CbGetCharsetInfo(PVOID Arguments, ULONG Length)
{
    ULONG RetLen;
    void *Ret;
    NTSTATUS Status = Call32(USER32_CALLBACK_GETCHARSETINFO, Arguments, Length, &Ret, &RetLen);
    return ReturnAsIs(Status, Ret, RetLen);
}

static
NTSTATUS
WINAPI
CbCopyImage(PVOID Arguments, ULONG Length)
{
    COPYIMAGE_CALLBACK_ARGUMENTS_32 Args32;
    ULONG RetLen;
    void *Ret;
    NTSTATUS Status;

    COPYIMAGE_CALLBACK_ARGUMENTS_64to32(&Args32, Arguments);
    Status = Call32(USER32_CALLBACK_COPYIMAGE, &Args32, sizeof(Args32), &Ret, &RetLen);
    return ReturnPointerSized(Status, Ret, RetLen);
}

static
NTSTATUS
WINAPI
CbSetWndIcons(PVOID Arguments, ULONG Length)
{
    SETWNDICONS_CALLBACK_ARGUMENTS_32 Args32;
    ULONG RetLen;
    void *Ret;
    NTSTATUS Status;

    SETWNDICONS_CALLBACK_ARGUMENTS_64to32(&Args32, Arguments);
    Status = Call32(USER32_CALLBACK_SETWNDICONS, &Args32, sizeof(Args32), &Ret, &RetLen);
    if (Ret && RetLen >= sizeof(Args32))
        SETWNDICONS_CALLBACK_ARGUMENTS_32to64(Arguments, Ret);
    return ZwCallbackReturn(Arguments, Length, Status);
}

static
NTSTATUS
WINAPI
CbDeliverUserApc(PVOID Arguments, ULONG Length)
{
    ULONG RetLen;
    void *Ret;
    NTSTATUS Status = Call32(USER32_CALLBACK_DELIVERUSERAPC, Arguments, Length, &Ret, &RetLen);
    return ZwCallbackReturn(NULL, 0, Status);
}

static
NTSTATUS
WINAPI
CbNotSupported(PVOID Arguments, ULONG Length)
{
    /* FIXME: DDE, UMPD */
    DbgPrint("WOW64WIN: this callback is not supported yet\n");
    return ZwCallbackReturn(NULL, 0, STATUS_NOT_SUPPORTED);
}

static
NTSTATUS
WINAPI
CbSetObm(PVOID Arguments, ULONG Length)
{
    /* (OEMBITMAPINFO: numbers only) */
    ULONG RetLen;
    void *Ret;
    NTSTATUS Status = Call32(USER32_CALLBACK_SETOBM, Arguments, Length, &Ret, &RetLen);
    return ReturnAsIs(Status, Ret, RetLen);
}

static
NTSTATUS
WINAPI
CbLpk(PVOID Arguments, ULONG Length)
{
    LPK_CALLBACK_ARGUMENTS_32 *Args32;
    ULONG Length32, RetLen;
    void *Ret;
    NTSTATUS Status;

    if (Length > MAX_CALLBACK_BUFFER) return ZwCallbackReturn(NULL, 0, STATUS_BUFFER_OVERFLOW);
    Args32 = RtlAllocateHeap(RtlGetProcessHeap(), 0, Length);
    if (!Args32) return ZwCallbackReturn(NULL, 0, STATUS_NO_MEMORY);
    Length32 = ConvertWithData(Args32, sizeof(*Args32), Arguments, sizeof(LPK_CALLBACK_ARGUMENTS),
                               Length, (void *)LPK_CALLBACK_ARGUMENTS_64to32);
    SHIFT_OFFSET(Args32->lpString, sizeof(*Args32), sizeof(LPK_CALLBACK_ARGUMENTS));
    Status = Call32(USER32_CALLBACK_LPK, Args32, Length32, &Ret, &RetLen);
    RtlFreeHeap(RtlGetProcessHeap(), 0, Args32);
    /* (a BOOL) */
    return ReturnAsIs(Status, Ret, RetLen);
}

static
NTSTATUS
WINAPI
CbImmProcessKey(PVOID Arguments, ULONG Length)
{
    IMMPROCESSKEY_CALLBACK_ARGUMENTS_32 Args32;
    ULONG RetLen;
    void *Ret;
    NTSTATUS Status;

    IMMPROCESSKEY_CALLBACK_ARGUMENTS_64to32(&Args32, Arguments);
    Status = Call32(USER32_CALLBACK_IMMPROCESSKEY, &Args32, sizeof(Args32), &Ret, &RetLen);
    /* (a DWORD) */
    return ReturnAsIs(Status, Ret, RetLen);
}

static
NTSTATUS
WINAPI
CbImmLoadLayout(PVOID Arguments, ULONG Length)
{
    IMMLOADLAYOUT_CALLBACK_ARGUMENTS_32 Args32;
    IMMLOADLAYOUT_CALLBACK_OUTPUT Output;
    ULONG RetLen;
    void *Ret;
    NTSTATUS Status;

    IMMLOADLAYOUT_CALLBACK_ARGUMENTS_64to32(&Args32, Arguments);
    Status = Call32(USER32_CALLBACK_IMMLOADLAYOUT, &Args32, sizeof(Args32), &Ret, &RetLen);
    memset(&Output, 0, sizeof(Output));
    if (Ret && RetLen >= sizeof(IMMLOADLAYOUT_CALLBACK_OUTPUT_32))
        IMMLOADLAYOUT_CALLBACK_OUTPUT_32to64(&Output, Ret);
    return ZwCallbackReturn(&Output, sizeof(Output), Status);
}

/* The 64-bit callback table of a 32-bit process, in the order of u32cb.h */
PVOID Wow64winCallbacks[USER32_CALLBACK_COUNT] =
{
    CbWindowProc,           /* USER32_CALLBACK_WINDOWPROC */
    CbSendAsyncProc,        /* USER32_CALLBACK_SENDASYNCPROC */
    CbLoadSysMenuTemplate,  /* USER32_CALLBACK_LOADSYSMENUTEMPLATE */
    CbLoadDefaultCursors,   /* USER32_CALLBACK_LOADDEFAULTCURSORS */
    CbHookProc,             /* USER32_CALLBACK_HOOKPROC */
    CbEventProc,            /* USER32_CALLBACK_EVENTPROC */
    CbLoadMenu,             /* USER32_CALLBACK_LOADMENU */
    CbClientThreadStartup,  /* USER32_CALLBACK_CLIENTTHREADSTARTUP */
    CbClientLoadLibrary,    /* USER32_CALLBACK_CLIENTLOADLIBRARY */
    CbGetCharsetInfo,       /* USER32_CALLBACK_GETCHARSETINFO */
    CbCopyImage,            /* USER32_CALLBACK_COPYIMAGE */
    CbSetWndIcons,          /* USER32_CALLBACK_SETWNDICONS */
    CbDeliverUserApc,       /* USER32_CALLBACK_DELIVERUSERAPC */
    CbNotSupported,         /* USER32_CALLBACK_DDEPOST */
    CbNotSupported,         /* USER32_CALLBACK_DDEGET */
    CbSetObm,               /* USER32_CALLBACK_SETOBM */
    CbLpk,                  /* USER32_CALLBACK_LPK */
    CbNotSupported,         /* USER32_CALLBACK_UMPD */
    CbImmProcessKey,        /* USER32_CALLBACK_IMMPROCESSKEY */
    CbImmLoadLayout,        /* USER32_CALLBACK_IMMLOADLAYOUT */
};
C_ASSERT(USER32_CALLBACK_COUNT == 20);
