/*
 * PROJECT:     ReactOS ICU subset
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ICU character set converters (icuuc.dll), on Windows code pages
 *
 * Windows 10 ships ICU (icuuc.dll), and Qt 6 builds for Windows use it for the
 * character sets that are not Unicode (QStringConverter). OBS, Audacity and
 * Wireshark did not start without it. This is the converter part of the ICU C
 * API (ucnv_*), with the ICU semantics: streaming conversion that keeps an
 * incomplete sequence for the next call, callbacks for invalid or unmapped
 * characters, output kept for later when the target is full. The conversions
 * themselves are those of the Windows code pages (stateless ones: single and
 * double byte, UTF-8, GB18030).
 */

#include <windef.h>
#include <winbase.h>
#include <winnls.h>

typedef signed char UBool;
typedef WCHAR UChar;
typedef INT32 UChar32;
typedef int UErrorCode;

#define U_ZERO_ERROR 0
#define U_ILLEGAL_ARGUMENT_ERROR 1
#define U_FILE_ACCESS_ERROR 4
#define U_MEMORY_ALLOCATION_ERROR 7
#define U_INVALID_CHAR_FOUND 10
#define U_TRUNCATED_CHAR_FOUND 11
#define U_ILLEGAL_CHAR_FOUND 12
#define U_BUFFER_OVERFLOW_ERROR 15
#define U_FAILURE(x) ((x) > U_ZERO_ERROR)

typedef enum
{
    UCNV_UNASSIGNED = 0,
    UCNV_ILLEGAL = 1,
    UCNV_IRREGULAR = 2,
    UCNV_RESET = 3,
    UCNV_CLOSE = 4,
    UCNV_CLONE = 5
} UConverterCallbackReason;

typedef struct UConverter UConverter;

typedef struct
{
    UINT16 size;
    UBool flush;
    UConverter *converter;
    const UChar *source;
    const UChar *sourceLimit;
    char *target;
    const char *targetLimit;
    INT32 *offsets;
} UConverterFromUnicodeArgs;

typedef struct
{
    UINT16 size;
    UBool flush;
    UConverter *converter;
    const char *source;
    const char *sourceLimit;
    UChar *target;
    const UChar *targetLimit;
    INT32 *offsets;
} UConverterToUnicodeArgs;

typedef void (__cdecl *UConverterToUCallback)(const void *context, UConverterToUnicodeArgs *args,
                                              const char *codeUnits, INT32 length,
                                              UConverterCallbackReason reason, UErrorCode *err);
typedef void (__cdecl *UConverterFromUCallback)(const void *context, UConverterFromUnicodeArgs *args,
                                                const UChar *codeUnits, INT32 length, UChar32 codePoint,
                                                UConverterCallbackReason reason, UErrorCode *err);

void __cdecl UCNV_TO_U_CALLBACK_SUBSTITUTE(const void *context, UConverterToUnicodeArgs *args,
                                           const char *codeUnits, INT32 length,
                                           UConverterCallbackReason reason, UErrorCode *err);
void __cdecl UCNV_FROM_U_CALLBACK_SUBSTITUTE(const void *context, UConverterFromUnicodeArgs *args,
                                             const UChar *codeUnits, INT32 length, UChar32 codePoint,
                                             UConverterCallbackReason reason, UErrorCode *err);

/* THE CHARACTER SETS *******************************************************/

typedef enum
{
    KIND_SBCS,      /* one byte per character */
    KIND_DBCS,      /* lead bytes of the code page start two-byte characters */
    KIND_UTF8,
    KIND_GB18030    /* one, two or four bytes */
} CHARSET_KIND;

typedef struct
{
    const char *Name;       /* IANA name, also the name of the converter */
    const char *Mime;       /* MIME name, if any */
    UINT CodePage;
    const char *Aliases;    /* separated by '|' */
} CHARSET;

