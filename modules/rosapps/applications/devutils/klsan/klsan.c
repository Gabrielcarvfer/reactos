/*
 * PROJECT:     ReactOS development utilities
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Control the kernel memory leak sanitizer (KLSAN)
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * For kernels built with ENABLE_KLSAN and booted with /KLSAN:
 *   klsan scan [MINAGE_S [MINSCANS [MAXSTACKS]]]   leak report (also on the debug log)
 *   klsan baseline                                 never report what is allocated now
 *   klsan mark                                     remember what each allocation stack has alive
 *   klsan growth [MAXSTACKS]                       the allocation stacks that grew since the mark
 *   klsan stats
 *   klsan dbgcat FILE                              copy a text file to the debug log
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#define NTOS_MODE_USER
#include <ndk/rtlfuncs.h>
#include <ndk/kdfuncs.h>
#include <ndk/setypes.h>
#include <reactos/klsan.h>

#define REPORT_SIZE (4 * 1024 * 1024)

static int
Text(ULONG Command, ULONG MinAge, ULONG MinScans, ULONG MaxStacks)
{
    KLSAN_CONTROL Control = { 0 };
    PCHAR Buffer = malloc(REPORT_SIZE);
    ULONG Length = 0;
    NTSTATUS Status;

    if (!Buffer)
        return 1;
    Control.Command = Command;
    Control.MinAgeSeconds = MinAge;
    Control.MinScans = MinScans;
    Control.MaxStacks = MaxStacks;
    Status = NtSystemDebugControl(SYSDBG_KLSAN_CONTROL, &Control, sizeof(Control), Buffer, REPORT_SIZE, &Length);
    if (!NT_SUCCESS(Status))
    {
        printf("KLSAN request failed: 0x%08lx (a KLSAN kernel booted with /KLSAN is needed)\n", Status);
        free(Buffer);
        return 1;
    }
    fwrite(Buffer, 1, Length ? Length - 1 : 0, stdout);
    free(Buffer);
    return 0;
}

static int
Simple(ULONG Command)
{
    KLSAN_CONTROL Control = { 0 };
    NTSTATUS Status;

    Control.Command = Command;
    Status = NtSystemDebugControl(SYSDBG_KLSAN_CONTROL, &Control, sizeof(Control), NULL, 0, NULL);
    printf("0x%08lx\n", Status);
    return NT_SUCCESS(Status) ? 0 : 1;
}

static int
Stats(VOID)
{
    KLSAN_CONTROL Control = { 0 };
    KLSAN_STATS s;
    NTSTATUS Status;

    Control.Command = KLSAN_CMD_STATS;
    Status = NtSystemDebugControl(SYSDBG_KLSAN_CONTROL, &Control, sizeof(Control), &s, sizeof(s), NULL);
    if (!NT_SUCCESS(Status))
    {
        printf("KLSAN request failed: 0x%08lx\n", Status);
        return 1;
    }
    printf("tracked %I64u blocks, %I64u bytes; %I64u allocations, %I64u frees, %I64u untracked, %I64u stacks\n"
           "scans %I64u, last %I64u ms, %I64u KB read, %I64u leaked blocks (%I64u bytes), %I64u deferred frees\n",
           s.TrackedBlocks, s.TrackedBytes, s.Allocations, s.Frees, s.UntrackedAllocations, s.Stacks,
           s.Scans, s.LastScanMs, s.LastScanBytes / 1024, s.LastLeakBlocks, s.LastLeakBytes, s.DeferredFrees);
    return 0;
}

static int
DbgCat(const char *Path)
{
    char Line[400];
    FILE *File = fopen(Path, "r");

    if (!File)
        return 1;
    while (fgets(Line, sizeof(Line), File))
        OutputDebugStringA(Line);
    fclose(File);
    return 0;
}

static ULONG
Arg(int argc, char **argv, int i)
{
    return (argc > i) ? strtoul(argv[i], NULL, 0) : 0;
}

int
main(int argc, char **argv)
{
    const char *Command = (argc > 1) ? argv[1] : "scan";
    BOOLEAN Old;

    RtlAdjustPrivilege(SE_DEBUG_PRIVILEGE, TRUE, FALSE, &Old);

    if (!strcmp(Command, "scan"))
        return Text(KLSAN_CMD_SCAN, Arg(argc, argv, 2), Arg(argc, argv, 3), Arg(argc, argv, 4));
    if (!strcmp(Command, "growth"))
        return Text(KLSAN_CMD_GROWTH, 0, 0, Arg(argc, argv, 2));
    if (!strcmp(Command, "baseline"))
        return Simple(KLSAN_CMD_BASELINE);
    if (!strcmp(Command, "mark"))
        return Simple(KLSAN_CMD_MARK);
    if (!strcmp(Command, "stats"))
        return Stats();
    if (!strcmp(Command, "dbgcat") && argc > 2)
        return DbgCat(argv[2]);

    printf("usage: klsan scan [minage [minscans [maxstacks]]] | baseline | mark | growth [maxstacks] | stats | dbgcat FILE\n");
    return 2;
}
