/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for handle inheritance with a handle list and a restricted token
 *
 * Chromium starts its sandboxed processes with CreateProcessAsUserW, a
 * restricted token and the handles to inherit in a PROC_THREAD_ATTRIBUTE_HANDLE_LIST
 * of a STARTUPINFOEX. The child must get the handle it was given, in all four
 * combinations (CreateProcess / AsUser, with and without a handle list).
 */

#include "precomp.h"

typedef BOOL (WINAPI *PINIT)(LPPROC_THREAD_ATTRIBUTE_LIST, DWORD, DWORD, PSIZE_T);
typedef BOOL (WINAPI *PUPDATE)(LPPROC_THREAD_ATTRIBUTE_LIST, DWORD, DWORD_PTR, PVOID, SIZE_T, PVOID, PSIZE_T);
typedef VOID (WINAPI *PDELETE)(LPPROC_THREAD_ATTRIBUTE_LIST);

#ifndef PROC_THREAD_ATTRIBUTE_HANDLE_LIST
#define PROC_THREAD_ATTRIBUTE_HANDLE_LIST 0x00020002
#endif
#ifndef EXTENDED_STARTUPINFO_PRESENT
#define EXTENDED_STARTUPINFO_PRESENT 0x00080000
#endif

#define CHILD_OK_EXIT_CODE 0x5A

static VOID
Child(
    _In_ PCSTR HandleText)
{
    HANDLE hEvent = (HANDLE)(ULONG_PTR)strtoul(HandleText, NULL, 16);
    DWORD Flags = 0;

    if (!GetHandleInformation(hEvent, &Flags))
        ExitProcess(GetLastError() ? GetLastError() : 1);
    if (!SetEvent(hEvent))
        ExitProcess(2);
    ExitProcess(CHILD_OK_EXIT_CODE);
}

START_TEST(InheritHandleList)
{
    static const char *Names[] = { "CreateProcess", "CreateProcess + handle list",
                                   "CreateProcessAsUser", "CreateProcessAsUser + handle list" };
    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    PINIT pInit = (PINIT)GetProcAddress(hKernel32, "InitializeProcThreadAttributeList");
    PUPDATE pUpdate = (PUPDATE)GetProcAddress(hKernel32, "UpdateProcThreadAttribute");
    PDELETE pDelete = (PDELETE)GetProcAddress(hKernel32, "DeleteProcThreadAttributeList");
    SECURITY_ATTRIBUTES Inheritable = { sizeof(Inheritable), NULL, TRUE };
    WCHAR FileName[MAX_PATH];
    WCHAR CommandLine[MAX_PATH + 64];
    HANDLE hEvent, hToken = NULL, hRestricted = NULL;
    char **argv;
    int argc, Mode;

    argc = winetest_get_mainargs(&argv);
    if (argc >= 4 && !strcmp(argv[2], "child"))
    {
        Child(argv[3]);
        return;
    }

    hEvent = CreateEventW(&Inheritable, TRUE, FALSE, NULL);
    ok(hEvent != NULL, "CreateEvent failed with %lu\n", GetLastError());
    if (!hEvent)
        return;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &hToken))
    {
        ok(CreateRestrictedToken(hToken, DISABLE_MAX_PRIVILEGE, 0, NULL, 0, NULL, 0, NULL, &hRestricted),
           "CreateRestrictedToken failed with %lu\n", GetLastError());
    }

    GetModuleFileNameW(NULL, FileName, _countof(FileName));
    StringCbPrintfW(CommandLine, sizeof(CommandLine), L"\"%ls\" InheritHandleList child %p", FileName, hEvent);

    for (Mode = 0; Mode < 4; Mode++)
    {
        STARTUPINFOEXW StartupInfo;
        PROCESS_INFORMATION ProcessInfo;
        WCHAR CommandLineCopy[MAX_PATH + 64];
        DWORD Flags = CREATE_SUSPENDED, Wait, ExitCode = 0;
        SIZE_T Size = 0;
        BOOL Ret;

        if ((Mode & 1) && (!pInit || !pUpdate || !pDelete))
        {
            skip("%s: attribute lists are not exported (Vista+)\n", Names[Mode]);
            continue;
        }
        if ((Mode & 2) && !hRestricted)
        {
            skip("%s: no restricted token\n", Names[Mode]);
            continue;
        }

        ResetEvent(hEvent);
        ZeroMemory(&StartupInfo, sizeof(StartupInfo));
        StartupInfo.StartupInfo.cb = sizeof(StartupInfo.StartupInfo);
        /* No standard handles: only the event is to be inherited */
        StartupInfo.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        if (Mode & 1)
        {
            pInit(NULL, 1, 0, &Size);
            StartupInfo.lpAttributeList = HeapAlloc(GetProcessHeap(), 0, Size);
            if (!StartupInfo.lpAttributeList || !pInit(StartupInfo.lpAttributeList, 1, 0, &Size) ||
                !pUpdate(StartupInfo.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                         &hEvent, sizeof(hEvent), NULL, NULL))
            {
                ok(0, "%s: building the attribute list failed with %lu\n", Names[Mode], GetLastError());
                if (StartupInfo.lpAttributeList)
                    HeapFree(GetProcessHeap(), 0, StartupInfo.lpAttributeList);
                continue;
            }
            StartupInfo.StartupInfo.cb = sizeof(StartupInfo);
            Flags |= EXTENDED_STARTUPINFO_PRESENT;
        }

        StringCbCopyW(CommandLineCopy, sizeof(CommandLineCopy), CommandLine);
        if (Mode & 2)
            Ret = CreateProcessAsUserW(hRestricted, FileName, CommandLineCopy, NULL, NULL, TRUE, Flags,
                                       NULL, NULL, &StartupInfo.StartupInfo, &ProcessInfo);
        else
            Ret = CreateProcessW(FileName, CommandLineCopy, NULL, NULL, TRUE, Flags,
                                 NULL, NULL, &StartupInfo.StartupInfo, &ProcessInfo);
        ok(Ret, "%s: creating the child failed with %lu\n", Names[Mode], GetLastError());
        if (Ret)
        {
            ResumeThread(ProcessInfo.hThread);
            Wait = WaitForSingleObject(ProcessInfo.hProcess, 15000);
            ok(Wait == WAIT_OBJECT_0, "%s: the child did not end (wait %lu)\n", Names[Mode], Wait);
            if (Wait != WAIT_OBJECT_0)
            {
                TerminateProcess(ProcessInfo.hProcess, 1);
                WaitForSingleObject(ProcessInfo.hProcess, 5000);
            }
            GetExitCodeProcess(ProcessInfo.hProcess, &ExitCode);
            ok(ExitCode == CHILD_OK_EXIT_CODE,
               "%s: child exit code 0x%lx (the handle was not valid in the child)\n", Names[Mode], ExitCode);
            ok(WaitForSingleObject(hEvent, 0) == WAIT_OBJECT_0,
               "%s: the event was not signaled by the child\n", Names[Mode]);
            CloseHandle(ProcessInfo.hThread);
            CloseHandle(ProcessInfo.hProcess);
        }
        if (StartupInfo.lpAttributeList)
        {
            pDelete(StartupInfo.lpAttributeList);
            HeapFree(GetProcessHeap(), 0, StartupInfo.lpAttributeList);
        }
    }

    if (hRestricted) CloseHandle(hRestricted);
    if (hToken) CloseHandle(hToken);
    CloseHandle(hEvent);
}
