/*
 * PROJECT:     ReactOS Media Foundation
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Media Foundation player (mfplay.dll), not available yet (see mfplat.c)
 *
 * LibreOffice imports MFPCreateMediaPlayer for its media playback, and did not
 * start without this DLL. No player can be created: it has no sound or video.
 */

#include <windef.h>
#include <winbase.h>
#include <objbase.h>

#define MF_E_PLATFORM_NOT_INITIALIZED ((HRESULT)0xC00D36B0L)

HRESULT WINAPI MFPCreateMediaPlayer(const WCHAR *Url, BOOL StartPlayback, DWORD Options,
                                    void *Callback, HWND Window, void **Player)
{
    if (Player) *Player = NULL;
    return MF_E_PLATFORM_NOT_INITIALIZED;
}
