/*
 * PROJECT:     ReactOS WoW64
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     wow64win.dll: the win32k system calls of 32-bit programs.
 *              The thunks are generated (win32k_thunks.c); the ones here
 *              are those their prototypes do not describe.
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

#include "wow64win_private.h"

/* Where the 32-bit user32/gdi32 look for win32k's data in the 64-bit TEB and
 * PEB (see win32ss/include/kptr.h) */
C_ASSERT(FIELD_OFFSET(TEB, Win32ThreadInfo) == KTEB64_WIN32THREADINFO);
C_ASSERT(FIELD_OFFSET(TEB, Win32ClientInfo) == KTEB64_WIN32CLIENTINFO);
C_ASSERT(FIELD_OFFSET(PEB, GdiSharedHandleTable) == KPEB64_GDISHAREDHANDLETABLE);
C_ASSERT(FIELD_OFFSET(PEB, GdiDCAttributeList) == KPEB64_GDIDCATTRIBUTELIST);

/* From win32k_thunks.c */
extern const ULONG_PTR wow64win_thunks[];
extern const BYTE wow64win_args[];
extern const char * const wow64win_names[];
extern const ULONG wow64win_count;

/* The table wow64.dll uses for the system calls 1000h and up (as in Wine) */
typedef struct
{
    ULONG_PTR *ServiceTable;
    ULONG_PTR *CounterTable;
    ULONG_PTR ServiceLimit;
    BYTE *ArgumentTable;
} SYSTEM_SERVICE_TABLE_W;

SYSTEM_SERVICE_TABLE_W sdwhwin32;

/* From callbacks.c */
extern PVOID Wow64winCallbacks[];

BOOL WINAPI DllMain( HINSTANCE inst, DWORD reason, void *reserved )
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        sdwhwin32.ServiceTable = (ULONG_PTR *)wow64win_thunks;
        sdwhwin32.ServiceLimit = wow64win_count;
        sdwhwin32.ArgumentTable = (BYTE *)wow64win_args;
        /* (Wine's wow64 does not use the counters: their place has the
         * names, for the debug output) */
        sdwhwin32.CounterTable = (ULONG_PTR *)wow64win_names;
        /* The callbacks of win32k go through the 64-bit ntdll, which takes
         * them from the 64-bit PEB: there, they reach callbacks.c */
        NtCurrentPeb()->KernelCallbackTable = Wow64winCallbacks;
    }
    return TRUE;
}

/* THE CALLS WRITTEN BY HAND ***********************************************/

#include "win32k_structs.h"

static UNICODE_STRING *ustr_32to64( UNICODE_STRING *str, const UNICODE_STRING_32 *str32 )
{
    if (!str32) return NULL;
    str->Length = str32->Length;
    str->MaximumLength = str32->MaximumLength;
    str->Buffer = ULongToPtr( str32->Buffer );
    return str;
}

/* CLSMENUNAME points to a UNICODE_STRING, which has the 32-bit layout too */
NTSTATUS WINAPI wow64_NtUserRegisterClassExWOW( UINT *args )
{
    WNDCLASSEXW_32 *wc32 = get_ptr( &args );
    UNICODE_STRING_32 *name32 = get_ptr( &args );
    UNICODE_STRING_32 *version32 = get_ptr( &args );
    CLSMENUNAME_32 *menu32 = get_ptr( &args );
    DWORD fnid = get_ulong( &args );
    DWORD flags = get_ulong( &args );
    DWORD *wow = get_ptr( &args );

    WNDCLASSEXW wc;
    UNICODE_STRING name, version, menu_name;
    CLSMENUNAME menu;

    if (wc32) WNDCLASSEXW_32to64( &wc, wc32 );
    if (menu32)
    {
        menu.pszClientAnsiMenuName = ULongToPtr( menu32->pszClientAnsiMenuName );
        menu.pwszClientUnicodeMenuName = ULongToPtr( menu32->pwszClientUnicodeMenuName );
        menu.pusMenuName = ustr_32to64( &menu_name, ULongToPtr( menu32->pusMenuName ));
    }
    return (NTSTATUS)(ULONG_PTR)NtUserRegisterClassExWOW( wc32 ? &wc : NULL, ustr_32to64( &name, name32 ),
                                                          ustr_32to64( &version, version32 ),
                                                          menu32 ? &menu : NULL, fnid, flags, wow );
}

