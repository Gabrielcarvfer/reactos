/*
 * PROJECT:     ReactOS UCRT
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     C99 floating-point environment (fenv.h), copysign, modff
 * COPYRIGHT:   Based on Wine's msvcrt/math.c (Copyright 2000 Jon Griffiths and
 *              the Wine project authors); adapted 2026 Gabriel Ferreira
 */

/*
 * These were stubs printing a warning on every call: Blender calls
 * fegetround/fegetenv/fesetenv millions of times (911000 debug lines, the
 * program crawled). fenv_t is encoded as Microsoft's UCRT does (the encoding
 * Wine found), so FE_DFL_ENV of the Microsoft headers works.
 */

#include <fenv.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <xmmintrin.h>

/* 32-bit: the SSE intrinsics need SSE enabled for the function using them */
#if defined(_M_IX86) && defined(__GNUC__) && !defined(__clang__)
#define SSE_FUNC __attribute__((target("sse")))
#else
#define SSE_FUNC
#endif

enum fenv_masks
{
    FENV_X_INVALID = 0x00100010,
    FENV_X_DENORMAL = 0x00200020,
    FENV_X_ZERODIVIDE = 0x00080008,
    FENV_X_OVERFLOW = 0x00040004,
    FENV_X_UNDERFLOW = 0x00020002,
    FENV_X_INEXACT = 0x00010001,
    FENV_X_AFFINE = 0x00004000,
    FENV_X_UP = 0x00800200,
    FENV_X_DOWN = 0x00400100,
    FENV_X_24 = 0x00002000,
    FENV_X_53 = 0x00001000,
    FENV_Y_INVALID = 0x10000010,
    FENV_Y_DENORMAL = 0x20000020,
    FENV_Y_ZERODIVIDE = 0x08000008,
    FENV_Y_OVERFLOW = 0x04000004,
    FENV_Y_UNDERFLOW = 0x02000002,
    FENV_Y_INEXACT = 0x01000001,
    FENV_Y_UP = 0x80000200,
    FENV_Y_DOWN = 0x40000100,
    FENV_Y_FLUSH = 0x00000400,
    FENV_Y_FLUSH_SAVE = 0x00000800
};

/* Encodes x87 (x) and SSE (y) control or status words */
static unsigned long fenv_encode(unsigned int x, unsigned int y)
{
    unsigned long ret = 0;

#ifdef _M_IX86
    if (x & _EM_INVALID) ret |= FENV_X_INVALID;
    if (x & _EM_DENORMAL) ret |= FENV_X_DENORMAL;
    if (x & _EM_ZERODIVIDE) ret |= FENV_X_ZERODIVIDE;
    if (x & _EM_OVERFLOW) ret |= FENV_X_OVERFLOW;
    if (x & _EM_UNDERFLOW) ret |= FENV_X_UNDERFLOW;
    if (x & _EM_INEXACT) ret |= FENV_X_INEXACT;
    if (x & _IC_AFFINE) ret |= FENV_X_AFFINE;
    if (x & _RC_UP) ret |= FENV_X_UP;
    if (x & _RC_DOWN) ret |= FENV_X_DOWN;
    if (x & _PC_24) ret |= FENV_X_24;
    if (x & _PC_53) ret |= FENV_X_53;
#else
    (void)x;
#endif
    if (y & _EM_INVALID) ret |= FENV_Y_INVALID;
    if (y & _EM_DENORMAL) ret |= FENV_Y_DENORMAL;
    if (y & _EM_ZERODIVIDE) ret |= FENV_Y_ZERODIVIDE;
    if (y & _EM_OVERFLOW) ret |= FENV_Y_OVERFLOW;
    if (y & _EM_UNDERFLOW) ret |= FENV_Y_UNDERFLOW;
    if (y & _EM_INEXACT) ret |= FENV_Y_INEXACT;
    if (y & _RC_UP) ret |= FENV_Y_UP;
    if (y & _RC_DOWN) ret |= FENV_Y_DOWN;
    if (y & _DN_FLUSH) ret |= FENV_Y_FLUSH;
    if (y & _DN_FLUSH_OPERANDS_SAVE_RESULTS) ret |= FENV_Y_FLUSH_SAVE;
    return ret;
}

static void fenv_decode(unsigned long enc, unsigned int *x, unsigned int *y)
{
    *x = *y = 0;
    if ((enc & FENV_X_INVALID) == FENV_X_INVALID) *x |= _EM_INVALID;
    if ((enc & FENV_X_DENORMAL) == FENV_X_DENORMAL) *x |= _EM_DENORMAL;
    if ((enc & FENV_X_ZERODIVIDE) == FENV_X_ZERODIVIDE) *x |= _EM_ZERODIVIDE;
    if ((enc & FENV_X_OVERFLOW) == FENV_X_OVERFLOW) *x |= _EM_OVERFLOW;
    if ((enc & FENV_X_UNDERFLOW) == FENV_X_UNDERFLOW) *x |= _EM_UNDERFLOW;
    if ((enc & FENV_X_INEXACT) == FENV_X_INEXACT) *x |= _EM_INEXACT;
    if ((enc & FENV_X_AFFINE) == FENV_X_AFFINE) *x |= _IC_AFFINE;
    if ((enc & FENV_X_UP) == FENV_X_UP) *x |= _RC_UP;
    if ((enc & FENV_X_DOWN) == FENV_X_DOWN) *x |= _RC_DOWN;
    if ((enc & FENV_X_24) == FENV_X_24) *x |= _PC_24;
    if ((enc & FENV_X_53) == FENV_X_53) *x |= _PC_53;

    if ((enc & FENV_Y_INVALID) == FENV_Y_INVALID) *y |= _EM_INVALID;
    if ((enc & FENV_Y_DENORMAL) == FENV_Y_DENORMAL) *y |= _EM_DENORMAL;
    if ((enc & FENV_Y_ZERODIVIDE) == FENV_Y_ZERODIVIDE) *y |= _EM_ZERODIVIDE;
    if ((enc & FENV_Y_OVERFLOW) == FENV_Y_OVERFLOW) *y |= _EM_OVERFLOW;
    if ((enc & FENV_Y_UNDERFLOW) == FENV_Y_UNDERFLOW) *y |= _EM_UNDERFLOW;
    if ((enc & FENV_Y_INEXACT) == FENV_Y_INEXACT) *y |= _EM_INEXACT;
    if ((enc & FENV_Y_UP) == FENV_Y_UP) *y |= _RC_UP;
    if ((enc & FENV_Y_DOWN) == FENV_Y_DOWN) *y |= _RC_DOWN;
    if ((enc & FENV_Y_FLUSH) == FENV_Y_FLUSH) *y |= _DN_FLUSH;
    if ((enc & FENV_Y_FLUSH_SAVE) == FENV_Y_FLUSH_SAVE) *y |= _DN_FLUSH_OPERANDS_SAVE_RESULTS;
}

