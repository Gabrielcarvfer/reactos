/*
 * PROJECT:     ReactOS win32 subsystem
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Fields of the structures that user32, gdi32 and imm32 share
 *              with win32k (the desktop heap, the shared handle tables, the
 *              server information, the client information in the TEB...)
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

#pragma once

/*
 * The 32-bit user32, gdi32 and imm32 of 64-bit ReactOS (SysWOW64, built with
 * BUILD_WOW6432) read these structures as the 64-bit kernel lays them out:
 * there, each pointer, handle and pointer-sized integer takes 8 bytes.
 *
 * KPTR(Type, Name) declares such a field. The code uses it under its name, as
 * a Type: that is its low 4 bytes, which is the whole value. The user-mode
 * addresses that the kernel gives a 32-bit process are below 2 GB, handles
 * are small numbers, and kernel addresses are only used to compute offsets
 * in the desktop heap (which also works on their low 32 bits). The memory
 * comes zeroed from the kernel: writing the low 4 bytes writes the value.
 *
 * Everywhere else, KPTR(Type, Name) is just "Type Name".
 */
/* Where win32k keeps its per-thread and per-process data in the 64-bit TEB
 * and PEB, which come right before the 32-bit ones (checked in 64-bit builds) */
#define KTEB64_OFFSET                     0x2000
#define KPEB64_OFFSET                     0x1000
#define KTEB64_WIN32THREADINFO            0x078
#define KTEB64_WIN32CLIENTINFO            0x800
#define KPEB64_GDISHAREDHANDLETABLE       0x0F8
#define KPEB64_GDIDCATTRIBUTELIST         0x108

/* (checked against the 64-bit TEB and PEB in wow64win) */

/* The 64-bit layout: in the user-mode files of the SysWOW64 build only (that
 * build also compiles a 32-bit win32k, which keeps its own layout) */
#if defined(BUILD_WOW6432) && !defined(_WIN32K_)
#define KPTR_64BIT_LAYOUT
#endif

#ifdef KPTR_64BIT_LAYOUT

#define KTEB64_FIELD(Type, Offset) (*(Type *)((PBYTE)NtCurrentTeb() - KTEB64_OFFSET + (Offset)))
#define KPEB64_FIELD(Type, Offset) (*(Type *)((PBYTE)NtCurrentTeb()->ProcessEnvironmentBlock - KPEB64_OFFSET + (Offset)))

#define KPTR(Type, Name) union { Type Name; ULONG64 Name##_k64; }

typedef struct _KLIST_ENTRY
{
    KPTR(struct _KLIST_ENTRY *, Flink);
    KPTR(struct _KLIST_ENTRY *, Blink);
} KLIST_ENTRY, *PKLIST_ENTRY;

typedef struct _KUNICODE_STRING
{
    USHORT Length;
    USHORT MaximumLength;
    KPTR(PWSTR, Buffer);
} KUNICODE_STRING, *PKUNICODE_STRING;

C_ASSERT(sizeof(KLIST_ENTRY) == 16);
C_ASSERT(sizeof(KUNICODE_STRING) == 16);

#else

#define KPTR(Type, Name) Type Name
typedef LIST_ENTRY KLIST_ENTRY, *PKLIST_ENTRY;
typedef UNICODE_STRING KUNICODE_STRING, *PKUNICODE_STRING;

#endif