/* Arrays of handles */

static void handles_64to32( ULONG *dst, const HANDLE *src, ULONG count )
{
    ULONG i;
    for (i = 0; i < count; i++) dst[i] = HandleToULong( src[i] );
}

NTSTATUS WINAPI wow64_NtUserBuildHwndList( UINT *args )
{
    HDESK desktop = get_handle( &args );
    HWND parent = get_handle( &args );
    BOOLEAN children = get_ulong( &args );
    ULONG thread = get_ulong( &args );
    ULONG count = get_ulong( &args );
    ULONG *list32 = get_ptr( &args );
    ULONG *needed = get_ptr( &args );

    HWND *list = NULL;
    NTSTATUS status;

    if (list32 && count && !(list = RtlAllocateHeap( RtlGetProcessHeap(), 0, count * sizeof(HWND) )))
        return STATUS_NO_MEMORY;
    status = NtUserBuildHwndList( desktop, parent, children, thread, count, list, needed );
    if (list)
    {
        if (!status) handles_64to32( list32, list, min( count, needed ? *needed : count ));
        RtlFreeHeap( RtlGetProcessHeap(), 0, list );
    }
    return status;
}

NTSTATUS WINAPI wow64_NtUserBuildHimcList( UINT *args )
{
    ULONG thread = get_ulong( &args );
    ULONG count = get_ulong( &args );
    ULONG *list32 = get_ptr( &args );
    ULONG *needed = get_ptr( &args );

    HIMC *list = NULL;
    NTSTATUS status;

    if (list32 && count && !(list = RtlAllocateHeap( RtlGetProcessHeap(), 0, count * sizeof(HIMC) )))
        return STATUS_NO_MEMORY;
    status = NtUserBuildHimcList( thread, count, list, needed );
    if (list)
    {
        if (!status) handles_64to32( list32, (HANDLE *)list, min( count, needed ? *needed : count ));
        RtlFreeHeap( RtlGetProcessHeap(), 0, list );
    }
    return status;
}

NTSTATUS WINAPI wow64_NtUserEnumDisplayMonitors( UINT *args )
{
    HDC hdc = get_handle( &args );
    const RECT *rect = get_ptr( &args );
    ULONG *monitors32 = get_ptr( &args );
    RECT *rects = get_ptr( &args );
    DWORD size = get_ulong( &args );

    HMONITOR *monitors = NULL;
    INT ret;

    if (monitors32 && size && !(monitors = RtlAllocateHeap( RtlGetProcessHeap(), 0, size * sizeof(HMONITOR) )))
        return -1;
    ret = NtUserEnumDisplayMonitors( hdc, rect, monitors, rects, size );
    if (monitors)
    {
        if (ret > 0) handles_64to32( monitors32, (HANDLE *)monitors, min( (ULONG)ret, size ));
        RtlFreeHeap( RtlGetProcessHeap(), 0, monitors );
    }
    return ret;
}

NTSTATUS WINAPI wow64_NtUserGetKeyboardLayoutList( UINT *args )
{
    ULONG count = get_ulong( &args );
    ULONG *list32 = get_ptr( &args );

    HKL *list = NULL;
    UINT ret;

    if (list32 && count && !(list = RtlAllocateHeap( RtlGetProcessHeap(), 0, count * sizeof(HKL) )))
        return 0;
    ret = NtUserGetKeyboardLayoutList( count, list );
    if (list)
    {
        handles_64to32( list32, (HANDLE *)list, min( ret, count ));
        RtlFreeHeap( RtlGetProcessHeap(), 0, list );
    }
    return ret;
}

/* NtUserMessageCall: what lParam points to depends on the message, and
 * what ResultInfo is depends on the kind of call */

/* The data of a message, at lParam, in 64-bit layout (64-bit pointers
 * inside). Returns the new lParam. */
struct msg_data
{
    union
    {
        CREATESTRUCTW cs;
        struct
        {
            NCCALCSIZE_PARAMS params;
            WINDOWPOS pos;
        } nccalc;
        WINDOWPOS pos;
        MEASUREITEMSTRUCT mis;
        DRAWITEMSTRUCT dis;
        HELPINFO hi;
        MDINEXTMENU mnm;
        COPYDATASTRUCT cds;
    };
};