static const CHARSET Charsets[] =
{
    { "UTF-8", "UTF-8", CP_UTF8, "utf8|unicode-1-1-utf-8|cp65001|windows-65001" },
    { "US-ASCII", "US-ASCII", 20127, "ascii|ansi_x3.4-1968|iso646-us|us|cp367|ibm367|646" },
    { "ISO-8859-1", "ISO-8859-1", 28591, "latin1|l1|iso_8859-1|iso_8859-1:1987|cp819|ibm819|8859-1" },
    { "ISO-8859-2", "ISO-8859-2", 28592, "latin2|l2|iso_8859-2|8859-2" },
    { "ISO-8859-3", "ISO-8859-3", 28593, "latin3|l3|iso_8859-3|8859-3" },
    { "ISO-8859-4", "ISO-8859-4", 28594, "latin4|l4|iso_8859-4|8859-4" },
    { "ISO-8859-5", "ISO-8859-5", 28595, "cyrillic|iso_8859-5|8859-5" },
    { "ISO-8859-6", "ISO-8859-6", 28596, "arabic|iso_8859-6|8859-6" },
    { "ISO-8859-7", "ISO-8859-7", 28597, "greek|greek8|iso_8859-7|8859-7" },
    { "ISO-8859-8", "ISO-8859-8", 28598, "hebrew|iso_8859-8|8859-8" },
    { "ISO-8859-9", "ISO-8859-9", 28599, "latin5|l5|iso_8859-9|8859-9" },
    { "ISO-8859-13", "ISO-8859-13", 28603, "latin7|iso_8859-13|8859-13" },
    { "ISO-8859-15", "ISO-8859-15", 28605, "latin9|latin-9|iso_8859-15|8859-15" },
    { "windows-1250", "windows-1250", 1250, "cp1250" },
    { "windows-1251", "windows-1251", 1251, "cp1251" },
    { "windows-1252", "windows-1252", 1252, "cp1252" },
    { "windows-1253", "windows-1253", 1253, "cp1253" },
    { "windows-1254", "windows-1254", 1254, "cp1254" },
    { "windows-1255", "windows-1255", 1255, "cp1255" },
    { "windows-1256", "windows-1256", 1256, "cp1256" },
    { "windows-1257", "windows-1257", 1257, "cp1257" },
    { "windows-1258", "windows-1258", 1258, "cp1258" },
    { "windows-874", "windows-874", 874, "cp874|tis-620" },
    { "IBM437", NULL, 437, "cp437|437" },
    { "IBM850", NULL, 850, "cp850|850" },
    { "IBM852", NULL, 852, "cp852|852" },
    { "IBM866", NULL, 866, "cp866|866" },
    { "KOI8-R", "KOI8-R", 20866, "koi8|cp20866" },
    { "KOI8-U", "KOI8-U", 21866, "cp21866" },
    { "macintosh", NULL, 10000, "mac|macroman|csmacintosh" },
    { "Shift_JIS", "Shift_JIS", 932, "sjis|ms_kanji|windows-31j|cp932|x-sjis" },
    { "EUC-JP", "EUC-JP", 20932, "eucjp|x-euc-jp" },
    { "GBK", "GBK", 936, "cp936|ms936|windows-936|gb2312|euc-cn" },
    { "GB18030", "GB18030", 54936, "gb-18030|cp54936" },
    { "Big5", "Big5", 950, "cp950|csbig5|x-big5" },
    { "EUC-KR", "EUC-KR", 949, "cp949|ks_c_5601-1987|korean|windows-949" },
};

/* (Character set names are ASCII) */
static BOOL IsAsciiAlphaNumeric(char c)
{
    return (c >= '0' && c <= '9') || ((c | 0x20) >= 'a' && (c | 0x20) <= 'z');
}

/* Names compare without case, and without the characters other than
 * letters and digits ("UTF-8" is "utf8") */
static BOOL NamesMatch(const char *a, SIZE_T aLength, const char *b)
{
    const char *aEnd = a + aLength;

    for (;;)
    {
        while (a < aEnd && !IsAsciiAlphaNumeric(*a)) a++;
        while (*b && !IsAsciiAlphaNumeric(*b)) b++;
        if (a == aEnd || !*b)
            return (a == aEnd) && !*b;
        if ((*a | 0x20) != (*b | 0x20))
            return FALSE;
        a++;
        b++;
    }
}

