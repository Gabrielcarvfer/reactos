/*
 * PROJECT:     ReactOS Event Utility
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     A minimal wevtutil: installing and uninstalling event manifests
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 *
 * Installers register the event providers of their programs with
 * "wevtutil im <manifest>" (and remove them with "um"), and fail when that
 * fails (Node.js). ReactOS has no Windows Event Log of that kind to register
 * them with: accept these commands, and refuse the others.
 */

#include <stdio.h>
#include <windef.h>
#include <wchar.h>

int wmain(int argc, WCHAR *argv[])
{
    if (argc >= 2 &&
        (!_wcsicmp(argv[1], L"im") || !_wcsicmp(argv[1], L"install-manifest") ||
         !_wcsicmp(argv[1], L"um") || !_wcsicmp(argv[1], L"uninstall-manifest")))
    {
        if (argc < 3)
        {
            fwprintf(stderr, L"The manifest file is missing.\n");
            return 87; /* ERROR_INVALID_PARAMETER */
        }
        return 0;
    }

    fwprintf(stderr, L"wevtutil: only the im and um commands are available.\n");
    return 50; /* ERROR_NOT_SUPPORTED */
}
