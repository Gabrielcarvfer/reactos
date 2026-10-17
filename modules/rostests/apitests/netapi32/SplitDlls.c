/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for srvcli.dll / wkscli.dll / netutils.dll (netapi32 split, Windows 7+)
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 *
 * TeamViewer_Service.exe imports NetShareEnum from srvcli.dll,
 * NetGetJoinInformation from wkscli.dll and NetApiBufferFree from
 * netutils.dll: without these DLLs it does not start (c0000135).
 */

#include <apitest.h>
#include <windef.h>
#include <winbase.h>
#include <lm.h>

typedef NET_API_STATUS (WINAPI *PFN_NetGetJoinInformation)(LPCWSTR, LPWSTR *, PNETSETUP_JOIN_STATUS);
typedef NET_API_STATUS (WINAPI *PFN_NetApiBufferFree)(LPVOID);

START_TEST(SplitDlls)
{
    HMODULE Srvcli = LoadLibraryW(L"srvcli.dll");
    HMODULE Wkscli = LoadLibraryW(L"wkscli.dll");
    HMODULE Netutils = LoadLibraryW(L"netutils.dll");
    PFN_NetGetJoinInformation pNetGetJoinInformation;
    PFN_NetApiBufferFree pNetApiBufferFree;
    NETSETUP_JOIN_STATUS Status = NetSetupUnknownStatus;
    LPWSTR Name = NULL;
    NET_API_STATUS Ret;

    ok(Srvcli != NULL, "srvcli.dll: %lu\n", GetLastError());
    ok(Wkscli != NULL, "wkscli.dll: %lu\n", GetLastError());
    ok(Netutils != NULL, "netutils.dll: %lu\n", GetLastError());
    if (!Srvcli || !Wkscli || !Netutils)
        return;

    ok(GetProcAddress(Srvcli, "NetShareEnum") != NULL, "No srvcli!NetShareEnum\n");
    ok(GetProcAddress(Srvcli, "NetServerGetInfo") != NULL, "No srvcli!NetServerGetInfo\n");
    ok(GetProcAddress(Wkscli, "NetWkstaGetInfo") != NULL, "No wkscli!NetWkstaGetInfo\n");
    pNetGetJoinInformation = (PFN_NetGetJoinInformation)GetProcAddress(Wkscli, "NetGetJoinInformation");
    pNetApiBufferFree = (PFN_NetApiBufferFree)GetProcAddress(Netutils, "NetApiBufferFree");
    ok(pNetGetJoinInformation != NULL, "No wkscli!NetGetJoinInformation\n");
    ok(pNetApiBufferFree != NULL, "No netutils!NetApiBufferFree\n");

    if (pNetGetJoinInformation && pNetApiBufferFree)
    {
        /* A buffer from wkscli is freed with netutils, as TeamViewer does */
        Ret = pNetGetJoinInformation(NULL, &Name, &Status);
        ok(Ret == NERR_Success, "NetGetJoinInformation: %lu\n", Ret);
        ok(Name != NULL, "No name\n");
        ok(Status != NetSetupUnknownStatus, "Status %d\n", Status);
        if (Name)
            ok(pNetApiBufferFree(Name) == NERR_Success, "NetApiBufferFree failed\n");
    }

    FreeLibrary(Netutils);
    FreeLibrary(Wkscli);
    FreeLibrary(Srvcli);
}