static const CHARSET *FindCharset(const char *name)
{
    SIZE_T i;

    if (!name)
        return NULL;
    for (i = 0; i < ARRAYSIZE(Charsets); i++)
    {
        const char *Alias = Charsets[i].Aliases;

        if (NamesMatch(name, strlen(name), Charsets[i].Name) ||
            (Charsets[i].Mime && NamesMatch(name, strlen(name), Charsets[i].Mime)))
            return &Charsets[i];
        while (Alias && *Alias)
        {
            const char *End = strchr(Alias, '|');
            SIZE_T Length = End ? (SIZE_T)(End - Alias) : strlen(Alias);
            if (NamesMatch(Alias, Length, name))
                return &Charsets[i];
            Alias = End ? End + 1 : NULL;
        }
    }
    return NULL;
}

/* THE CONVERTER ************************************************************/

#define PENDING_MAX 8
#define OVERFLOW_MAX 32

struct UConverter
{
    const CHARSET *Charset;
    CHARSET_KIND Kind;
    INT8 MaxCharSize;
    CPINFO CodePageInfo;

    /* Bytes of an incomplete character, for the next call */
    char ToUPending[PENDING_MAX];
    INT32 ToUPendingLength;
    /* A high surrogate waiting for its low one */
    UChar FromUPending;
    INT32 FromUPendingLength;

    /* Output that did not fit in the target, for the next call */
    UChar ToUOverflow[OVERFLOW_MAX];
    INT32 ToUOverflowLength;
    char FromUOverflow[OVERFLOW_MAX];
    INT32 FromUOverflowLength;

    UConverterToUCallback ToUCallback;
    const void *ToUContext;
    UConverterFromUCallback FromUCallback;
    const void *FromUContext;
};

UConverter * __cdecl ucnv_open(const char *converterName, UErrorCode *err)
{
    const CHARSET *Charset;
    UConverter *Converter;

    if (!err || U_FAILURE(*err))
        return NULL;
    Charset = FindCharset(converterName);
    if (!Charset || !IsValidCodePage(Charset->CodePage))
    {
        *err = U_FILE_ACCESS_ERROR;
        return NULL;
    }
    Converter = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Converter));
    if (!Converter)
    {
        *err = U_MEMORY_ALLOCATION_ERROR;
        return NULL;
    }
    Converter->Charset = Charset;
    GetCPInfo(Charset->CodePage, &Converter->CodePageInfo);
    if (Charset->CodePage == CP_UTF8)
    {
        Converter->Kind = KIND_UTF8;
        Converter->MaxCharSize = 3; /* (per UTF-16 unit, as ICU counts) */
    }
    else if (Charset->CodePage == 54936)
    {
        Converter->Kind = KIND_GB18030;
        Converter->MaxCharSize = 4;
    }
    else if (Converter->CodePageInfo.MaxCharSize > 1)
    {
        Converter->Kind = KIND_DBCS;
        Converter->MaxCharSize = 2;
    }
    else
    {
        Converter->Kind = KIND_SBCS;
        Converter->MaxCharSize = 1;
    }
    Converter->ToUCallback = UCNV_TO_U_CALLBACK_SUBSTITUTE;
    Converter->FromUCallback = UCNV_FROM_U_CALLBACK_SUBSTITUTE;
    return Converter;
}

void __cdecl ucnv_close(UConverter *converter)
{
    if (converter)
        HeapFree(GetProcessHeap(), 0, converter);
}

void __cdecl ucnv_reset(UConverter *converter)
{
    if (!converter)
        return;
    converter->ToUPendingLength = 0;
    converter->FromUPendingLength = 0;
    converter->ToUOverflowLength = 0;
    converter->FromUOverflowLength = 0;
}

const char * __cdecl ucnv_getName(const UConverter *converter, UErrorCode *err)
{
    if (!err || U_FAILURE(*err))
        return NULL;
    if (!converter)
    {
        *err = U_ILLEGAL_ARGUMENT_ERROR;
        return NULL;
    }
    return converter->Charset->Name;
}

INT8 __cdecl ucnv_getMaxCharSize(const UConverter *converter)
{
    return converter ? converter->MaxCharSize : 1;
}

/* Only the MIME and IANA standards are known */
const char * __cdecl ucnv_getStandardName(const char *name, const char *standard, UErrorCode *err)
{
    const CHARSET *Charset;

    if (!err || U_FAILURE(*err))
        return NULL;
    if (!name || !standard)
    {
        *err = U_ILLEGAL_ARGUMENT_ERROR;
        return NULL;
    }
    Charset = FindCharset(name);
    if (!Charset)
        return NULL;
    if (!lstrcmpiA(standard, "MIME"))
        return Charset->Mime;
    if (!lstrcmpiA(standard, "IANA"))
        return Charset->Name;
    return NULL;
}

