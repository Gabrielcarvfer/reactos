/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Kernel memory leak sanitizer (KLSAN): the control interface
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * Kernels built with ENABLE_KLSAN and booted with the option /KLSAN track
 * every pool block. A scan looks for blocks that nothing points to any more
 * (see ntoskrnl/klsan/klsan.c). User mode asks for one with
 * NtSystemDebugControl(SYSDBG_KLSAN_CONTROL, &Control, sizeof(Control),
 * Buffer, BufferSize, &Length) (needs SeDebugPrivilege); kernel mode with the
 * exported KlsanControl (MmGetSystemRoutineAddress(L"KlsanControl")).
 */

#pragma once

/* (a ReactOS-only SYSDBG_COMMAND) */
#define SYSDBG_KLSAN_CONTROL        0x4B4C

#define KLSAN_CMD_SCAN              1   /* scan, write the report (text) to the output buffer */
#define KLSAN_CMD_BASELINE          2   /* blocks allocated until now are never reported */
#define KLSAN_CMD_QUERY             3   /* the state of the block at Address (output: ULONG) */
#define KLSAN_CMD_STATS             4   /* counters (output: KLSAN_STATS) */
#define KLSAN_CMD_MARK              5   /* remember how much each allocation stack has alive now */
#define KLSAN_CMD_GROWTH            6   /* the allocation stacks that grew since the mark (text) */

/* KLSAN_CMD_QUERY results */
#define KLSAN_BLOCK_UNTRACKED       0   /* not a tracked pool block (or freed) */
#define KLSAN_BLOCK_REFERENCED      1   /* something pointed to it in the last scan */
#define KLSAN_BLOCK_UNREFERENCED    2   /* nothing pointed to it in the last scan */
#define KLSAN_BLOCK_NEW             3   /* allocated after the last scan started */

typedef struct _KLSAN_CONTROL
{
    ULONG Command;
    ULONG MinAgeSeconds;    /* scan: report blocks at least this old (0: default, 30 s) */
    ULONG MinScans;         /* scan: ...unreferenced in this many scans in a row (0: default, 2) */
    ULONG MaxStacks;        /* scan, growth: how many allocation stacks to report (0: default, 40) */
    ULONG Flags;            /* scan: KLSAN_SCAN_* */
    ULONG Reserved;
    PVOID Address;          /* query: the block */
} KLSAN_CONTROL, *PKLSAN_CONTROL;

#define KLSAN_SCAN_QUIET            0x1 /* no report on the debug log */

typedef struct _KLSAN_STATS
{
    ULONGLONG TrackedBlocks;
    ULONGLONG TrackedBytes;
    ULONGLONG Allocations;
    ULONGLONG Frees;
    ULONGLONG UntrackedAllocations;    /* no memory for the tracking record */
    ULONGLONG Stacks;
    ULONGLONG Scans;
    ULONGLONG LastScanMs;
    ULONGLONG LastScanBytes;           /* memory read by the last scan */
    ULONGLONG LastLeakBlocks;
    ULONGLONG LastLeakBytes;
    ULONGLONG DeferredFrees;           /* frees that waited for the end of a scan */
} KLSAN_STATS, *PKLSAN_STATS;

#ifndef KLSAN_NO_PROTOTYPES
typedef NTSTATUS
(NTAPI *PKLSAN_CONTROL_ROUTINE)(
    _In_ PKLSAN_CONTROL Control,
    _Out_writes_bytes_opt_(OutputLength) PVOID Output,
    _In_ ULONG OutputLength,
    _Out_opt_ PULONG ReturnLength);
#endif
