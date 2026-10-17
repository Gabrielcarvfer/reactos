/*
 * PROJECT:     ReactOS api tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     A 64-bit program shimmed at startup (MsysDecoy) must start
 *
 * sysmain.sdb gives every 64-bit program with a msys-2.0.dll beside it the
 * MsysDecoy shim (acgenral.dll). The shim engine loads acgenral during loader
 * init, with the entry points of the modules already loaded held back. msvcrt
 * was held back too, yet acgenral -> uxtheme -> shlwapi's DllMain needs it:
 * msvcrt's _lock() recursed until the stack overflowed and the process hung
 * in shlwapi's DLL_PROCESS_DETACH (Git for Windows: bash.exe, sh.exe).
 */

#include <windows.h>
#include <shlwapi.h>
#include "wine/test.h"

START_TEST(MsysShim)
{
#ifdef _WIN64
    WCHAR Dir[MAX_PATH], Child[MAX_PATH], Dll[MAX_PATH], Source[MAX_PATH], Self[MAX_PATH];
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    DWORD Wait, Code = 0;

    GetModuleFileNameW(NULL, Self, ARRAYSIZE(Self));
    PathRemoveFileSpecW(Self);
    PathCombineW(Source, Self, L"apphelp_msyschild.exe");
    if (GetFileAttributesW(Source) == INVALID_FILE_ATTRIBUTES)
    {
        skip("apphelp_msyschild.exe not found\n");
        return;
    }

    GetTempPathW(ARRAYSIZE(Dir), Dir);
    PathAppendW(Dir, L"apphelp_msysshim");
    CreateDirectoryW(Dir, NULL);
    PathCombineW(Child, Dir, L"msyschild.exe");
    PathCombineW(Dll, Dir, L"msys-2.0.dll");
    ok(CopyFileW(Source, Child, FALSE), "CopyFile failed: %lu\n", GetLastError());
    /* Any 64-bit DLL will do as msys-2.0.dll: the database matches the name */
    GetSystemDirectoryW(Source, ARRAYSIZE(Source));
    PathAppendW(Source, L"version.dll");
    ok(CopyFileW(Source, Dll, FALSE), "CopyFile failed: %lu\n", GetLastError());

    ok(CreateProcessW(Child, NULL, NULL, NULL, FALSE, 0, NULL, Dir, &si, &pi),
       "CreateProcess failed: %lu\n", GetLastError());
    if (pi.hProcess)
    {
        Wait = WaitForSingleObject(pi.hProcess, 20000);
        ok(Wait == WAIT_OBJECT_0, "the shimmed program hangs (wait %lu)\n", Wait);
        if (Wait != WAIT_OBJECT_0)
            TerminateProcess(pi.hProcess, 1);
        GetExitCodeProcess(pi.hProcess, &Code);
        ok(Code == 42, "exit code 0x%lx, expected 42\n", Code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }

    DeleteFileW(Child);
    DeleteFileW(Dll);
    RemoveDirectoryW(Dir);
#else
    skip("The MsysDecoy shim is for 64-bit programs\n");
#endif
}
