/*
 * PROJECT:     ReactOS WoW64
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     The headers that gen_csr_thunks.py reads (after the C
 *              preprocessor); compiled for i386 with csr_thunks.h, it checks
 *              the 32-bit layouts of the generated header
 */

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winbase_undoc.h>
#include <wingdi.h>
#include <winreg.h>
#include <wincon.h>
#include <wincon_undoc.h>
#include <winuser.h>
#define NTDDI_VERSION NTDDI_WS03SP1
#define NTOS_MODE_USER
#include <ndk/ntndk.h>
#include <csr/csr.h>
#include <win/base.h>
#include <win/basemsg.h>
#include <win/console.h>
#include <win/conmsg.h>
#ifndef _WIN64
typedef struct { char x[0x124]; } USERCONNECT;
#endif
#include <win/winmsg.h>

#ifndef _WIN64
struct csr_field { ULONG off32, off64, kind; };
struct csr_api { ULONG size32, size64; void *to64, *to32; const struct csr_field *fields; };
#define CSR_FIELD_PTR 0
#define CSR_FIELD_HANDLE 1
#define CSR_FIELD_PTRINT 2
#include "csr_thunks.h"
#endif
