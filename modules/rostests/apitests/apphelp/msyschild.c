/*
 * PROJECT:     ReactOS api tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Child of the MsysShim test: a program with msvcrt among its
 *              static imports, as MSYS2's bash.exe has (via user32)
 */

#include <windows.h>
#include <stdio.h>

int wmain(void)
{
    GetSystemMetrics(SM_CXSCREEN);
    printf("msyschild ok\n");
    return 42;
}