static LPARAM msg_lparam_32to64( UINT msg, WPARAM wparam, LPARAM lparam, struct msg_data *data )
{
    void *ptr = (void *)lparam;

    if (!lparam) return lparam;
    switch (msg)
    {
    case WM_CREATE:
    case WM_NCCREATE:
        CREATESTRUCTW_32to64( &data->cs, ptr );
        return (LPARAM)&data->cs;
    case WM_NCCALCSIZE:
        if (!wparam) return lparam;
        NCCALCSIZE_PARAMS_32to64( &data->nccalc.params, ptr );
        if (((NCCALCSIZE_PARAMS_32 *)ptr)->lppos)
            WINDOWPOS_32to64( &data->nccalc.pos, ULongToPtr( ((NCCALCSIZE_PARAMS_32 *)ptr)->lppos ));
        data->nccalc.params.lppos = &data->nccalc.pos;
        return (LPARAM)&data->nccalc.params;
    case WM_WINDOWPOSCHANGING:
    case WM_WINDOWPOSCHANGED:
        WINDOWPOS_32to64( &data->pos, ptr );
        return (LPARAM)&data->pos;
    case WM_MEASUREITEM:
        MEASUREITEMSTRUCT_32to64( &data->mis, ptr );
        return (LPARAM)&data->mis;
    case WM_DRAWITEM:
        DRAWITEMSTRUCT_32to64( &data->dis, ptr );
        return (LPARAM)&data->dis;
    case WM_HELP:
        HELPINFO_32to64( &data->hi, ptr );
        return (LPARAM)&data->hi;
    case WM_NEXTMENU:
        MDINEXTMENU_32to64( &data->mnm, ptr );
        return (LPARAM)&data->mnm;
    case WM_COPYDATA:
        COPYDATASTRUCT_32to64( &data->cds, ptr );
        return (LPARAM)&data->cds;
    }
    return lparam;
}

/* And back, for what the kernel may have changed */
static void msg_lparam_64to32( UINT msg, WPARAM wparam, LPARAM lparam32, const struct msg_data *data )
{
    void *ptr = (void *)lparam32;

    if (!lparam32) return;
    switch (msg)
    {
    case WM_NCCALCSIZE:
        if (!wparam) return;
        memcpy( ((NCCALCSIZE_PARAMS_32 *)ptr)->rgrc, data->nccalc.params.rgrc, sizeof(data->nccalc.params.rgrc) );
        if (((NCCALCSIZE_PARAMS_32 *)ptr)->lppos)
            WINDOWPOS_64to32( ULongToPtr( ((NCCALCSIZE_PARAMS_32 *)ptr)->lppos ), &data->nccalc.pos );
        return;
    case WM_WINDOWPOSCHANGING:
    case WM_WINDOWPOSCHANGED:
        WINDOWPOS_64to32( ptr, &data->pos );
        return;
    case WM_MEASUREITEM:
        MEASUREITEMSTRUCT_64to32( ptr, &data->mis );
        return;
    case WM_DRAWITEM:
        DRAWITEMSTRUCT_64to32( ptr, &data->dis );
        return;
    case WM_NEXTMENU:
        MDINEXTMENU_64to32( ptr, &data->mnm );
        return;
    }
}

