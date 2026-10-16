/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for RegNotifyChangeKeyValue
 *
 * Registry change notifications: asynchronous (event) and synchronous forms,
 * subtree watches, key deletion and REG_NOTIFY_THREAD_AGNOSTIC. Chromium
 * watches HKLM\...\Tcpip\Parameters this way for DNS changes, and settings
 * watchers (VS Code, Spotify) watch their keys under HKCU.
 */

#include "precomp.h"

#ifndef REG_NOTIFY_THREAD_AGNOSTIC
#define REG_NOTIFY_THREAD_AGNOSTIC 0x10000000L
#endif

#define TEST_KEY L"Software\\ReactOS_RegNotifyChangeKeyValue_Test"

static HKEY OpenTestKey(PCWSTR Sub)
{
    WCHAR Path[MAX_PATH];
    HKEY Key = NULL;

    StringCchCopyW(Path, _countof(Path), TEST_KEY);
    if (Sub)
    {
        StringCchCatW(Path, _countof(Path), L"\\");
        StringCchCatW(Path, _countof(Path), Sub);
    }
    if (RegCreateKeyExW(HKEY_CURRENT_USER, Path, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &Key, NULL))
        return NULL;
    return Key;
}

static void SetDword(HKEY Key, PCWSTR Name, DWORD Value)
{
    RegSetValueExW(Key, Name, 0, REG_DWORD, (const BYTE *)&Value, sizeof(Value));
}

static LONG Arm(HKEY Key, BOOL Tree, DWORD Filter, HANDLE Event)
{
    return RegNotifyChangeKeyValue(Key, Tree, Filter, Event, TRUE);
}

/* Deletes a key and its subkeys (RegDeleteTree is Vista+) */
static void DeleteTree(HKEY Root, PCWSTR Path)
{
    HKEY Key;
    WCHAR Name[MAX_PATH];

    if (RegOpenKeyExW(Root, Path, 0, KEY_ALL_ACCESS, &Key))
        return;
    while (!RegEnumKeyW(Key, 0, Name, _countof(Name)))
    {
        DeleteTree(Key, Name);
        if (RegDeleteKeyW(Key, Name))
            break;
    }
    RegCloseKey(Key);
    RegDeleteKeyW(Root, Path);
}

typedef struct
{
    HKEY Key;
    LONG Ret;
} SYNC_CONTEXT;

static DWORD WINAPI SyncThread(PVOID Param)
{
    SYNC_CONTEXT *Context = Param;
    Context->Ret = RegNotifyChangeKeyValue(Context->Key, FALSE, REG_NOTIFY_CHANGE_LAST_SET, NULL, FALSE);
    return 0;
}

