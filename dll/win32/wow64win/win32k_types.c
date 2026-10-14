/*
 * PROJECT:     ReactOS WoW64
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     The headers that gen_win32k_thunks.py reads (after the C
 *              preprocessor, with its line markers); also the headers of
 *              wow64win.dll
 */

#ifndef WIN32K_TYPES_INCLUDED
#define WIN32K_TYPES_INCLUDED
#include <stdio.h>
#include <stdarg.h>
#define WIN32_NO_STATUS
#define _INC_WINDOWS
#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <winreg.h>
#include <winuser.h>
#include <imm.h>
#include <immdev.h>
#include <imm32_undoc.h>
#include <ddeml.h>
#include <dde.h>
#include <undocuser.h>
#include <winnls.h>
#include <objbase.h>
#include <winddi.h>
#include <prntfont.h>
#include <ddrawi.h>
#include <ddrawgdi.h>
#include <d3dkmthk.h>
#define NTOS_MODE_USER
#include <ndk/ntndk.h>
#include <ntusrtyp.h>
#include <ntuser.h>
#include <callback.h>
#include <ntgdityp.h>
#include <ntgdi.h>
#include <ntgdibad.h>
#include <ntgdihdl.h>

#endif
