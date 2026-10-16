/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     Tests for the ICU character set converters of icuuc.dll
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * Qt 6 (QStringConverter) loads icuuc.dll (Windows 10 has it) and uses the
 * ucnv_* converters: windows-1252, UTF-8 split across calls, an invalid
 * byte handled by its own callback, Shift_JIS.
 */

#include <apitest.h>

typedef int UErrorCode;
typedef void *UConv;
typedef struct
{
    unsigned short size;
    signed char flush;
    void *converter;
    const char *source;
    const char *sourceLimit;
    WCHAR *target;
    const WCHAR *targetLimit;
    int *offsets;
} ToUArgs;
typedef void (__cdecl *ToUCb)(const void *, ToUArgs *, const char *, int, int, UErrorCode *);

static UConv (__cdecl *p_ucnv_open)(const char *, UErrorCode *);
static void (__cdecl *p_ucnv_close)(UConv);
static void (__cdecl *p_ucnv_toUnicode)(UConv, WCHAR **, const WCHAR *, const char **, const char *, int *, signed char, UErrorCode *);
static void (__cdecl *p_ucnv_fromUnicode)(UConv, char **, const char *, const WCHAR **, const WCHAR *, int *, signed char, UErrorCode *);
static int (__cdecl *p_ucnv_toUCountPending)(UConv, UErrorCode *);
static const char *(__cdecl *p_ucnv_getName)(UConv, UErrorCode *);
static const char *(__cdecl *p_ucnv_getStandardName)(const char *, const char *, UErrorCode *);
static int (__cdecl *p_ucnv_countAvailable)(void);
static void (__cdecl *p_ucnv_setToUCallBack)(UConv, ToUCb, const void *, ToUCb *, const void **, UErrorCode *);
static void (__cdecl *p_ucnv_cbToUWriteUChars)(ToUArgs *, const WCHAR *, int, int, UErrorCode *);

static int g_Invalid;

/* Qt's callback: an invalid sequence becomes NUL and is counted */
static void __cdecl NulSubstitute(const void *ctx, ToUArgs *args, const char *units, int len, int reason, UErrorCode *err)
{
    if (reason <= 2)
    {
        WCHAR c = 0;
        *err = 0;
        p_ucnv_cbToUWriteUChars(args, &c, 1, 0, err);
        g_Invalid += len;
    }
}

