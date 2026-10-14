/*
 * PROJECT:     ReactOS WoW64
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     wow64win.dll: the win32k system calls of 32-bit programs
 */

#pragma once

#include "win32k_types.c"
#include <string.h>

/* Memory freed at the end of the current system call (wow64.dll) */
void * WINAPI Wow64AllocateTemp( SIZE_T size );

/* The arguments of a 32-bit system call (as in Wine's wow64) */
static inline ULONG get_ulong( UINT **args ) { return *(*args)++; }
static inline HANDLE get_handle( UINT **args ) { return LongToHandle( *(*args)++ ); }
static inline void *get_ptr( UINT **args ) { return ULongToPtr( *(*args)++ ); }
static inline ULONG64 get_ulong64( UINT **args )
{
    ULONG64 ret = *(ULONG64 *)*args;
    *args += 2;
    return ret;
}

typedef struct
{
    USHORT Length;
    USHORT MaximumLength;
    ULONG Buffer;
} UNICODE_STRING32_W;

/* Give a converted structure back to the 32-bit caller: only if it changed, so
 * that input structures in read-only memory are never written (their padding
 * is kept too) */
#define WRITE_BACK( name, dst32, src64 ) \
    do { \
        name##_32 tmp_; \
        tmp_ = *(dst32); \
        name##_64to32( &tmp_, (src64) ); \
        if (memcmp( &tmp_, (dst32), sizeof(tmp_) )) *(dst32) = tmp_; \
    } while (0)