NTSTATUS WINAPI wow64_NtUserMessageCall( UINT *args )
{
    HWND hwnd = get_handle( &args );
    UINT msg = get_ulong( &args );
    WPARAM wparam = get_ulong( &args );
    LPARAM lparam32 = (LONG)get_ulong( &args );
    ULONG result_info32 = get_ulong( &args );
    DWORD type = get_ulong( &args );
    BOOL ansi = get_ulong( &args );

    struct msg_data data;
    LPARAM lparam = msg_lparam_32to64( msg, wparam, lparam32, &data );
    ULONG_PTR result_info = result_info32;
    LRESULT result = 0;
    DOSENDMESSAGE dsm;
    BROADCASTPARM bp;
    CALL_BACK_INFO cbi;
    BOOL ret;

    switch (type)
    {
    case FNID_DEFWINDOWPROC:
    case FNID_CALLWNDPROC:
    case FNID_CALLWNDPROCRET:
    case FNID_SCROLLBAR:
    case FNID_DESKTOP:
    case FNID_MENU:
    case FNID_SENDMESSAGE:
        /* a pointer to an LRESULT */
        if (result_info32) result_info = (ULONG_PTR)&result;
        break;
    case FNID_SENDMESSAGEFF:
    case FNID_SENDMESSAGEWTOOPTION:
        if (result_info32)
        {
            DOSENDMESSAGE_32to64( &dsm, ULongToPtr( result_info32 ));
            result_info = (ULONG_PTR)&dsm;
        }
        break;
    case FNID_BROADCASTSYSTEMMESSAGE:
        if (result_info32)
        {
            BROADCASTPARM_32to64( &bp, ULongToPtr( result_info32 ));
            result_info = (ULONG_PTR)&bp;
        }
        break;
    case FNID_SENDMESSAGECALLBACK:
        if (result_info32)
        {
            CALL_BACK_INFO_32to64( &cbi, ULongToPtr( result_info32 ));
            result_info = (ULONG_PTR)&cbi;
        }
        break;
    }

    ret = NtUserMessageCall( hwnd, msg, wparam, lparam, result_info, type, ansi );

    msg_lparam_64to32( msg, wparam, lparam32, &data );
    if (result_info32)
    {
        if (result_info == (ULONG_PTR)&result)
            *(LONG *)ULongToPtr( result_info32 ) = (LONG)result;
        else if (result_info == (ULONG_PTR)&dsm)
            DOSENDMESSAGE_64to32( ULongToPtr( result_info32 ), &dsm );
        else if (result_info == (ULONG_PTR)&bp)
            BROADCASTPARM_64to32( ULongToPtr( result_info32 ), &bp );
    }
    return ret;
}

/* GetObject: the bitmap (BITMAP, DIBSECTION), brush (LOGBRUSH) and pen
 * (EXTLOGPEN) structures have pointer-sized fields */
typedef struct
{
    LONG bmType;
    LONG bmWidth;
    LONG bmHeight;
    LONG bmWidthBytes;
    WORD bmPlanes;
    WORD bmBitsPixel;
    ULONG bmBits;
} BITMAP32;

typedef struct
{
    BITMAP32 dsBm;
    BITMAPINFOHEADER dsBmih;
    DWORD dsBitfields[3];
    ULONG dshSection;
    DWORD dsOffset;
} DIBSECTION32;

typedef struct
{
    DWORD elpPenStyle;
    DWORD elpWidth;
    UINT elpBrushStyle;
    COLORREF elpColor;
    ULONG elpHatch;
    DWORD elpNumEntries;
    DWORD elpStyleEntry[1];
} EXTLOGPEN32;

C_ASSERT(sizeof(BITMAP32) == 24);
C_ASSERT(sizeof(DIBSECTION32) == 84);
C_ASSERT(sizeof(LOGBRUSH32) == 12);
C_ASSERT(sizeof(EXTLOGPEN32) == 28);

static void bitmap_64to32( BITMAP32 *bm32, const BITMAP *bm )
{
    bm32->bmType = bm->bmType;
    bm32->bmWidth = bm->bmWidth;
    bm32->bmHeight = bm->bmHeight;
    bm32->bmWidthBytes = bm->bmWidthBytes;
    bm32->bmPlanes = bm->bmPlanes;
    bm32->bmBitsPixel = bm->bmBitsPixel;
    bm32->bmBits = PtrToUlong( bm->bmBits );
}

