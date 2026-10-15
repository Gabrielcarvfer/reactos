/*
 * PROJECT:     ReactOS C runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The C99 functions of ucrtbase that are not taken from msun
 */

#include <math.h>
#include "math_private.h"

double __cdecl lgamma(double x)
{
    int sign;
    return __msun_lgamma_r(x, &sign);
}

float __cdecl lgammaf(float x)
{
    int sign;
    return __msun_lgammaf_r(x, &sign);
}

/* rint without the inexact exception (which nothing here reports) */
double __cdecl nearbyint(double x)
{
    return rint(x);
}

float __cdecl nearbyintf(float x)
{
    return rintf(x);
}

double __cdecl nan(const char *tag)
{
    (void)tag;
    return __builtin_nan("");
}

float __cdecl nanf(const char *tag)
{
    (void)tag;
    return __builtin_nanf("");
}