INT32 __cdecl ucnv_countAvailable(void)
{
    INT32 Count = 0;
    SIZE_T i;

    for (i = 0; i < ARRAYSIZE(Charsets); i++)
    {
        if (IsValidCodePage(Charsets[i].CodePage))
            Count++;
    }
    return Count;
}

const char * __cdecl ucnv_getAvailableName(INT32 n)
{
    SIZE_T i;

    for (i = 0; i < ARRAYSIZE(Charsets); i++)
    {
        if (IsValidCodePage(Charsets[i].CodePage) && n-- == 0)
            return Charsets[i].Name;
    }
    return NULL;
}

INT32 __cdecl ucnv_toUCountPending(const UConverter *converter, UErrorCode *err)
{
    if (!err || U_FAILURE(*err))
        return -1;
    if (!converter)
    {
        *err = U_ILLEGAL_ARGUMENT_ERROR;
        return -1;
    }
    return converter->ToUPendingLength;
}

INT32 __cdecl ucnv_fromUCountPending(const UConverter *converter, UErrorCode *err)
{
    if (!err || U_FAILURE(*err))
        return -1;
    if (!converter)
    {
        *err = U_ILLEGAL_ARGUMENT_ERROR;
        return -1;
    }
    return converter->FromUPendingLength;
}

/* CALLBACKS ****************************************************************/

void __cdecl ucnv_getToUCallBack(const UConverter *converter, UConverterToUCallback *action, const void **context)
{
    if (action) *action = converter->ToUCallback;
    if (context) *context = converter->ToUContext;
}

void __cdecl ucnv_getFromUCallBack(const UConverter *converter, UConverterFromUCallback *action,
                                   const void **context)
{
    if (action) *action = converter->FromUCallback;
    if (context) *context = converter->FromUContext;
}

void __cdecl ucnv_setToUCallBack(UConverter *converter, UConverterToUCallback newAction, const void *newContext,
                                 UConverterToUCallback *oldAction, const void **oldContext, UErrorCode *err)
{
    if (!err || U_FAILURE(*err))
        return;
    if (oldAction) *oldAction = converter->ToUCallback;
    if (oldContext) *oldContext = converter->ToUContext;
    converter->ToUCallback = newAction;
    converter->ToUContext = newContext;
}

void __cdecl ucnv_setFromUCallBack(UConverter *converter, UConverterFromUCallback newAction,
                                   const void *newContext, UConverterFromUCallback *oldAction,
                                   const void **oldContext, UErrorCode *err)
{
    if (!err || U_FAILURE(*err))
        return;
    if (oldAction) *oldAction = converter->FromUCallback;
    if (oldContext) *oldContext = converter->FromUContext;
    converter->FromUCallback = newAction;
    converter->FromUContext = newContext;
}

/* Write UTF-16 units to the target of a conversion to Unicode; what does
 * not fit is kept in the converter, and the error says the target is full */
void __cdecl ucnv_cbToUWriteUChars(UConverterToUnicodeArgs *args, const UChar *source, INT32 length,
                                   INT32 offsetIndex, UErrorCode *err)
{
    UConverter *Converter = args->converter;
    INT32 i;

    if (U_FAILURE(*err))
        return;
    for (i = 0; i < length; i++)
    {
        if (args->target < args->targetLimit)
        {
            *args->target++ = source[i];
            if (args->offsets) *args->offsets++ = offsetIndex;
        }
        else if (Converter->ToUOverflowLength < OVERFLOW_MAX)
        {
            Converter->ToUOverflow[Converter->ToUOverflowLength++] = source[i];
            *err = U_BUFFER_OVERFLOW_ERROR;
        }
    }
}

