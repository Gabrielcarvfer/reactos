/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for patching a child's ntdll through copy-on-write
 *
 * Chromium's sandbox patches the ntdll of a new (suspended) child: it makes
 * the page PAGE_WRITECOPY with VirtualProtectEx, writes the new code with
 * WriteProcessMemory and restores the protection. The copy-on-write of that
 * page must not change the protection of the next page (ReactOS once made it
 * read/write, so it lost execute and the child crashed in ntdll), and the
 * child must still run.
 */

#include "precomp.h"

#define CHILD_EXIT_CODE 7
#define TEST_PAGE_SIZE 0x1000

static DWORD
ProtectionOf(
    _In_ HANDLE hProcess,
    _In_ PVOID Address)
{
    MEMORY_BASIC_INFORMATION Info;

    if (!VirtualQueryEx(hProcess, Address, &Info, sizeof(Info)))
        return 0;
    return Info.Protect;
}

START_TEST(WriteProcessMemoryCow)
{
    WCHAR FileName[MAX_PATH];
    WCHAR CommandLine[MAX_PATH + 48];
    STARTUPINFOW StartupInfo;
    PROCESS_INFORMATION ProcessInfo;
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    PBYTE Target, NextPage;
    BYTE Bytes[32], Check[32];
    DWORD OldProtect = 0, OldProtect2 = 0, NextBefore, Protect, Wait, ExitCode = 0;
    SIZE_T Done;
    BOOL Ret;
    char **argv;
    int argc;

    argc = winetest_get_mainargs(&argv);
    if (argc >= 3 && !strcmp(argv[2], "child"))
    {
        /* Runs through the patched ntdll code (NtOpenThread is not even
           needed: getting here at all means ntdll still executes) */
        ExitProcess(CHILD_EXIT_CODE);
    }

    /* ntdll is at the same address in every process */
    Target = (PBYTE)GetProcAddress(hNtdll, "NtOpenThread");
    ok(Target != NULL, "NtOpenThread not found\n");
    if (!Target)
        return;
    NextPage = (PBYTE)(((ULONG_PTR)Target | (TEST_PAGE_SIZE - 1)) + 1);
    if (Target + sizeof(Bytes) > NextPage)
    {
        skip("NtOpenThread is at the end of a page\n");
        return;
    }

    GetModuleFileNameW(NULL, FileName, _countof(FileName));
    StringCbPrintfW(CommandLine, sizeof(CommandLine), L"\"%ls\" WriteProcessMemoryCow child", FileName);
    ZeroMemory(&StartupInfo, sizeof(StartupInfo));
    StartupInfo.cb = sizeof(StartupInfo);
    StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    if (!CreateProcessW(FileName, CommandLine, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL,
                        &StartupInfo, &ProcessInfo))
    {
        skip("CreateProcess failed with %lu\n", GetLastError());
        return;
    }

    NextBefore = ProtectionOf(ProcessInfo.hProcess, NextPage);
    ok(NextBefore != 0, "VirtualQueryEx of the next page failed with %lu\n", GetLastError());

    Ret = ReadProcessMemory(ProcessInfo.hProcess, Target, Bytes, sizeof(Bytes), &Done);
    ok(Ret && Done == sizeof(Bytes), "ReadProcessMemory: %d, %Iu bytes, error %lu\n", Ret, Done, GetLastError());

    Ret = VirtualProtectEx(ProcessInfo.hProcess, Target, sizeof(Bytes), PAGE_WRITECOPY, &OldProtect);
    ok(Ret, "VirtualProtectEx(PAGE_WRITECOPY) failed with %lu\n", GetLastError());
    Protect = ProtectionOf(ProcessInfo.hProcess, NextPage);
    ok(Protect == NextBefore, "After PAGE_WRITECOPY: next page protection 0x%lx, expected 0x%lx\n", Protect, NextBefore);

    Done = 0;
    Ret = WriteProcessMemory(ProcessInfo.hProcess, Target, Bytes, sizeof(Bytes), &Done);
    ok(Ret && Done == sizeof(Bytes), "WriteProcessMemory: %d, %Iu bytes, error %lu\n", Ret, Done, GetLastError());
    Protect = ProtectionOf(ProcessInfo.hProcess, NextPage);
    ok(Protect == NextBefore, "After the write: next page protection 0x%lx, expected 0x%lx\n", Protect, NextBefore);

    Ret = VirtualProtectEx(ProcessInfo.hProcess, Target, sizeof(Bytes), OldProtect, &OldProtect2);
    ok(Ret, "VirtualProtectEx(restore 0x%lx) failed with %lu\n", OldProtect, GetLastError());
    Protect = ProtectionOf(ProcessInfo.hProcess, Target);
    ok(Protect == OldProtect, "Restored page protection 0x%lx, expected 0x%lx\n", Protect, OldProtect);
    Protect = ProtectionOf(ProcessInfo.hProcess, NextPage);
    ok(Protect == NextBefore, "After the restore: next page protection 0x%lx, expected 0x%lx\n", Protect, NextBefore);

    Ret = ReadProcessMemory(ProcessInfo.hProcess, Target, Check, sizeof(Check), &Done);
    ok(Ret && !memcmp(Bytes, Check, sizeof(Bytes)), "The written bytes did not read back\n");

    ResumeThread(ProcessInfo.hThread);
    Wait = WaitForSingleObject(ProcessInfo.hProcess, 20000);
    ok(Wait == WAIT_OBJECT_0, "The patched child did not end (wait %lu)\n", Wait);
    if (Wait != WAIT_OBJECT_0)
    {
        TerminateProcess(ProcessInfo.hProcess, 1);
        WaitForSingleObject(ProcessInfo.hProcess, 5000);
    }
    GetExitCodeProcess(ProcessInfo.hProcess, &ExitCode);
    ok(ExitCode == CHILD_EXIT_CODE, "Patched child exit code 0x%lx, expected %u\n", ExitCode, CHILD_EXIT_CODE);
    CloseHandle(ProcessInfo.hThread);
    CloseHandle(ProcessInfo.hProcess);
}