NTSTATUS WINAPI wow64_NtGdiExtGetObjectW( UINT *args )
{
    HANDLE handle = get_handle( &args );
    INT count32 = get_ulong( &args );
    void *buffer32 = get_ptr( &args );

    union
    {
        BITMAP bm;
        DIBSECTION ds;
        LOGBRUSH lb;
        EXTLOGPEN elp;
        BYTE bytes[1];
    } *buffer, local;
    INT count, ret;

    switch (GDI_HANDLE_GET_TYPE( handle ))
    {
    case GDI_OBJECT_TYPE_BITMAP:
        if (!buffer32)
        {
            ret = NtGdiExtGetObjectW( handle, 0, NULL );
            return ret == sizeof(DIBSECTION) ? sizeof(DIBSECTION32) :
                   ret == sizeof(BITMAP) ? sizeof(BITMAP32) : ret;
        }
        if (count32 < (INT)sizeof(BITMAP32)) return 0;
        count = count32 >= (INT)sizeof(DIBSECTION32) ? sizeof(DIBSECTION) : sizeof(BITMAP);
        ret = NtGdiExtGetObjectW( handle, count, &local );
        if (ret == sizeof(DIBSECTION))
        {
            DIBSECTION32 *ds32 = buffer32;

            bitmap_64to32( &ds32->dsBm, &local.ds.dsBm );
            ds32->dsBmih = local.ds.dsBmih;
            memcpy( ds32->dsBitfields, local.ds.dsBitfields, sizeof(ds32->dsBitfields) );
            ds32->dshSection = HandleToULong( local.ds.dshSection );
            ds32->dsOffset = local.ds.dsOffset;
            return sizeof(DIBSECTION32);
        }
        if (ret == sizeof(BITMAP))
        {
            bitmap_64to32( buffer32, &local.bm );
            return sizeof(BITMAP32);
        }
        return ret;

    case GDI_OBJECT_TYPE_BRUSH:
        if (!buffer32)
        {
            ret = NtGdiExtGetObjectW( handle, 0, NULL );
            return ret == sizeof(LOGBRUSH) ? sizeof(LOGBRUSH32) : ret;
        }
        if (count32 < (INT)sizeof(LOGBRUSH32)) return 0;
        ret = NtGdiExtGetObjectW( handle, sizeof(LOGBRUSH), &local );
        if (ret == sizeof(LOGBRUSH))
        {
            LOGBRUSH32 *lb32 = buffer32;

            lb32->lbStyle = local.lb.lbStyle;
            lb32->lbColor = local.lb.lbColor;
            lb32->lbHatch = (ULONG)local.lb.lbHatch;
            return sizeof(LOGBRUSH32);
        }
        return ret;

    case GDI_OBJECT_TYPE_PEN:
    case GDI_OBJECT_TYPE_EXTPEN:
    {
        /* a LOGPEN (same in both), or an EXTLOGPEN with its style entries */
        const INT diff = FIELD_OFFSET(EXTLOGPEN, elpStyleEntry) - FIELD_OFFSET(EXTLOGPEN32, elpStyleEntry);

        if (!buffer32)
        {
            ret = NtGdiExtGetObjectW( handle, 0, NULL );
            return ret >= FIELD_OFFSET(EXTLOGPEN, elpStyleEntry) ? ret - diff : ret;
        }
        count = count32 == sizeof(EXTLOGPEN32) ? sizeof(EXTLOGPEN) :
                count32 > (INT)sizeof(LOGPEN) ? count32 + diff : count32;
        if (count <= (INT)sizeof(local)) buffer = &local;
        else if (!(buffer = RtlAllocateHeap( RtlGetProcessHeap(), 0, count ))) return 0;
        ret = NtGdiExtGetObjectW( handle, count, buffer );
        if (ret >= FIELD_OFFSET(EXTLOGPEN, elpStyleEntry))
        {
            EXTLOGPEN32 *elp32 = buffer32;

            elp32->elpPenStyle = buffer->elp.elpPenStyle;
            elp32->elpWidth = buffer->elp.elpWidth;
            elp32->elpBrushStyle = buffer->elp.elpBrushStyle;
            elp32->elpColor = buffer->elp.elpColor;
            elp32->elpHatch = (ULONG)buffer->elp.elpHatch;
            elp32->elpNumEntries = buffer->elp.elpNumEntries;
            memcpy( elp32->elpStyleEntry, buffer->elp.elpStyleEntry,
                    ret - FIELD_OFFSET(EXTLOGPEN, elpStyleEntry) );
            ret = ret == sizeof(EXTLOGPEN) ? sizeof(EXTLOGPEN32) : ret - diff;
        }
        else if (ret > 0) memcpy( buffer32, buffer, ret );
        if (buffer != &local) RtlFreeHeap( RtlGetProcessHeap(), 0, buffer );
        return ret;
    }

    default:
        /* LOGPEN, LOGFONTW, LOGPALETTE...: the same in both */
        return NtGdiExtGetObjectW( handle, count32, buffer32 );
    }
}

