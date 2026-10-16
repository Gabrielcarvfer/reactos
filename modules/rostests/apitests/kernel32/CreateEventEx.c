/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for the Vista "Ex" synchronization functions
 *
 * CreateEventExW, CreateMutexExW, FlushProcessWriteBuffers and
 * K32EnumProcessModulesEx were stubs returning NULL, which modern programs
 * (Chromium, Qt) treat as fatal.
 */

#include "precomp.h"

#ifndef CREATE_EVENT_MANUAL_RESET
#define CREATE_EVENT_MANUAL_RESET 0x1
#define CREATE_EVENT_INITIAL_SET 0x2
#endif
#ifndef CREATE_MUTEX_INITIAL_OWNER
#define CREATE_MUTEX_INITIAL_OWNER 0x1
#endif
#ifndef LIST_MODULES_ALL
#define LIST_MODULES_ALL 0x03
#endif

typedef HANDLE (WINAPI *FN_CreateObjectExW)(LPSECURITY_ATTRIBUTES, LPCWSTR, DWORD, DWORD);
typedef VOID (WINAPI *FN_FlushProcessWriteBuffers)(VOID);
typedef BOOL (WINAPI *FN_K32EnumProcessModulesEx)(HANDLE, HMODULE *, DWORD, LPDWORD, DWORD);

START_TEST(CreateEventEx)
{
    HMODULE Kernel32 = GetModuleHandleW(L"kernel32.dll");
    FN_CreateObjectExW pCreateEventExW = (FN_CreateObjectExW)GetProcAddress(Kernel32, "CreateEventExW");
    FN_CreateObjectExW pCreateMutexExW = (FN_CreateObjectExW)GetProcAddress(Kernel32, "CreateMutexExW");
    FN_FlushProcessWriteBuffers pFlushProcessWriteBuffers =
        (FN_FlushProcessWriteBuffers)GetProcAddress(Kernel32, "FlushProcessWriteBuffers");
    FN_K32EnumProcessModulesEx pK32EnumProcessModulesEx =
        (FN_K32EnumProcessModulesEx)GetProcAddress(Kernel32, "K32EnumProcessModulesEx");
    WCHAR Name[64];
    HANDLE Handle, Handle2;
    HMODULE Modules[64];
    DWORD Needed;

    if (!pCreateEventExW)
    {
        skip("CreateEventExW is not exported\n");
    }
    else
    {
        Handle = pCreateEventExW(NULL, NULL, CREATE_EVENT_MANUAL_RESET | CREATE_EVENT_INITIAL_SET, EVENT_ALL_ACCESS);
        ok(Handle != NULL, "CreateEventExW(manual, set) failed with %lu\n", GetLastError());
        if (Handle)
        {
            ok(WaitForSingleObject(Handle, 0) == WAIT_OBJECT_0, "Manual-reset event is not set\n");
            ok(WaitForSingleObject(Handle, 0) == WAIT_OBJECT_0, "Manual-reset event was reset by a wait\n");
            CloseHandle(Handle);
        }

        StringCchPrintfW(Name, _countof(Name), L"CreateEventEx_apitest_%lu", GetCurrentProcessId());
        Handle = pCreateEventExW(NULL, Name, 0, SYNCHRONIZE | EVENT_MODIFY_STATE);
        ok(Handle != NULL, "CreateEventExW(auto, named) failed with %lu\n", GetLastError());
        if (Handle)
        {
            ok(WaitForSingleObject(Handle, 0) == WAIT_TIMEOUT, "Auto-reset event is set initially\n");
            ok(SetEvent(Handle), "SetEvent failed with %lu\n", GetLastError());
            ok(WaitForSingleObject(Handle, 0) == WAIT_OBJECT_0, "Event is not set after SetEvent\n");
            ok(WaitForSingleObject(Handle, 0) == WAIT_TIMEOUT, "Auto-reset event was not reset by the wait\n");
            Handle2 = OpenEventW(SYNCHRONIZE, FALSE, Name);
            ok(Handle2 != NULL, "OpenEventW did not find the named event, error %lu\n", GetLastError());
            if (Handle2)
                CloseHandle(Handle2);
            CloseHandle(Handle);
        }

        SetLastError(0xdeadbeef);
        Handle = pCreateEventExW(NULL, NULL, 0x100, EVENT_ALL_ACCESS);
        ok(Handle == NULL, "CreateEventExW with invalid flags succeeded\n");
        ok(GetLastError() == ERROR_INVALID_PARAMETER, "Error %lu, expected ERROR_INVALID_PARAMETER\n", GetLastError());
        if (Handle)
            CloseHandle(Handle);
    }

    if (!pCreateMutexExW)
    {
        skip("CreateMutexExW is not exported\n");
    }
    else
    {
        Handle = pCreateMutexExW(NULL, NULL, CREATE_MUTEX_INITIAL_OWNER, MUTEX_ALL_ACCESS);
        ok(Handle != NULL, "CreateMutexExW(owned) failed with %lu\n", GetLastError());
        if (Handle)
        {
            ok(ReleaseMutex(Handle), "ReleaseMutex of an initially owned mutex failed with %lu\n", GetLastError());
            CloseHandle(Handle);
        }

        Handle = pCreateMutexExW(NULL, NULL, 0, MUTEX_ALL_ACCESS);
        ok(Handle != NULL, "CreateMutexExW(not owned) failed with %lu\n", GetLastError());
        if (Handle)
        {
            SetLastError(0xdeadbeef);
            ok(!ReleaseMutex(Handle), "ReleaseMutex of a mutex not owned succeeded\n");
            ok(GetLastError() == ERROR_NOT_OWNER, "Error %lu, expected ERROR_NOT_OWNER\n", GetLastError());
            CloseHandle(Handle);
        }
    }

    if (!pFlushProcessWriteBuffers)
    {
        skip("FlushProcessWriteBuffers is not exported\n");
    }
    else
    {
        pFlushProcessWriteBuffers();
        pFlushProcessWriteBuffers();
        ok(TRUE, "FlushProcessWriteBuffers returned\n");
    }

    if (!pK32EnumProcessModulesEx)
    {
        skip("K32EnumProcessModulesEx is not exported\n");
    }
    else
    {
        Needed = 0;
        ZeroMemory(Modules, sizeof(Modules));
        ok(pK32EnumProcessModulesEx(GetCurrentProcess(), Modules, sizeof(Modules), &Needed, LIST_MODULES_ALL),
           "K32EnumProcessModulesEx failed with %lu\n", GetLastError());
        ok(Needed >= sizeof(HMODULE), "Needed is %lu\n", Needed);
        ok(Modules[0] == GetModuleHandleW(NULL), "First module is %p, expected %p\n",
           Modules[0], GetModuleHandleW(NULL));
    }
}