START_TEST(Converters)
{
    HMODULE hIcu;
    UErrorCode err = 0;
    UConv c;
    WCHAR wbuf[64], *wt;
    char buf[64], *t;
    const char *s, *name;
    const WCHAR *ws;
    static const WCHAR Unicode1252[] = { 'a', 0x20AC, 'z', 0x4E00 };

    hIcu = LoadLibraryW(L"icuuc.dll");
    if (!hIcu)
    {
        skip("icuuc.dll not available (%lu)\n", GetLastError());
        return;
    }

#define GET(name) *(FARPROC *)&p_##name = GetProcAddress(hIcu, #name); \
    ok(p_##name != NULL, "%s not exported\n", #name)
    GET(ucnv_open);
    GET(ucnv_close);
    GET(ucnv_toUnicode);
    GET(ucnv_fromUnicode);
    GET(ucnv_toUCountPending);
    GET(ucnv_getName);
    GET(ucnv_getStandardName);
    GET(ucnv_countAvailable);
    GET(ucnv_setToUCallBack);
    GET(ucnv_cbToUWriteUChars);
#undef GET
    if (!p_ucnv_open || !p_ucnv_close || !p_ucnv_toUnicode || !p_ucnv_fromUnicode ||
        !p_ucnv_toUCountPending || !p_ucnv_getName || !p_ucnv_getStandardName ||
        !p_ucnv_countAvailable || !p_ucnv_setToUCallBack || !p_ucnv_cbToUWriteUChars)
    {
        skip("icuuc.dll lacks converter functions\n");
        FreeLibrary(hIcu);
        return;
    }

    /* windows-1252: 0x80 is the euro sign, both ways */
    c = p_ucnv_open("cp1252", &err);
    ok(c != NULL && err == 0, "ucnv_open(cp1252): %p, error %d\n", c, err);
    if (c)
    {
        s = "a\x80z";
        wt = wbuf;
        p_ucnv_toUnicode(c, &wt, wbuf + 64, &s, s + 3, NULL, 1, &err);
        ok(err == 0 && wt - wbuf == 3 && wbuf[1] == 0x20AC,
           "cp1252 to Unicode: error %d, %d units, [1] = 0x%04x\n", err, (int)(wt - wbuf), wbuf[1]);

        ws = Unicode1252;
        t = buf;
        p_ucnv_fromUnicode(c, &t, buf + 64, &ws, ws + 4, NULL, 1, &err);
        ok(err == 0 && t - buf == 4 && (unsigned char)buf[1] == 0x80 && buf[3] == '?',
           "cp1252 from Unicode: error %d, %d bytes, [1] = 0x%02x, [3] = 0x%02x (unmapped must be '?')\n",
           err, (int)(t - buf), (unsigned char)buf[1], (unsigned char)buf[3]);

        name = p_ucnv_getName(c, &err);
        ok(name && !strcmp(name, "windows-1252"), "ucnv_getName: %s\n", name ? name : "(null)");
        name = p_ucnv_getStandardName("cp1252", "MIME", &err);
        ok(name && !strcmp(name, "windows-1252"), "ucnv_getStandardName(cp1252, MIME): %s\n", name ? name : "(null)");
        p_ucnv_close(c);
    }

    /* UTF-8 split in the middle of a character: kept for the next call */
    err = 0;
    c = p_ucnv_open("utf8", &err);
    ok(c != NULL && err == 0, "ucnv_open(utf8): %p, error %d\n", c, err);
    if (c)
    {
        int pending;

        s = "x\xE2\x82";
        wt = wbuf;
        p_ucnv_toUnicode(c, &wt, wbuf + 64, &s, s + 3, NULL, 0, &err);
        pending = p_ucnv_toUCountPending(c, &err);
        ok(err == 0 && wt - wbuf == 1 && pending == 2,
           "UTF-8 partial: error %d, %d units out, %d bytes pending (expected 1 and 2)\n",
           err, (int)(wt - wbuf), pending);
        s = "\xAC";
        p_ucnv_toUnicode(c, &wt, wbuf + 64, &s, s + 1, NULL, 1, &err);
        ok(err == 0 && wt - wbuf == 2 && wbuf[1] == 0x20AC,
           "UTF-8 completed: error %d, %d units, [1] = 0x%04x\n", err, (int)(wt - wbuf), wbuf[1]);

        /* Invalid byte through Qt's callback */
        p_ucnv_setToUCallBack(c, NulSubstitute, NULL, NULL, NULL, &err);
        ok(err == 0, "ucnv_setToUCallBack: error %d\n", err);
        s = "a\xFF" "b";
        wt = wbuf;
        p_ucnv_toUnicode(c, &wt, wbuf + 64, &s, s + 3, NULL, 1, &err);
        ok(err == 0 && wt - wbuf == 3 && wbuf[1] == 0 && g_Invalid == 1,
           "UTF-8 invalid byte: error %d, %d units, [1] = 0x%04x, %d invalid\n",
           err, (int)(wt - wbuf), wbuf[1], g_Invalid);
        p_ucnv_close(c);
    }

    /* Shift_JIS, a double-byte character */
    err = 0;
    c = p_ucnv_open("Shift_JIS", &err);
    ok(c != NULL && err == 0, "ucnv_open(Shift_JIS): %p, error %d\n", c, err);
    if (c)
    {
        s = "\x82\xA0";
        wt = wbuf;
        p_ucnv_toUnicode(c, &wt, wbuf + 64, &s, s + 2, NULL, 1, &err);
        ok(err == 0 && wt - wbuf == 1 && wbuf[0] == 0x3042,
           "Shift_JIS: error %d, %d units, [0] = 0x%04x (expected hiragana a, 0x3042)\n",
           err, (int)(wt - wbuf), wbuf[0]);
        p_ucnv_close(c);
    }

    err = 0;
    c = p_ucnv_open("no-such-charset", &err);
    ok(c == NULL && err == 4, "ucnv_open(unknown): %p, error %d (expected U_FILE_ACCESS_ERROR, 4)\n", c, err);
    if (c)
        p_ucnv_close(c);

    ok(p_ucnv_countAvailable() > 20, "ucnv_countAvailable: %d\n", p_ucnv_countAvailable());

    FreeLibrary(hIcu);
}
