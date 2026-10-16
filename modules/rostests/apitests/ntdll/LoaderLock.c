/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test that the loader lock is not held outside of the loader
 *
 * Chromium's crash reporter CHECKs that it does not run under the loader lock
 * (PEB->LoaderLock). Once the process runs its own code, in the main thread,
 * in a new thread and after a LoadLibrary, no thread may still own it.
 */

#include "precomp.h"

static
VOID
CheckLoaderLock(
    _In_ PCSTR Where)
{
    PRTL_CRITICAL_SECTION Lock = NtCurrentPeb()->LoaderLock;
    HANDLE Self = UlongToHandle(GetCurrentThreadId());

    ok(Lock != NULL, "%s: PEB->LoaderLock is NULL\n", Where);
    if (!Lock)
        return;
    ok(Lock->OwningThread != Self,
       "%s: this thread owns the loader lock (lock count %ld, recursion %ld)\n",
       Where, Lock->LockCount, Lock->RecursionCount);
}

static
DWORD
WINAPI
ThreadProc(
    _In_ PVOID Parameter)
{
    UNREFERENCED_PARAMETER(Parameter);
    CheckLoaderLock("new thread");
    return 0;
}

START_TEST(LoaderLock)
{
    HANDLE Thread;
    HMODULE Module;

    CheckLoaderLock("test thread");

    Thread = CreateThread(NULL, 0, ThreadProc, NULL, 0, NULL);
    ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    if (Thread)
    {
        ok(WaitForSingleObject(Thread, 10000) == WAIT_OBJECT_0, "The thread did not end\n");
        CloseHandle(Thread);
    }

    Module = LoadLibraryW(L"shell32.dll");
    ok(Module != NULL, "LoadLibrary(shell32) failed: %lu\n", GetLastError());
    CheckLoaderLock("after LoadLibrary");
    if (Module)
        FreeLibrary(Module);
    CheckLoaderLock("after FreeLibrary");
}
