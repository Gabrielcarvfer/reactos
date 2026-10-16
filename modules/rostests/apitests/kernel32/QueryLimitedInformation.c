/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for PROCESS/THREAD_QUERY_LIMITED_INFORMATION handles
 *
 * Chromium duplicates its process handle with only
 * PROCESS_QUERY_LIMITED_INFORMATION and CHECKs that GetProcessTimes works
 * (Spotify). Handles with PROCESS_QUERY_INFORMATION must keep working.
 */

#include "precomp.h"

typedef BOOL (WINAPI *FN_QueryFullProcessImageNameW)(HANDLE, DWORD, LPWSTR, PDWORD);

START_TEST(QueryLimitedInformation)
{
    FN_QueryFullProcessImageNameW pQueryFullProcessImageNameW =
        (FN_QueryFullProcessImageNameW)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "QueryFullProcessImageNameW");
    HANDLE Process = NULL, Thread = NULL, Handle;
    FILETIME Creation, Exit, Kernel, User;
    WCHAR Name[MAX_PATH];
    DWORD Length;
    BOOL Ret;

    Ret = DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(), &Process,
                          PROCESS_QUERY_LIMITED_INFORMATION, FALSE, 0);
    ok(Ret, "Duplicating the process handle with QUERY_LIMITED failed with %lu\n", GetLastError());
    if (Ret)
    {
        ok(GetProcessTimes(Process, &Creation, &Exit, &Kernel, &User),
           "GetProcessTimes on a QUERY_LIMITED handle failed with %lu\n", GetLastError());
        if (pQueryFullProcessImageNameW)
        {
            Length = _countof(Name);
            ok(pQueryFullProcessImageNameW(Process, 0, Name, &Length),
               "QueryFullProcessImageNameW on a QUERY_LIMITED handle failed with %lu\n", GetLastError());
        }
        else
        {
            skip("No QueryFullProcessImageNameW\n");
        }
        ok(GetProcessId(Process) == GetCurrentProcessId(),
           "GetProcessId on a QUERY_LIMITED handle returned %lu (error %lu)\n", GetProcessId(Process), GetLastError());
        CloseHandle(Process);
    }

    Ret = DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &Thread,
                          THREAD_QUERY_LIMITED_INFORMATION, FALSE, 0);
    ok(Ret, "Duplicating the thread handle with QUERY_LIMITED failed with %lu\n", GetLastError());
    if (Ret)
    {
        ok(GetThreadTimes(Thread, &Creation, &Exit, &Kernel, &User),
           "GetThreadTimes on a QUERY_LIMITED handle failed with %lu\n", GetLastError());
        CloseHandle(Thread);
    }

    Handle = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, GetCurrentProcessId());
    ok(Handle != NULL, "OpenProcess(QUERY_INFORMATION) failed with %lu\n", GetLastError());
    if (Handle)
    {
        ok(GetProcessTimes(Handle, &Creation, &Exit, &Kernel, &User),
           "GetProcessTimes on a QUERY_INFORMATION handle failed with %lu\n", GetLastError());
        CloseHandle(Handle);
    }

    Handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentProcessId());
    ok(Handle != NULL, "OpenProcess(QUERY_LIMITED) failed with %lu\n", GetLastError());
    if (Handle)
    {
        ok(GetProcessTimes(Handle, &Creation, &Exit, &Kernel, &User),
           "GetProcessTimes on an OpenProcess(QUERY_LIMITED) handle failed with %lu\n", GetLastError());
        CloseHandle(Handle);
    }

    Handle = OpenProcess(PROCESS_VM_READ, FALSE, GetCurrentProcessId());
    ok(Handle != NULL, "OpenProcess(VM_READ) failed with %lu\n", GetLastError());
    if (Handle)
    {
        SetLastError(0xdeadbeef);
        ok(!GetProcessTimes(Handle, &Creation, &Exit, &Kernel, &User),
           "GetProcessTimes on a VM_READ-only handle succeeded\n");
        ok(GetLastError() == ERROR_ACCESS_DENIED, "Error %lu, expected ERROR_ACCESS_DENIED\n", GetLastError());
        CloseHandle(Handle);
    }
}
