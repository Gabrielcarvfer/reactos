/*
 * PROJECT:     ReactOS Secure Attention Sequence library
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     SendSAS (Windows 7)
 *
 * Remote desktop programs (RustDesk, TightVNC) call SendSAS to bring up the
 * Ctrl+Alt+Del screen for a remote user. ReactOS's Winlogon has no way to be
 * asked for it from outside yet, so the call does nothing; the programs only
 * need the DLL to load.
 */

#include <windef.h>
#include <winbase.h>

VOID
WINAPI
SendSAS(
    _In_ BOOL AsUser)
{
    UNREFERENCED_PARAMETER(AsUser);
    OutputDebugStringA("SendSAS: not supported by Winlogon yet\n");
}
