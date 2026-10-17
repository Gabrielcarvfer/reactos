/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for HeapCreate(HEAP_CREATE_ENABLE_EXECUTE)
 *
 * Wine Mono puts the code of dynamic methods (e.g. the wrapper that a
 * delegate becomes when WinForms passes it as a window procedure) into a heap
 * made with HeapCreate(HEAP_CREATE_ENABLE_EXECUTE, 0, 0). The memory of such
 * a heap must be executable even when the CPU enforces no-execute (NX).
 */

#include "precomp.h"

static volatile LONG Faulted;

/* On a fault at the start of the called code, "return" to the caller with -1 */
static LONG CALLBACK
ExecFaultHandler(PEXCEPTION_POINTERS Info)
{
    PEXCEPTION_RECORD Rec = Info->ExceptionRecord;
    PCONTEXT Ctx = Info->ContextRecord;

    if (Rec->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || Rec->NumberParameters < 2)
        return EXCEPTION_CONTINUE_SEARCH;
#ifdef _WIN64
    if (Ctx->Rip != (ULONG_PTR)Rec->ExceptionAddress)
        return EXCEPTION_CONTINUE_SEARCH;
    Faulted = (LONG)Rec->ExceptionInformation[0] + 100;
    Ctx->Rip = *(DWORD64 *)Ctx->Rsp;
    Ctx->Rsp += 8;
    Ctx->Rax = (DWORD64)-1;
#elif defined(_M_IX86)
    if (Ctx->Eip != (ULONG_PTR)Rec->ExceptionAddress)
        return EXCEPTION_CONTINUE_SEARCH;
    Faulted = (LONG)Rec->ExceptionInformation[0] + 100;
    Ctx->Eip = *(DWORD *)Ctx->Esp;
    Ctx->Esp += 4;
    Ctx->Eax = (DWORD)-1;
#else
    return EXCEPTION_CONTINUE_SEARCH;
#endif
    return EXCEPTION_CONTINUE_EXECUTION;
}

/* mov eax, 42; ret (the same bytes on x86 and x64) */
static const BYTE ReturnFortyTwo[] = { 0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3 };

static void
TestExecute(const char *What, PBYTE Mem)
{
    MEMORY_BASIC_INFORMATION Mbi;
    int (*Func)(void) = (int (*)(void))Mem;
    int Ret;

    ok(Mem != NULL, "%s: allocation failed (%lu)\n", What, GetLastError());
    if (!Mem)
        return;

    memcpy(Mem, ReturnFortyTwo, sizeof(ReturnFortyTwo));
    FlushInstructionCache(GetCurrentProcess(), Mem, sizeof(ReturnFortyTwo));

    ZeroMemory(&Mbi, sizeof(Mbi));
    ok(VirtualQuery(Mem, &Mbi, sizeof(Mbi)) != 0, "%s: VirtualQuery failed\n", What);
    ok(Mbi.Protect == PAGE_EXECUTE_READWRITE, "%s: protection 0x%lx, expected 0x40\n", What, Mbi.Protect);

    Faulted = 0;
    Ret = Func();
    ok(Ret == 42, "%s: code returned %d, expected 42\n", What, Ret);
    ok(Faulted == 0, "%s: executing the code faulted (access type %ld)\n", What, Faulted - 100);
}

START_TEST(HeapCreateExecute)
{
    MEMORY_BASIC_INFORMATION Mbi;
    HANDLE Heap, FixedHeap, NormalHeap;
    PVOID Handler;
    PBYTE Mem, Last = NULL;
    int i;

    Handler = AddVectoredExceptionHandler(1, ExecFaultHandler);
    ok(Handler != NULL, "AddVectoredExceptionHandler failed\n");

    /* What Mono's code manager does */
    Heap = HeapCreate(HEAP_CREATE_ENABLE_EXECUTE, 0, 0);
    ok(Heap != NULL, "HeapCreate failed (%lu)\n", GetLastError());
    if (Heap)
    {
        TestExecute("first block", HeapAlloc(Heap, 0, 0x200));

        /* Grow the heap: commit more pages and add new segments */
        for (i = 0; i < 1200; i++)
            Last = HeapAlloc(Heap, 0, 0x1000);
        TestExecute("block after growing", Last);

        /* A big block is allocated directly from virtual memory */
        TestExecute("large block", HeapAlloc(Heap, 0, 2 * 1024 * 1024));

        HeapDestroy(Heap);
    }

    /* A non-growable heap */
    FixedHeap = HeapCreate(HEAP_CREATE_ENABLE_EXECUTE, 0x10000, 0x100000);
    ok(FixedHeap != NULL, "HeapCreate failed (%lu)\n", GetLastError());
    if (FixedHeap)
    {
        TestExecute("fixed-size heap", HeapAlloc(FixedHeap, 0, 0x100));
        HeapDestroy(FixedHeap);
    }

    /* A normal heap stays non-executable */
    NormalHeap = HeapCreate(0, 0, 0);
    ok(NormalHeap != NULL, "HeapCreate failed (%lu)\n", GetLastError());
    if (NormalHeap)
    {
        Mem = HeapAlloc(NormalHeap, 0, 0x100);
        ZeroMemory(&Mbi, sizeof(Mbi));
        VirtualQuery(Mem, &Mbi, sizeof(Mbi));
        ok(Mbi.Protect == PAGE_READWRITE, "normal heap: protection 0x%lx, expected 0x4\n", Mbi.Protect);
        HeapDestroy(NormalHeap);
    }

    if (Handler)
        RemoveVectoredExceptionHandler(Handler);
}