/* GetOutlineTextMetrics: the structure is followed by its strings (family,
 * face, style and full names) and its four "pointers" are their offsets from
 * the start of the structure. The 64-bit structure is larger, so the strings
 * move and the offsets shrink by the difference. */
NTSTATUS WINAPI wow64_NtGdiGetOutlineTextMetricsInternalW( UINT *args )
{
    HDC hdc = get_handle( &args );
    ULONG size32 = get_ulong( &args );
    OUTLINETEXTMETRICW_32 *otm32 = get_ptr( &args );
    TMDIFF *tmdiff = get_ptr( &args );

    const ULONG diff = sizeof(OUTLINETEXTMETRICW) - sizeof(OUTLINETEXTMETRICW_32);
    OUTLINETEXTMETRICW *otm;
    ULONG ret, size;

    if (!otm32 || !size32)
    {
        ret = NtGdiGetOutlineTextMetricsInternalW( hdc, 0, NULL, tmdiff );
        return ret ? ret - diff : 0;
    }
    size = size32 + diff;
    if (!(otm = RtlAllocateHeap( RtlGetProcessHeap(), HEAP_ZERO_MEMORY, size ))) return 0;
    ret = NtGdiGetOutlineTextMetricsInternalW( hdc, size, otm, tmdiff );
    if (ret)
    {
        OUTLINETEXTMETRICW_64to32( otm32, otm );
        otm32->otmSize = otm->otmSize - diff;
#define SHIFT(field) if (otm->field) otm32->field = (ULONG)((ULONG_PTR)otm->field - diff)
        SHIFT(otmpFamilyName);
        SHIFT(otmpFaceName);
        SHIFT(otmpStyleName);
        SHIFT(otmpFullName);
#undef SHIFT
        if (min( ret, size ) > sizeof(*otm))
            memcpy( otm32 + 1, otm + 1, min( ret, size ) - sizeof(*otm) );
        ret -= diff;
    }
    RtlFreeHeap( RtlGetProcessHeap(), 0, otm );
    return ret;
}

/* SetWindowLong: on 32-bit it is also SetWindowLongPtr. The 64-bit kernel
 * only takes 4-byte values for the 4-byte fields; the window procedure,
 * instance and parent go through NtUserSetWindowLongPtr. */
NTSTATUS WINAPI wow64_NtUserSetWindowLong( UINT *args )
{
    HWND hwnd = get_handle( &args );
    INT index = get_ulong( &args );
    LONG value = get_ulong( &args );
    BOOL ansi = get_ulong( &args );

    switch (index)
    {
    case GWLP_WNDPROC:
    case GWLP_HINSTANCE:
        return (ULONG)NtUserSetWindowLongPtr( hwnd, index, (ULONG)value, ansi );
    case GWLP_HWNDPARENT:
        return HandleToLong( (HANDLE)NtUserSetWindowLongPtr( hwnd, index, (LONG_PTR)value, ansi ) );
    default:
        return NtUserSetWindowLong( hwnd, index, value, ansi );
    }
}

/* FlashWindowEx: FLASHWINFO has a window handle (and its size in cbSize) */
NTSTATUS WINAPI wow64_NtUserFlashWindowEx( UINT *args )
{
    struct
    {
        UINT cbSize;
        ULONG hwnd;
        DWORD dwFlags;
        UINT uCount;
        DWORD dwTimeout;
    } *info32 = get_ptr( &args );
    FLASHWINFO info;

    if (!info32 || info32->cbSize != sizeof(*info32))
        return NtUserFlashWindowEx( NULL );
    info.cbSize = sizeof(info);
    info.hwnd = LongToHandle( info32->hwnd );
    info.dwFlags = info32->dwFlags;
    info.uCount = info32->uCount;
    info.dwTimeout = info32->dwTimeout;
    return NtUserFlashWindowEx( &info );
}

/* GetRealizationInfo: REALIZATION_INFO has no pointers */
BOOL APIENTRY NtGdiGetRealizationInfo( HDC hdc, PREALIZATION_INFO info, HFONT font );

NTSTATUS WINAPI wow64_NtGdiGetRealizationInfo( UINT *args )
{
    HDC hdc = get_handle( &args );
    PREALIZATION_INFO info = get_ptr( &args );
    HFONT font = get_handle( &args );

    return NtGdiGetRealizationInfo( hdc, info, font );
}
