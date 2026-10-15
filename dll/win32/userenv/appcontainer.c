/*
 * PROJECT:     ReactOS User Environment
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     AppContainer names and profiles (Windows 8)
 *
 * An AppContainer's SID is derived from its name: S-1-15-2, then the first
 * 28 bytes of the SHA-256 of the lower-case name (UTF-16) as 7 numbers.
 * Chromium's sandbox derives it (Evernote imports it).
 */

#include "precomp.h"
#include <wincrypt.h>

HRESULT
WINAPI
DeriveAppContainerSidFromAppContainerName(
    _In_ PCWSTR pszAppContainerName,
    _Outptr_ PSID *ppsidAppContainerSid)
{
    static SID_IDENTIFIER_AUTHORITY AppPackageAuthority = { { 0, 0, 0, 0, 0, 15 } };
    WCHAR szName[65];
    DWORD Hash[8], HashSize = sizeof(Hash);
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    SIZE_T Length;
    BOOL Success;

    if (!pszAppContainerName || !ppsidAppContainerSid)
        return E_INVALIDARG;
    *ppsidAppContainerSid = NULL;

    /* 1 to 64 characters */
    Length = wcslen(pszAppContainerName);
    if (Length < 1 || Length >= ARRAYSIZE(szName))
        return E_INVALIDARG;
    wcscpy(szName, pszAppContainerName);
    CharLowerW(szName);

    Success = CryptAcquireContextW(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) &&
              CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash) &&
              CryptHashData(hHash, (const BYTE *)szName, (DWORD)(Length * sizeof(WCHAR)), 0) &&
              CryptGetHashParam(hHash, HP_HASHVAL, (BYTE *)Hash, &HashSize, 0);
    if (hHash) CryptDestroyHash(hHash);
    if (hProv) CryptReleaseContext(hProv, 0);
    if (!Success)
        return HRESULT_FROM_WIN32(GetLastError());

    /* SECURITY_APP_PACKAGE_BASE_RID (2), then the hash (freed with FreeSid) */
    if (!AllocateAndInitializeSid(&AppPackageAuthority, 8, 2,
                                  Hash[0], Hash[1], Hash[2], Hash[3], Hash[4], Hash[5], Hash[6],
                                  ppsidAppContainerSid))
        return HRESULT_FROM_WIN32(GetLastError());
    return S_OK;
}

/*
 * ReactOS does not isolate AppContainers, so there is no profile to keep:
 * creating one gives its SID (what callers use: Chromium's sandbox, in OBS's
 * browser source), deleting one has nothing to do.
 */
HRESULT
WINAPI
CreateAppContainerProfile(
    _In_ PCWSTR pszAppContainerName,
    _In_ PCWSTR pszDisplayName,
    _In_ PCWSTR pszDescription,
    _In_reads_opt_(dwCapabilityCount) PSID_AND_ATTRIBUTES pCapabilities,
    _In_ DWORD dwCapabilityCount,
    _Outptr_ PSID *ppSidAppContainerSid)
{
    if (!pszDisplayName || !pszDescription || (dwCapabilityCount && !pCapabilities))
        return E_INVALIDARG;
    return DeriveAppContainerSidFromAppContainerName(pszAppContainerName, ppSidAppContainerSid);
}

HRESULT
WINAPI
DeleteAppContainerProfile(
    _In_ PCWSTR pszAppContainerName)
{
    if (!pszAppContainerName || !*pszAppContainerName)
        return E_INVALIDARG;
    return S_OK;
}
