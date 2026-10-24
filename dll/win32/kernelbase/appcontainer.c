/*
 * PROJECT:     ReactOS Kernel Base
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     AppContainer tokens and capability SIDs (only in KernelBase on Windows)
 *
 * Chromium's sandbox (Edge 155: its renderers) makes the token of an
 * AppContainer process with CreateAppContainerToken and the SIDs of its
 * capabilities with DeriveCapabilitySidsFromName.
 */

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "winternl.h"
#include "wincrypt.h"

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) ((NTSTATUS)(Status) >= 0)
#endif

/* (winnt.h, Windows 8) */
typedef struct _KB_SECURITY_CAPABILITIES
{
    PSID AppContainerSid;
    PSID_AND_ATTRIBUTES Capabilities;
    DWORD CapabilityCount;
    DWORD Reserved;
} KB_SECURITY_CAPABILITIES, *PKB_SECURITY_CAPABILITIES;

/* The capabilities of Windows 8, which have SIDs of their own */
static const struct { const WCHAR *Name; DWORD Rid; } KnownCapabilities[] =
{
    { L"internetClient", 1 }, { L"internetClientServer", 2 }, { L"privateNetworkClientServer", 3 },
    { L"picturesLibrary", 4 }, { L"videosLibrary", 5 }, { L"musicLibrary", 6 }, { L"documentsLibrary", 7 },
    { L"enterpriseAuthentication", 8 }, { L"sharedUserCertificates", 9 }, { L"removableStorage", 10 },
    { L"appointments", 11 }, { L"contacts", 12 },
};

/* SHA-256 of the upper-case name (UTF-16), as 8 numbers */
static
BOOL
HashCapabilityName(
    _In_ LPCWSTR Name,
    _Out_writes_(8) DWORD *Hash)
{
    DWORD Size = 8 * sizeof(DWORD);
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    SIZE_T Length = wcslen(Name);
    WCHAR *Upper;
    BOOL Success;

    Upper = HeapAlloc(GetProcessHeap(), 0, (Length + 1) * sizeof(WCHAR));
    if (!Upper)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    wcscpy(Upper, Name);
    _wcsupr(Upper);
    Success = CryptAcquireContextW(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) &&
              CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash) &&
              CryptHashData(hHash, (const BYTE *)Upper, (DWORD)(Length * sizeof(WCHAR)), 0) &&
              CryptGetHashParam(hHash, HP_HASHVAL, (BYTE *)Hash, &Size, 0);
    if (hHash) CryptDestroyHash(hHash);
    if (hProv) CryptReleaseContext(hProv, 0);
    HeapFree(GetProcessHeap(), 0, Upper);
    return Success;
}

/* An array of one SID, freed with LocalFree (the SID, then the array) */
static
PSID *
OneSidArray(
    _In_ PSID_IDENTIFIER_AUTHORITY Authority,
    _In_ BYTE Count,
    _In_reads_(Count) const DWORD *Rids)
{
    PSID *Array;
    PSID Sid;

    Array = LocalAlloc(LPTR, sizeof(PSID));
    Sid = LocalAlloc(LPTR, GetSidLengthRequired(Count));
    if (!Array || !Sid)
    {
        LocalFree(Array);
        LocalFree(Sid);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    InitializeSid(Sid, Authority, Count);
    while (Count--)
        *GetSidSubAuthority(Sid, Count) = Rids[Count];
    Array[0] = Sid;
    return Array;
}

BOOL
WINAPI
DeriveCapabilitySidsFromName(
    _In_ LPCWSTR CapName,
    _Outptr_result_buffer_(*CapGroupSidCount) PSID **CapGroupSids,
    _Out_ DWORD *CapGroupSidCount,
    _Outptr_result_buffer_(*CapSidCount) PSID **CapSids,
    _Out_ DWORD *CapSidCount)
{
    static SID_IDENTIFIER_AUTHORITY NtAuthority = { SECURITY_NT_AUTHORITY };
    static SID_IDENTIFIER_AUTHORITY AppPackageAuthority = { { 0, 0, 0, 0, 0, 15 } };
    DWORD Hash[8], Rids[10];
    ULONG i;

    if (!CapName || !CapGroupSids || !CapGroupSidCount || !CapSids || !CapSidCount)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *CapGroupSids = *CapSids = NULL;
    *CapGroupSidCount = *CapSidCount = 0;
    if (!HashCapabilityName(CapName, Hash))
        return FALSE;

    /* The group: S-1-5-32-<hash> */
    Rids[0] = SECURITY_BUILTIN_DOMAIN_RID;
    memcpy(&Rids[1], Hash, sizeof(Hash));
    *CapGroupSids = OneSidArray(&NtAuthority, 9, Rids);
    if (!*CapGroupSids)
        return FALSE;

    /* The capability: S-1-15-3-<known RID>, else S-1-15-3-1024-<hash> */
    Rids[0] = 3; /* SECURITY_CAPABILITY_BASE_RID */
    for (i = 0; i < ARRAYSIZE(KnownCapabilities); i++)
    {
        if (!_wcsicmp(CapName, KnownCapabilities[i].Name))
            break;
    }
    if (i < ARRAYSIZE(KnownCapabilities))
    {
        Rids[1] = KnownCapabilities[i].Rid;
        *CapSids = OneSidArray(&AppPackageAuthority, 2, Rids);
    }
    else
    {
        Rids[1] = 1024; /* SECURITY_CAPABILITY_APP_RID */
        memcpy(&Rids[2], Hash, sizeof(Hash));
        *CapSids = OneSidArray(&AppPackageAuthority, 10, Rids);
    }
    if (!*CapSids)
    {
        LocalFree((*CapGroupSids)[0]);
        LocalFree(*CapGroupSids);
        *CapGroupSids = NULL;
        return FALSE;
    }
    *CapGroupSidCount = *CapSidCount = 1;
    return TRUE;
}

typedef NTSTATUS (NTAPI *PNTCREATELOWBOXTOKEN)(PHANDLE, HANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PSID, ULONG,
                                               PSID_AND_ATTRIBUTES, ULONG, HANDLE *);

BOOL
WINAPI
CreateAppContainerToken(
    _In_opt_ HANDLE TokenHandle,
    _In_ PKB_SECURITY_CAPABILITIES SecurityCapabilities,
    _Out_ PHANDLE OutToken)
{
    PNTCREATELOWBOXTOKEN pNtCreateLowBoxToken;
    HANDLE OwnToken = NULL;
    NTSTATUS Status;

    if (!SecurityCapabilities || !SecurityCapabilities->AppContainerSid || !OutToken)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    pNtCreateLowBoxToken = (PNTCREATELOWBOXTOKEN)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtCreateLowBoxToken");
    if (!pNtCreateLowBoxToken)
    {
        SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
        return FALSE;
    }
    if (!TokenHandle)
    {
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY, &OwnToken))
            return FALSE;
        TokenHandle = OwnToken;
    }
    Status = pNtCreateLowBoxToken(OutToken, TokenHandle, TOKEN_ALL_ACCESS, NULL,
                                  SecurityCapabilities->AppContainerSid,
                                  SecurityCapabilities->CapabilityCount,
                                  SecurityCapabilities->Capabilities, 0, NULL);
    if (OwnToken)
        CloseHandle(OwnToken);
    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return FALSE;
    }
    return TRUE;
}
