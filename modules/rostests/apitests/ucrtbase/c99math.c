/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for the C99 math functions of ucrtbase.dll
 *
 * Programs built with the Universal CRT (Qt 6 among them) call these
 * functions: rounding to integers, the inverse hyperbolic functions, cbrt,
 * erf, the gamma functions, fmod / remainder / remquo and nan. They are
 * looked up with GetProcAddress, so the compiler cannot replace a call with
 * its own built-in version.
 */

#include <apitest.h>
#include <windows.h>

typedef double (__cdecl *PFN_D_D)(double);
typedef float (__cdecl *PFN_F_F)(float);
typedef long long (__cdecl *PFN_LL_D)(double);
typedef double (__cdecl *PFN_D_DD)(double, double);
typedef double (__cdecl *PFN_D_DDI)(double, double, int *);
typedef double (__cdecl *PFN_D_S)(const char *);

static HMODULE hCrt;

static double AbsD(double x) { return x < 0 ? -x : x; }
static float AbsF(float x) { return x < 0 ? -x : x; }

static void CheckD(const char *Name, double Arg, double Expected, double Tolerance)
{
    PFN_D_D Func = (PFN_D_D)GetProcAddress(hCrt, Name);
    double Result, Scale;

    ok(Func != NULL, "%s is not exported\n", Name);
    if (!Func)
        return;
    Result = Func(Arg);
    Scale = AbsD(Expected) > 1 ? AbsD(Expected) : 1;
    ok(AbsD(Result - Expected) <= Tolerance * Scale,
       "%s(%.17g) = %.17g, expected %.17g\n", Name, Arg, Result, Expected);
}

static void CheckF(const char *Name, float Arg, float Expected, float Tolerance)
{
    PFN_F_F Func = (PFN_F_F)GetProcAddress(hCrt, Name);
    float Result, Scale;

    ok(Func != NULL, "%s is not exported\n", Name);
    if (!Func)
        return;
    Result = Func(Arg);
    Scale = AbsF(Expected) > 1 ? AbsF(Expected) : 1;
    ok(AbsF(Result - Expected) <= Tolerance * Scale,
       "%s(%.9g) = %.9g, expected %.9g\n", Name, (double)Arg, (double)Result, (double)Expected);
}

static void CheckLL(const char *Name, double Arg, long long Expected)
{
    PFN_LL_D Func = (PFN_LL_D)GetProcAddress(hCrt, Name);
    long long Result;

    ok(Func != NULL, "%s is not exported\n", Name);
    if (!Func)
        return;
    Result = Func(Arg);
    ok(Result == Expected, "%s(%.17g) = %I64d, expected %I64d\n", Name, Arg, Result, Expected);
}

START_TEST(c99math)
{
    PFN_D_DD pfmod, premainder;
    PFN_D_DDI premquo;
    PFN_D_S pnan;
    double Result;
    int Quotient;

    hCrt = GetModuleHandleW(L"ucrtbase.dll");
    if (!hCrt)
    {
        skip("ucrtbase.dll is not loaded\n");
        return;
    }

    CheckLL("llround", 3779.527559, 3780);
    CheckLL("llround", -2.5, -3);
    CheckLL("llrint", 2.5, 2);
    CheckD("rint", -1.5, -2, 0);
    CheckD("nearbyint", 2.5, 2, 0);
    CheckD("acosh", 2, 1.3169578969248166, 1e-15);
    CheckD("asinh", 1, 0.88137358701954305, 1e-15);
    CheckD("atanh", 0.5, 0.54930614433405489, 1e-15);
    CheckD("cbrt", 27, 3, 1e-15);
    CheckD("erf", 1, 0.84270079294971487, 1e-15);
    CheckD("erfc", 2, 0.0046777349810472658, 1e-14);
    CheckD("lgamma", 10, 12.801827480081469, 1e-14);
    CheckD("tgamma", 5, 24, 1e-14);
    CheckD("tgamma", 0.5, 1.7724538509055159, 1e-14);
    CheckD("log1p", 1e-10, 9.9999999995e-11, 1e-12);
    CheckF("cbrtf", 8, 2, 1e-6f);
    CheckF("erff", 0.5f, 0.520499878f, 1e-6f);
    CheckF("lgammaf", 3.5f, 1.20097364f, 1e-5f);
    CheckF("tgammaf", 4.5f, 11.6317284f, 1e-5f);

    pfmod = (PFN_D_DD)GetProcAddress(hCrt, "fmod");
    ok(pfmod != NULL, "fmod is not exported\n");
    if (pfmod)
    {
        Result = pfmod(7, 4);
        ok(Result == 3, "fmod(7, 4) = %.17g, expected 3\n", Result);
        Result = pfmod(-7.5, 2);
        ok(Result == -1.5, "fmod(-7.5, 2) = %.17g, expected -1.5\n", Result);
        Result = pfmod(1e10, 3);
        ok(Result == 1, "fmod(1e10, 3) = %.17g, expected 1\n", Result);
    }

    premainder = (PFN_D_DD)GetProcAddress(hCrt, "remainder");
    ok(premainder != NULL, "remainder is not exported\n");
    if (premainder)
    {
        Result = premainder(7, 2);
        ok(Result == -1, "remainder(7, 2) = %.17g, expected -1\n", Result);
    }

    premquo = (PFN_D_DDI)GetProcAddress(hCrt, "remquo");
    ok(premquo != NULL, "remquo is not exported\n");
    if (premquo)
    {
        Quotient = 0;
        Result = premquo(10, 3, &Quotient);
        ok(Result == 1, "remquo(10, 3) = %.17g, expected 1\n", Result);
        ok((Quotient & 7) == 3, "remquo(10, 3): quotient %d, expected 3 in the low bits\n", Quotient);
    }

    pnan = (PFN_D_S)GetProcAddress(hCrt, "nan");
    ok(pnan != NULL, "nan is not exported\n");
    if (pnan)
    {
        Result = pnan("");
        ok(Result != Result, "nan(\"\") = %.17g, expected a NaN\n", Result);
    }
}