/* The words of this architecture, encoded */
static unsigned long fenv_encode_word(unsigned int w)
{
#ifdef _M_IX86
    return fenv_encode(w, w);
#else
    return fenv_encode(0, w);
#endif
}

/* Sets the SSE exception flags to exactly the _SW_* flags given */
static SSE_FUNC void set_sse_status(unsigned int sw)
{
    unsigned int csr = _mm_getcsr() & ~0x3f;
    if (sw & _SW_INVALID) csr |= 0x1;
    if (sw & _SW_DENORMAL) csr |= 0x2;
    if (sw & _SW_ZERODIVIDE) csr |= 0x4;
    if (sw & _SW_OVERFLOW) csr |= 0x8;
    if (sw & _SW_UNDERFLOW) csr |= 0x10;
    if (sw & _SW_INEXACT) csr |= 0x20;
    _mm_setcsr(csr);
}

/* Default environment: all exceptions masked, round to nearest, no flags */
static SSE_FUNC void fenv_reset(void)
{
#if defined(__GNUC__) || defined(__clang__)
#ifdef _M_IX86
    const unsigned short x86_cw = 0x27f;
    __asm__ __volatile__("fninit; fldcw %0" : : "m" (x86_cw));
#endif
#endif
    _mm_setcsr(0x1f80);
}

int __cdecl fegetenv(fenv_t *env)
{
    env->_Fe_ctl = fenv_encode_word(_control87(0, 0));
    env->_Fe_stat = fenv_encode_word(_statusfp());
    return 0;
}

int __cdecl fesetenv(const fenv_t *env)
{
    unsigned int x, y, sx, sy;

    if (!env->_Fe_ctl && !env->_Fe_stat)
    {
        fenv_reset();
        return 0;
    }

    fenv_decode(env->_Fe_ctl, &x, &y);
    fenv_decode(env->_Fe_stat, &sx, &sy);
#ifdef _M_IX86
    _control87(x | y, _MCW_EM | _MCW_RC | _MCW_PC | _MCW_IC);
    sy |= sx;
#else
    _control87(y, _MCW_EM | _MCW_RC | _MCW_DN);
#endif
    /* Status: clear all, then set the ones asked for (on SSE) */
    _clearfp();
    if (sy) set_sse_status(sy);
    return 0;
}

int __cdecl feholdexcept(fenv_t *env)
{
    fegetenv(env);
    return feclearexcept(FE_ALL_EXCEPT);
}

int __cdecl fetestexcept(int flags)
{
    return _statusfp() & flags & FE_ALL_EXCEPT;
}

int __cdecl feclearexcept(int flags)
{
    fenv_t env;

    fegetenv(&env);
    flags &= FE_ALL_EXCEPT;
    env._Fe_stat &= ~fenv_encode(flags, flags);
    return fesetenv(&env);
}

int __cdecl fegetexceptflag(fexcept_t *status, int excepts)
{
    *status = fenv_encode_word(_statusfp() & excepts);
    return 0;
}

int __cdecl fesetexceptflag(const fexcept_t *status, int excepts)
{
    fenv_t env;

    excepts &= FE_ALL_EXCEPT;
    if (!excepts)
        return 0;

    fegetenv(&env);
    env._Fe_stat &= ~fenv_encode(excepts, excepts);
    env._Fe_stat |= *status & fenv_encode(excepts, excepts);
    return fesetenv(&env);
}

int __cdecl fegetround(void)
{
    return _controlfp(0, 0) & _MCW_RC;
}

int __cdecl fesetround(int round_mode)
{
    if (round_mode & (~_MCW_RC))
        return 1;
    _controlfp(round_mode, _MCW_RC);
    return 0;
}

double __cdecl copysign(double x, double y)
{
    union { double f; uint64_t i; } ux = { x }, uy = { y };
    ux.i = (ux.i & ~(1ull << 63)) | (uy.i & (1ull << 63));
    return ux.f;
}

float __cdecl copysignf(float x, float y)
{
    union { float f; uint32_t i; } ux = { x }, uy = { y };
    ux.i = (ux.i & ~(1u << 31)) | (uy.i & (1u << 31));
    return ux.f;
}

/* Splitting a float through double is exact (Blender calls it).
   x86 has no modff export: there the CRT header defines it inline */
#ifndef _M_IX86
float __cdecl modff(float x, float *iptr)
{
    double ip, frac = modf(x, &ip);
    *iptr = (float)ip;
    return (float)frac;
}
#endif
