/*
 * PROJECT:     ReactOS WoW64
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     The registry view of 32-bit programs (Wow6432Node)
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * A 32-bit program has its own view of the registry (see
 * sdk/include/reactos/wow64regview.h); it can ask for the 64-bit view with
 * KEY_WOW64_64KEY. Setup fills the 32-bit view with a copy of the 64-bit one.
 *
 * The keys are recognized by their full path: the name of the root key
 * (NtQueryKey) followed by the name being opened or created.
 */

#include <stdarg.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "winnt.h"
#include "winternl.h"
#include "wow64_private.h"

#ifndef KEY_WOW64_64KEY
#define KEY_WOW64_64KEY 0x0100
#endif

#include <wow64regview.h>

/*
 * Before NtOpenKey/NtCreateKey for a 32-bit program: if it means another key,
 * point the object attributes to it (Buffer holds the new name).
 */
BOOL get_key_redirect(OBJECT_ATTRIBUTES *attr, ACCESS_MASK access, WCHAR *Buffer, SIZE_T BufferSize,
                      UNICODE_STRING *NewName)
{
    struct
    {
        KEY_NAME_INFORMATION Info;
        WCHAR Name[512];
    } Root;
    WCHAR *Path;
    SIZE_T Length = 0, NameLength;
    ULONG Size;
    BOOL bRedirected;

    if (!attr || (access & KEY_WOW64_64KEY))
        return FALSE;

    NameLength = attr->ObjectName ? attr->ObjectName->Length / sizeof(WCHAR) : 0;
    Path = RtlAllocateHeap(GetProcessHeap(), 0, (ARRAY_SIZE(Root.Name) + NameLength + 2) * sizeof(WCHAR));
    if (!Path)
        return FALSE;

    /* The full path: the root key, then the name */
    if (attr->RootDirectory)
    {
        if (NtQueryKey(attr->RootDirectory, KeyNameInformation, &Root, sizeof(Root) - sizeof(WCHAR), &Size))
        {
            RtlFreeHeap(GetProcessHeap(), 0, Path);
            return FALSE;
        }
        Length = Root.Info.NameLength / sizeof(WCHAR);
        memcpy(Path, Root.Info.Name, Length * sizeof(WCHAR));

        /* Windows remembers the view a key was opened in, and relative names
         * stay in it (checked on Windows 11 with a 32-bit program). Here the
         * path of the root tells it: a key that a 32-bit program sees
         * elsewhere (HKLM\SOFTWARE, HKLM\SOFTWARE\Vendor, Classes\CLSID) can
         * only have been opened with KEY_WOW64_64KEY, and keeps its subkeys
         * in the 64-bit view. (TeamViewer's installer creates
         * SOFTWARE\TVInstallTemp in the 64-bit view, then opens RestoreInfo
         * below that handle without the flag.) A key that is the same in
         * both views (HKLM\SOFTWARE\Classes, HKCR, shared keys) is taken as
         * opened without the flag: names below it are redirected (COM looks
         * up CLSID\{...} relative to HKCR). Only a shared key opened with
         * KEY_WOW64_64KEY differs from Windows, whose relative opens below it
         * stay in the 64-bit view. */
        if (GetKeyView32(Path, Length, Buffer, BufferSize))
        {
            RtlFreeHeap(GetProcessHeap(), 0, Path);
            return FALSE;
        }

        if (NameLength) Path[Length++] = L'\\';
    }
    if (NameLength)
    {
        const WCHAR *Name = attr->ObjectName->Buffer;

        /* (no leading backslash after the root) */
        if (Length && Name[0] == L'\\') { Name++; NameLength--; }
        memcpy(Path + Length, Name, NameLength * sizeof(WCHAR));
        Length += NameLength;
    }
    /* (and none at the end) */
    while (Length && Path[Length - 1] == L'\\') Length--;
    Path[Length] = 0;

    bRedirected = GetKeyView32(Path, Length, Buffer, BufferSize);
    RtlFreeHeap(GetProcessHeap(), 0, Path);
    if (!bRedirected)
        return FALSE;

    RtlInitUnicodeString(NewName, Buffer);
    attr->RootDirectory = NULL;
    attr->ObjectName = NewName;
    return TRUE;
}