static void WriteBytes(UConverterFromUnicodeArgs *args, const char *bytes, INT32 length, INT32 offsetIndex,
                       UErrorCode *err)
{
    UConverter *Converter = args->converter;
    INT32 i;

    for (i = 0; i < length; i++)
    {
        if (args->target < args->targetLimit)
        {
            *args->target++ = bytes[i];
            if (args->offsets) *args->offsets++ = offsetIndex;
        }
        else if (Converter->FromUOverflowLength < OVERFLOW_MAX)
        {
            Converter->FromUOverflow[Converter->FromUOverflowLength++] = bytes[i];
            *err = U_BUFFER_OVERFLOW_ERROR;
        }
    }
}

/* One code point (one or two units) in the code page; FALSE: unmapped */
static BOOL EncodeCodePoint(const UConverter *Converter, const UChar *units, INT32 length, char *bytes,
                            INT32 *byteCount)
{
    BOOL UsedDefault = FALSE;
    int Count;

    if (Converter->Kind == KIND_UTF8 || Converter->Kind == KIND_GB18030)
        Count = WideCharToMultiByte(Converter->Charset->CodePage, 0, units, length, bytes, 8, NULL, NULL);
    else
        Count = WideCharToMultiByte(Converter->Charset->CodePage, WC_NO_BEST_FIT_CHARS, units, length, bytes, 8,
                                    NULL, &UsedDefault);
    if (Count <= 0 || UsedDefault)
        return FALSE;
    *byteCount = Count;
    return TRUE;
}

/* Write UTF-16 units, in the converter's character set, to the target of a
 * conversion from Unicode (what cannot be mapped becomes the substitute) */
void __cdecl ucnv_cbFromUWriteUChars(UConverterFromUnicodeArgs *args, const UChar **source,
                                     const UChar *sourceLimit, INT32 offsetIndex, UErrorCode *err)
{
    if (U_FAILURE(*err))
        return;
    while (*source < sourceLimit)
    {
        char Bytes[8];
        INT32 Count = 0, Units = 1;

        if (IS_HIGH_SURROGATE((*source)[0]) && *source + 1 < sourceLimit && IS_LOW_SURROGATE((*source)[1]))
            Units = 2;
        if (!EncodeCodePoint(args->converter, *source, Units, Bytes, &Count))
        {
            Bytes[0] = '?';
            Count = 1;
        }
        WriteBytes(args, Bytes, Count, offsetIndex, err);
        *source += Units;
    }
}

/* The substitute: U+FFFD, or '?' in the character set. ICU's default
 * callbacks; a context of "i" stops on irregular sequences. */
void __cdecl UCNV_TO_U_CALLBACK_SUBSTITUTE(const void *context, UConverterToUnicodeArgs *args,
                                           const char *codeUnits, INT32 length,
                                           UConverterCallbackReason reason, UErrorCode *err)
{
    static const UChar Substitute = 0xFFFD;

    if (reason > UCNV_IRREGULAR)
        return;
    if (context && *(const char *)context == 'i' && reason == UCNV_IRREGULAR)
        return;
    *err = U_ZERO_ERROR;
    ucnv_cbToUWriteUChars(args, &Substitute, 1, 0, err);
}

void __cdecl UCNV_FROM_U_CALLBACK_SUBSTITUTE(const void *context, UConverterFromUnicodeArgs *args,
                                             const UChar *codeUnits, INT32 length, UChar32 codePoint,
                                             UConverterCallbackReason reason, UErrorCode *err)
{
    if (reason > UCNV_IRREGULAR)
        return;
    if (context && *(const char *)context == 'i' && reason == UCNV_IRREGULAR)
        return;
    *err = U_ZERO_ERROR;
    WriteBytes(args, "?", 1, 0, err);
}

/* TO UNICODE ***************************************************************/

/* How many bytes the character starting with these bytes has; 0: invalid
 * lead byte. GB18030 needs the second byte too (it says 2 until it has it). */
static INT32 CharLength(const UConverter *Converter, const unsigned char *bytes, INT32 available)
{
    unsigned char Lead = bytes[0];

    switch (Converter->Kind)
    {
        case KIND_SBCS:
            return 1;
        case KIND_DBCS:
            return IsDBCSLeadByteEx(Converter->Charset->CodePage, Lead) ? 2 : 1;
        case KIND_UTF8:
            if (Lead < 0x80) return 1;
            if (Lead >= 0xC2 && Lead <= 0xDF) return 2;
            if (Lead >= 0xE0 && Lead <= 0xEF) return 3;
            if (Lead >= 0xF0 && Lead <= 0xF4) return 4;
            return 0;
        case KIND_GB18030:
            if (Lead < 0x80) return 1;
            if (Lead == 0x80 || Lead == 0xFF) return 0;
            if (available >= 2 && bytes[1] >= 0x30 && bytes[1] <= 0x39) return 4;
            return 2;
    }
    return 1;
}

