/*
 * PROJECT:     NetAPI DLL
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     WebDAV path conversion (Vista)
 *
 * A WebDAV share's UNC path names the server, then optionally "@SSL" (HTTPS)
 * and "@<port>": \\server@SSL@8443\DavWWWRoot\folder is
 * https://server:8443/folder. LibreOffice imports this.
 */

#include "netapi32.h"

WINE_DEFAULT_DEBUG_CHANNEL(netapi32);

DWORD
WINAPI
DavGetHTTPFromUNCPath(
    _In_ LPCWSTR UncPath,
    _Out_writes_opt_(*lpSize) LPWSTR HttpPath,
    _Inout_ LPDWORD lpSize)
{
    static const WCHAR DavRoot[] = L"DavWWWRoot";
    const WCHAR *Host, *HostEnd, *Path, *Tag;
    BOOL Ssl = FALSE;
    ULONG Port = 0;
    WCHAR PortText[16] = L"";
    DWORD Needed;
    WCHAR *Out;

    TRACE("(%s %p %p)\n", debugstr_w(UncPath), HttpPath, lpSize);

    if (!UncPath || !lpSize)
        return ERROR_INVALID_PARAMETER;
    if (UncPath[0] != L'\\' || UncPath[1] != L'\\')
        return ERROR_INVALID_PARAMETER;

    /* The server name, then its "@..." parts */
    Host = UncPath + 2;
    for (HostEnd = Host; *HostEnd && *HostEnd != L'\\' && *HostEnd != L'@'; HostEnd++);
    if (HostEnd == Host)
        return ERROR_BAD_NET_NAME;
    for (Tag = HostEnd; *Tag == L'@';)
    {
        const WCHAR *TagEnd;

        Tag++;
        for (TagEnd = Tag; *TagEnd && *TagEnd != L'\\' && *TagEnd != L'@'; TagEnd++);
        if (TagEnd - Tag == 3 && !_wcsnicmp(Tag, L"SSL", 3))
        {
            Ssl = TRUE;
        }
        else
        {
            WCHAR *End;
            Port = wcstoul(Tag, &End, 10);
            if (End != TagEnd || !Port || Port > 65535)
                return ERROR_INVALID_PARAMETER;
        }
        Tag = TagEnd;
    }

    /* The path: without the DavWWWRoot share, with forward slashes */
    Path = Tag;
    if (*Path == L'\\')
    {
        Path++;
        if (!_wcsnicmp(Path, DavRoot, ARRAYSIZE(DavRoot) - 1) &&
            (!Path[ARRAYSIZE(DavRoot) - 1] || Path[ARRAYSIZE(DavRoot) - 1] == L'\\'))
        {
            Path += ARRAYSIZE(DavRoot) - 1;
            if (*Path == L'\\') Path++;
        }
    }

    if (Port)
        _snwprintf(PortText, ARRAYSIZE(PortText) - 1, L":%lu", Port);

    /* "http[s]://" host [":" port] "/" path, and the terminating null */
    Needed = (Ssl ? 8 : 7) + (DWORD)(HostEnd - Host) + (DWORD)wcslen(PortText) + 1 + (DWORD)wcslen(Path) + 1;
    if (!HttpPath || *lpSize < Needed)
    {
        *lpSize = Needed;
        return ERROR_INSUFFICIENT_BUFFER;
    }

    Out = HttpPath;
    wcscpy(Out, Ssl ? L"https://" : L"http://");
    Out += wcslen(Out);
    memcpy(Out, Host, (HostEnd - Host) * sizeof(WCHAR));
    Out += HostEnd - Host;
    wcscpy(Out, PortText);
    Out += wcslen(Out);
    *Out++ = L'/';
    for (; *Path; Path++)
        *Out++ = (*Path == L'\\') ? L'/' : *Path;
    *Out = UNICODE_NULL;

    *lpSize = Needed;
    return ERROR_SUCCESS;
}
