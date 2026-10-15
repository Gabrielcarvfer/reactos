/*
 * The parts of FreeBSD's msun math_private.h that the C99 functions taken
 * from msun use (word access to IEEE 754 numbers), for ucrtbase.
 * The word access macros follow the original, Copyright (C) 1993 by Sun
 * Microsystems, Inc.: "Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice is preserved."
 */
#pragma once

#include <stdint.h>
#include <float.h>
#include <math.h>

typedef uint32_t u_int32_t;
typedef uint64_t u_int64_t;

typedef union
{
    double value;
    struct { u_int32_t lsw, msw; } parts;
    struct { u_int64_t w; } xparts;
} ieee_double_shape_type;

#define EXTRACT_WORDS(ix0, ix1, d) do { ieee_double_shape_type ew_u; ew_u.value = (d); \
    (ix0) = ew_u.parts.msw; (ix1) = ew_u.parts.lsw; } while (0)
#define EXTRACT_WORD64(ix, d) do { ieee_double_shape_type ew_u; ew_u.value = (d); (ix) = ew_u.xparts.w; } while (0)
#define GET_HIGH_WORD(i, d) do { ieee_double_shape_type gh_u; gh_u.value = (d); (i) = gh_u.parts.msw; } while (0)
#define GET_LOW_WORD(i, d) do { ieee_double_shape_type gl_u; gl_u.value = (d); (i) = gl_u.parts.lsw; } while (0)
#define INSERT_WORDS(d, ix0, ix1) do { ieee_double_shape_type iw_u; iw_u.parts.msw = (ix0); \
    iw_u.parts.lsw = (ix1); (d) = iw_u.value; } while (0)
#define INSERT_WORD64(d, ix) do { ieee_double_shape_type iw_u; iw_u.xparts.w = (ix); (d) = iw_u.value; } while (0)
#define SET_HIGH_WORD(d, v) do { ieee_double_shape_type sh_u; sh_u.value = (d); sh_u.parts.msw = (v); \
    (d) = sh_u.value; } while (0)
#define SET_LOW_WORD(d, v) do { ieee_double_shape_type sl_u; sl_u.value = (d); sl_u.parts.lsw = (v); \
    (d) = sl_u.value; } while (0)

typedef union
{
    float value;
    u_int32_t word;
} ieee_float_shape_type;

#define GET_FLOAT_WORD(i, d) do { ieee_float_shape_type gf_u; gf_u.value = (d); (i) = gf_u.word; } while (0)
#define SET_FLOAT_WORD(d, i) do { ieee_float_shape_type sf_u; sf_u.word = (i); (d) = sf_u.value; } while (0)

/* Their names in this CRT */
#define scalb _scalb
#define finite _finite

/* On i386 the x87 keeps more precision than the type: store to memory to
 * round to it (as the original does; long double is double here, so
 * always) */
#ifdef __i386__
#define STRICT_ASSIGN(type, lval, rval) do { volatile type __lval; \
    __lval = (rval); (lval) = __lval; } while (0)
#else
#define STRICT_ASSIGN(type, lval, rval) ((lval) = (rval))
#endif

/* The functions defined under their msun internal names */
#define __ieee754_acosh acosh
#define __ieee754_acoshf acoshf
#define __ieee754_atanh atanh
#define __ieee754_atanhf atanhf
#define __ieee754_remainder remainder
#define __ieee754_remainderf remainderf
#define __ieee754_lgamma_r __msun_lgamma_r
#define __ieee754_lgammaf_r __msun_lgammaf_r
/* and the ones they use */
#define __ieee754_log log
#define __ieee754_logf logf
#define __ieee754_exp exp
#define __ieee754_expf expf
#define __ieee754_sqrt sqrt
#define __ieee754_sqrtf sqrtf
#define __ieee754_fmod fmod
#define __ieee754_fmodf fmodf
#define __ieee754_pow pow
#define __kernel_sin(x, y, iy) sin((x) + (y))
#define __kernel_cos(x, y) cos((x) + (y))
#define __kernel_sindf(x) ((float)sin(x))
#define __kernel_cosdf(x) ((float)cos(x))

double __msun_lgamma_r(double x, int *signgamp);
float __msun_lgammaf_r(float x, int *signgamp);