START_TEST(RegNotifyChangeKeyValue)
{
    HANDLE Event;
    HKEY Root, Key, Key2, Sub;
    LONG Ret, Ret2;
    DWORD Wait;

    Event = CreateEventW(NULL, FALSE, FALSE, NULL);
    ok(Event != NULL, "CreateEvent failed: %lu\n", GetLastError());
    if (!Event)
        return;

    DeleteTree(HKEY_CURRENT_USER, TEST_KEY);
    Root = OpenTestKey(NULL);
    ok(Root != NULL, "Could not create the test key\n");
    if (!Root)
    {
        CloseHandle(Event);
        return;
    }

    /* A value set on the watched key */
    Key = OpenTestKey(NULL);
    Ret = Arm(Key, FALSE, REG_NOTIFY_CHANGE_LAST_SET, Event);
    ok(Ret == ERROR_SUCCESS, "Arm (LAST_SET): %ld\n", Ret);
    Wait = WaitForSingleObject(Event, 0);
    ok(Wait == WAIT_TIMEOUT, "Event signalled right after arming: %lu\n", Wait);
    SetDword(Root, L"v1", 1);
    Wait = WaitForSingleObject(Event, 1000);
    ok(Wait == WAIT_OBJECT_0, "Value set did not signal the event: %lu\n", Wait);

    /* A change between two requests completes the next one at once */
    SetDword(Root, L"v1", 2);
    Ret = Arm(Key, FALSE, REG_NOTIFY_CHANGE_LAST_SET, Event);
    Wait = WaitForSingleObject(Event, 1000);
    ok(Ret == ERROR_SUCCESS && Wait == WAIT_OBJECT_0,
       "Change made while not armed not reported on re-arm: %ld, %lu\n", Ret, Wait);

    /* Value delete */
    Ret = Arm(Key, FALSE, REG_NOTIFY_CHANGE_LAST_SET, Event);
    RegDeleteValueW(Root, L"v1");
    Wait = WaitForSingleObject(Event, 1000);
    ok(Ret == ERROR_SUCCESS && Wait == WAIT_OBJECT_0,
       "Value delete did not signal the event: %ld, %lu\n", Ret, Wait);
    RegCloseKey(Key);

    /* Subtree watch, subkey created two levels down */
    Sub = OpenTestKey(L"A");
    Key = OpenTestKey(NULL);
    Ret = Arm(Key, TRUE, REG_NOTIFY_CHANGE_NAME, Event);
    ok(Ret == ERROR_SUCCESS, "Arm (subtree, NAME): %ld\n", Ret);
    Key2 = OpenTestKey(L"A\\B");
    Wait = WaitForSingleObject(Event, 1000);
    ok(Wait == WAIT_OBJECT_0, "Subtree: subkey creation did not signal the event: %lu\n", Wait);
    if (Key2)
        RegCloseKey(Key2);

    /* Subtree watch, value set in a subkey */
    Ret = Arm(Key, TRUE, REG_NOTIFY_CHANGE_LAST_SET, Event);
    SetDword(Sub, L"x", 1);
    Wait = WaitForSingleObject(Event, 1000);
    ok(Ret == ERROR_SUCCESS && Wait == WAIT_OBJECT_0,
       "Subtree: value set in a subkey did not signal the event: %ld, %lu\n", Ret, Wait);
    RegCloseKey(Key);

    /* A watch without subtree does not see a subkey's value */
    Key = OpenTestKey(NULL);
    Ret = Arm(Key, FALSE, REG_NOTIFY_CHANGE_LAST_SET, Event);
    SetDword(Sub, L"x", 2);
    Wait = WaitForSingleObject(Event, 1000);
    ok(Ret == ERROR_SUCCESS && Wait == WAIT_TIMEOUT,
       "No subtree: a subkey's value change was reported: %ld, %lu\n", Ret, Wait);
    RegCloseKey(Key);

    /* Subkey deletion is reported on the parent */
    Key = OpenTestKey(NULL);
    Ret = Arm(Key, FALSE, REG_NOTIFY_CHANGE_NAME, Event);
    Ret2 = RegDeleteKeyW(Sub, L"B");
    Wait = WaitForSingleObject(Event, 1000);
    ok(Ret == ERROR_SUCCESS && Ret2 == ERROR_SUCCESS && Wait == WAIT_OBJECT_0,
       "Subkey deletion did not signal the parent's watch: %ld, %ld, %lu\n", Ret, Ret2, Wait);
    RegCloseKey(Key);
    RegCloseKey(Sub);

    /* No change: not signalled */
    Key = OpenTestKey(NULL);
    Ret = Arm(Key, TRUE, REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET, Event);
    Wait = WaitForSingleObject(Event, 1000);
    ok(Ret == ERROR_SUCCESS && Wait == WAIT_TIMEOUT,
       "Event signalled without a change: %ld, %lu\n", Ret, Wait);
    /* Closing the key completes the pending notification */
    RegCloseKey(Key);
    Wait = WaitForSingleObject(Event, 1000);
    ok(Wait == WAIT_OBJECT_0, "Closing the key did not signal the pending notification: %lu\n", Wait);

    /* Synchronous form, in a thread */
    {
        SYNC_CONTEXT Context;
        HANDLE Thread;

        Context.Key = OpenTestKey(NULL);
        Context.Ret = -1;
        Thread = CreateThread(NULL, 0, SyncThread, &Context, 0, NULL);
        ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
        if (Thread)
        {
            Wait = WaitForSingleObject(Thread, 500);
            ok(Wait == WAIT_TIMEOUT, "Synchronous call did not block until a change: %lu\n", Wait);
            SetDword(Root, L"v2", 1);
            Wait = WaitForSingleObject(Thread, 1000);
            ok(Wait == WAIT_OBJECT_0 && Context.Ret == ERROR_SUCCESS,
               "Synchronous call did not return after a change: %lu, %ld\n", Wait, Context.Ret);
            if (Wait != WAIT_OBJECT_0)
            {
                TerminateThread(Thread, 0);
                WaitForSingleObject(Thread, 1000);
            }
            CloseHandle(Thread);
        }
        RegCloseKey(Context.Key);
    }

    /* REG_NOTIFY_THREAD_AGNOSTIC */
    Key = OpenTestKey(NULL);
    Ret = Arm(Key, FALSE, REG_NOTIFY_CHANGE_LAST_SET | REG_NOTIFY_THREAD_AGNOSTIC, Event);
    ok(Ret == ERROR_SUCCESS, "REG_NOTIFY_THREAD_AGNOSTIC refused: %ld\n", Ret);
    SetDword(Root, L"v3", 1);
    Wait = WaitForSingleObject(Event, 1000);
    ok(Wait == WAIT_OBJECT_0, "Thread agnostic notification not signalled: %lu\n", Wait);
    RegCloseKey(Key);

    /* Invalid filter */
    Key = OpenTestKey(NULL);
    Ret = Arm(Key, FALSE, 0x100, Event);
    ok(Ret == ERROR_INVALID_PARAMETER, "Invalid filter: %ld\n", Ret);
    RegCloseKey(Key);

    /* The watched key is deleted */
    Sub = OpenTestKey(L"C");
    Ret = Arm(Sub, FALSE, REG_NOTIFY_CHANGE_LAST_SET, Event);
    Ret2 = RegDeleteKeyW(Root, L"C");
    Wait = WaitForSingleObject(Event, 1000);
    ok(Ret == ERROR_SUCCESS && Ret2 == ERROR_SUCCESS && Wait == WAIT_OBJECT_0,
       "Deleting the watched key did not signal it: %ld, %ld, %lu\n", Ret, Ret2, Wait);
    RegCloseKey(Sub);

    /* Chromium's DNS configuration watch */
    Ret = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters",
                        0, KEY_NOTIFY, &Key);
    ok(Ret == ERROR_SUCCESS, "Opening Tcpip\\Parameters with KEY_NOTIFY: %ld\n", Ret);
    if (Ret == ERROR_SUCCESS)
    {
        Ret = RegNotifyChangeKeyValue(Key, TRUE,
                                      REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_ATTRIBUTES |
                                      REG_NOTIFY_CHANGE_LAST_SET | REG_NOTIFY_CHANGE_SECURITY |
                                      REG_NOTIFY_THREAD_AGNOSTIC,
                                      Event, TRUE);
        ok(Ret == ERROR_SUCCESS, "Chromium's Tcpip\\Parameters watch: %ld\n", Ret);
        RegCloseKey(Key);
    }

    RegCloseKey(Root);
    DeleteTree(HKEY_CURRENT_USER, TEST_KEY);
    CloseHandle(Event);
}