static void ToUError(UConverterToUnicodeArgs *Args, const char *Bytes, INT32 Length,
                     UConverterCallbackReason Reason, UErrorCode *err)
{
    UConverter *Converter = Args->converter;

    *err = (Reason == UCNV_UNASSIGNED) ? U_INVALID_CHAR_FOUND : U_ILLEGAL_CHAR_FOUND;
    if (Converter->ToUCallback)
        Converter->ToUCallback(Converter->ToUContext, Args, Bytes, Length, Reason, err);
}

void __cdecl ucnv_toUnicode(UConverter *converter, UChar **target, const UChar *targetLimit,
                            const char **source, const char *sourceLimit, INT32 *offsets, UBool flush,
                            UErrorCode *err)
{
    UConverterToUnicodeArgs Args;
    INT32 i;

    if (!err || U_FAILURE(*err))
        return;
    if (!converter || !target || !source || !*target || targetLimit < *target || sourceLimit < *source)
    {
        *err = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }

    Args.size = sizeof(Args);
    Args.flush = flush;
    Args.converter = converter;
    Args.source = *source;
    Args.sourceLimit = sourceLimit;
    Args.target = *target;
    Args.targetLimit = targetLimit;
    Args.offsets = offsets;

    /* First what did not fit last time */
    for (i = 0; i < converter->ToUOverflowLength && Args.target < Args.targetLimit; i++)
        *Args.target++ = converter->ToUOverflow[i];
    if (i < converter->ToUOverflowLength)
    {
        memmove(converter->ToUOverflow, converter->ToUOverflow + i,
                (converter->ToUOverflowLength - i) * sizeof(UChar));
        converter->ToUOverflowLength -= i;
        *err = U_BUFFER_OVERFLOW_ERROR;
        goto Done;
    }
    converter->ToUOverflowLength = 0;

    for (;;)
    {
        unsigned char Bytes[PENDING_MAX];
        INT32 Have = converter->ToUPendingLength, Need, Taken = 0;
        UChar Units[2];
        int Count;

        /* The bytes of the next character: the pending ones, then the source */
        memcpy(Bytes, converter->ToUPending, Have);
        if (!Have)
        {
            if (Args.source >= Args.sourceLimit)
                break;
            Bytes[Have++] = *Args.source;
            Taken++;
        }
        Need = CharLength(converter, Bytes, Have);
        while (Need && Have < Need && Args.source + Taken < Args.sourceLimit)
        {
            Bytes[Have++] = Args.source[Taken++];
            if (converter->Kind == KIND_GB18030 && Have == 2)
                Need = CharLength(converter, Bytes, Have);
        }

        if (Need && Have < Need)
        {
            /* Incomplete: keep it for the next call, unless this is the end */
            Args.source += Taken;
            if (!flush)
            {
                memcpy(converter->ToUPending, Bytes, Have);
                converter->ToUPendingLength = Have;
                break;
            }
            converter->ToUPendingLength = 0;
            ToUError(&Args, (const char *)Bytes, Have, UCNV_ILLEGAL, err);
            if (U_FAILURE(*err))
            {
                if (*err == U_ILLEGAL_CHAR_FOUND) *err = U_TRUNCATED_CHAR_FOUND;
                break;
            }
            continue;
        }

        Args.source += Taken;
        converter->ToUPendingLength = 0;
        if (!Need)
        {
            ToUError(&Args, (const char *)Bytes, 1, UCNV_ILLEGAL, err);
        }
        else
        {
            Count = MultiByteToWideChar(converter->Charset->CodePage, MB_ERR_INVALID_CHARS,
                                        (const char *)Bytes, Need, Units, 2);
            if (Count <= 0)
            {
                ToUError(&Args, (const char *)Bytes, Need, UCNV_ILLEGAL, err);
            }
            else
            {
                ucnv_cbToUWriteUChars(&Args, Units, Count, 0, err);
            }
        }
        if (U_FAILURE(*err))
            break;
    }

Done:
    *source = Args.source;
    *target = Args.target;
}

