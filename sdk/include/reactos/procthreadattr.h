/*
 * PROJECT:     ReactOS Win32 base
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     The layout of a process/thread attribute list (STARTUPINFOEX),
 *              shared by kernel32 (which fills it) and CreateProcess callers
 */

#pragma once

typedef struct _PROC_THREAD_ATTRIBUTE_ENTRY
{
    DWORD_PTR Attribute;
    SIZE_T Size;
    PVOID Value;
} PROC_THREAD_ATTRIBUTE_ENTRY;

typedef struct _PROC_THREAD_ATTRIBUTE_LIST_INTERNAL
{
    DWORD Flags;    /* attributes present, one bit per attribute number */
    ULONG Size;     /* number of entries allocated */
    ULONG Count;    /* number of entries used */
    ULONG Reserved;
    PROC_THREAD_ATTRIBUTE_ENTRY Entries[ANYSIZE_ARRAY];
} PROC_THREAD_ATTRIBUTE_LIST_INTERNAL, *PPROC_THREAD_ATTRIBUTE_LIST_INTERNAL;

#ifndef EXTENDED_STARTUPINFO_PRESENT
#define EXTENDED_STARTUPINFO_PRESENT 0x00080000
#endif

#ifndef PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES
#define PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES 0x00020009
#endif

/* The value of an attribute in a list, or NULL */
static __inline
PVOID
BaseGetProcThreadAttribute(
    _In_opt_ PVOID AttributeList,
    _In_ DWORD_PTR Attribute,
    _In_ SIZE_T MinimumSize)
{
    PPROC_THREAD_ATTRIBUTE_LIST_INTERNAL List = (PPROC_THREAD_ATTRIBUTE_LIST_INTERNAL)AttributeList;
    ULONG i;

    if (!List)
        return NULL;
    for (i = 0; i < List->Count && i < List->Size; i++)
    {
        if (List->Entries[i].Attribute == Attribute && List->Entries[i].Size >= MinimumSize)
            return List->Entries[i].Value;
    }
    return NULL;
}

/* SECURITY_CAPABILITIES (winnt.h, Windows 8) */
typedef struct _BASE_SECURITY_CAPABILITIES
{
    PSID AppContainerSid;
    PSID_AND_ATTRIBUTES Capabilities;
    DWORD CapabilityCount;
    DWORD Reserved;
} BASE_SECURITY_CAPABILITIES, *PBASE_SECURITY_CAPABILITIES;