/* FROM UNICODE *************************************************************/

static void FromUError(UConverterFromUnicodeArgs *Args, const UChar *Units, INT32 Length, UChar32 CodePoint,
                       UConverterCallbackReason Reason, UErrorCode *err)
{
    UConverter *Converter = Args->converter;

    *err = (Reason == UCNV_UNASSIGNED) ? U_INVALID_CHAR_FOUND : U_ILLEGAL_CHAR_FOUND;
    if (Converter->FromUCallback)
        Converter->FromUCallback(Converter->FromUContext, Args, Units, Length, CodePoint, Reason, err);
}

void __cdecl ucnv_fromUnicode(UConverter *converter, char **target, const char *targetLimit,
                              const UChar **source, const UChar *sourceLimit, INT32 *offsets, UBool flush,
                              UErrorCode *err)
{
    UConverterFromUnicodeArgs Args;
    INT32 i;

    if (!err || U_FAILURE(*err))
        return;
    if (!converter || !target || !source || !*target || targetLimit < *target || sourceLimit < *source)
    {
        *err = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }

    Args.size = sizeof(Args);
    Args.flush = flush;
    Args.converter = converter;
    Args.source = *source;
    Args.sourceLimit = sourceLimit;
    Args.target = *target;
    Args.targetLimit = targetLimit;
    Args.offsets = offsets;

    /* First what did not fit last time */
    for (i = 0; i < converter->FromUOverflowLength && Args.target < Args.targetLimit; i++)
        *Args.target++ = converter->FromUOverflow[i];
    if (i < converter->FromUOverflowLength)
    {
        memmove(converter->FromUOverflow, converter->FromUOverflow + i, converter->FromUOverflowLength - i);
        converter->FromUOverflowLength -= i;
        *err = U_BUFFER_OVERFLOW_ERROR;
        goto Done;
    }
    converter->FromUOverflowLength = 0;

    for (;;)
    {
        UChar Units[2];
        INT32 Length = 0;
        char Bytes[8];
        INT32 Count;

        /* The units of the next code point: a pending high surrogate first */
        if (converter->FromUPendingLength)
        {
            Units[Length++] = converter->FromUPending;
            converter->FromUPendingLength = 0;
        }
        else
        {
            if (Args.source >= Args.sourceLimit)
                break;
            Units[Length++] = *Args.source++;
        }

        if (IS_HIGH_SURROGATE(Units[0]))
        {
            if (Args.source >= Args.sourceLimit)
            {
                /* Its low surrogate may come next time */
                if (!flush)
                {
                    converter->FromUPending = Units[0];
                    converter->FromUPendingLength = 1;
                    break;
                }
                FromUError(&Args, Units, 1, Units[0], UCNV_ILLEGAL, err);
                if (U_FAILURE(*err))
                {
                    if (*err == U_ILLEGAL_CHAR_FOUND) *err = U_TRUNCATED_CHAR_FOUND;
                    break;
                }
                continue;
            }
            if (!IS_LOW_SURROGATE(*Args.source))
            {
                FromUError(&Args, Units, 1, Units[0], UCNV_ILLEGAL, err);
                if (U_FAILURE(*err))
                    break;
                continue;
            }
            Units[Length++] = *Args.source++;
        }
        else if (IS_LOW_SURROGATE(Units[0]))
        {
            FromUError(&Args, Units, 1, Units[0], UCNV_ILLEGAL, err);
            if (U_FAILURE(*err))
                break;
            continue;
        }

        if (EncodeCodePoint(converter, Units, Length, Bytes, &Count))
        {
            WriteBytes(&Args, Bytes, Count, 0, err);
        }
        else
        {
            UChar32 CodePoint = (Length == 2)
                ? 0x10000 + ((Units[0] - 0xD800) << 10) + (Units[1] - 0xDC00) : Units[0];
            FromUError(&Args, Units, Length, CodePoint, UCNV_UNASSIGNED, err);
        }
        if (U_FAILURE(*err))
            break;
    }

Done:
    *source = Args.source;
    *target = Args.target;
}
