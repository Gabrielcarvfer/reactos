/*
 * PROJECT:     ReactOS WoW64
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Which registry key a 32-bit program means (Wow6432Node):
 *              used by wow64.dll, and by the 64-bit advapi32 for
 *              KEY_WOW64_32KEY
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * As on 64-bit Windows, 32-bit programs have their own HKLM\SOFTWARE: its keys
 * live in HKLM\SOFTWARE\Wow6432Node, except some keys that both views share.
 * In the classes, the COM registrations (CLSID, Interface...) have their
 * 32-bit version in Classes\Wow6432Node.
 *
 * GetKeyView32(Path): the key that a 32-bit program means by the full path
 * \REGISTRY\... (FALSE: the same key).
 */

#pragma once

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

static const WCHAR SoftwarePath[] = L"\\REGISTRY\\MACHINE\\SOFTWARE";
static const WCHAR UserPath[] = L"\\REGISTRY\\USER\\";
static const WCHAR Wow6432Node[] = L"Wow6432Node";

/* The keys of HKLM\SOFTWARE that 32 and 64-bit programs share (Windows 7) */
static const WCHAR *const SharedKeys[] =
{
    L"Microsoft\\COM3",
    L"Microsoft\\Cryptography\\Calais\\Current",
    L"Microsoft\\Cryptography\\Calais\\Readers",
    L"Microsoft\\Cryptography\\Services",
    L"Microsoft\\CTF\\SystemShared",
    L"Microsoft\\CTF\\TIP",
    L"Microsoft\\DFS",
    L"Microsoft\\Driver Signing",
    L"Microsoft\\EnterpriseCertificates",
    L"Microsoft\\EventSystem",
    L"Microsoft\\MSMQ",
    L"Microsoft\\Non-Driver Signing",
    L"Microsoft\\Notepad\\DefaultFonts",
    L"Microsoft\\OLE",
    L"Microsoft\\RAS",
    L"Microsoft\\RPC",
    L"Microsoft\\Shared Tools\\MSInfo",
    L"Microsoft\\SystemCertificates",
    L"Microsoft\\TermServLicensing",
    L"Microsoft\\Transaction Server",
    L"Microsoft\\Windows\\CurrentVersion\\Control Panel\\Cursors\\Schemes",
    L"Microsoft\\Windows\\CurrentVersion\\Explorer\\AutoplayHandlers",
    L"Microsoft\\Windows\\CurrentVersion\\Explorer\\DriveIcons",
    L"Microsoft\\Windows\\CurrentVersion\\Explorer\\KindMap",
    L"Microsoft\\Windows\\CurrentVersion\\Group Policy",
    L"Microsoft\\Windows\\CurrentVersion\\Policies",
    L"Microsoft\\Windows\\CurrentVersion\\PreviewHandlers",
    L"Microsoft\\Windows\\CurrentVersion\\Setup",
    L"Microsoft\\Windows\\CurrentVersion\\Telephony",
    L"Microsoft\\Windows NT\\CurrentVersion\\CompatibilityFlags",
    L"Microsoft\\Windows NT\\CurrentVersion\\Console",
    L"Microsoft\\Windows NT\\CurrentVersion\\FontDpi",
    L"Microsoft\\Windows NT\\CurrentVersion\\FontLink",
    L"Microsoft\\Windows NT\\CurrentVersion\\FontMapper",
    L"Microsoft\\Windows NT\\CurrentVersion\\Fonts",
    L"Microsoft\\Windows NT\\CurrentVersion\\FontSubstitutes",
    L"Microsoft\\Windows NT\\CurrentVersion\\Gre_Initialize",
    L"Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options",
    L"Microsoft\\Windows NT\\CurrentVersion\\LanguagePack",
    L"Microsoft\\Windows NT\\CurrentVersion\\NetworkCards",
    L"Microsoft\\Windows NT\\CurrentVersion\\Perflib",
    L"Microsoft\\Windows NT\\CurrentVersion\\Ports",
    L"Microsoft\\Windows NT\\CurrentVersion\\Print",
    L"Microsoft\\Windows NT\\CurrentVersion\\ProfileList",
    L"Microsoft\\Windows NT\\CurrentVersion\\Time Zones",
    L"Policies",
    L"ReactOS",                 /* the settings of ReactOS itself */
    L"RegisteredApplications",
};

/* The classes that have a 32-bit version */
static const WCHAR *const RedirectedClasses[] =
{
    L"CLSID",
    L"DirectShow",
    L"Interface",
    L"Media Type",
    L"MediaFoundation",
};

/* Does Path (relative, no leading backslash) start with Prefix (one or more
 * whole components)? If so, *pRest is what follows (L"" or L"\\...") */
static BOOL PathStartsWith(const WCHAR *Path, SIZE_T Length, const WCHAR *Prefix, const WCHAR **pRest)
{
    SIZE_T PrefixLength = wcslen(Prefix);

    if (Length < PrefixLength || _wcsnicmp(Path, Prefix, PrefixLength) ||
        (Length > PrefixLength && Path[PrefixLength] != L'\\'))
    {
        return FALSE;
    }
    if (pRest) *pRest = Path + PrefixLength;
    return TRUE;
}

static BOOL IsInList(const WCHAR *Path, SIZE_T Length, const WCHAR *const *List, SIZE_T Count)
{
    SIZE_T i;

    for (i = 0; i < Count; i++)
    {
        if (PathStartsWith(Path, Length, List[i], NULL))
            return TRUE;
    }
    return FALSE;
}

/*
 * The path of a classes key in the view of a 32-bit program: Classes points
 * after "Classes\", and is Length characters long. The result is appended to
 * Out (which holds the path up to "Classes\").
 */
static void ClassesView(const WCHAR *Classes, SIZE_T Length, WCHAR *Out, SIZE_T OutSize)
{
    const WCHAR *Rest = Classes;

    /* (already in the 32-bit part: the same key) */
    if (PathStartsWith(Classes, Length, Wow6432Node, &Rest))
    {
        Rest += (*Rest == L'\\');
        Length -= Rest - Classes;
        Classes = Rest;
    }
    if (Length && IsInList(Classes, Length, RedirectedClasses, ARRAY_SIZE(RedirectedClasses)))
    {
        wcsncat(Out, Wow6432Node, OutSize - wcslen(Out) - 1);
        wcsncat(Out, L"\\", OutSize - wcslen(Out) - 1);
    }
    wcsncat(Out, Classes, min(Length, OutSize - wcslen(Out) - 1));
}

/*
 * The registry key that a 32-bit program means by Path (a full path
 * \REGISTRY\..., Length characters): Out receives it. FALSE if it is the
 * same one.
 */
static BOOL GetKeyView32(const WCHAR *Path, SIZE_T Length, WCHAR *Out, SIZE_T OutSize)
{
    const SIZE_T SoftwareLength = ARRAY_SIZE(SoftwarePath) - 1;
    const WCHAR *Rest, *Sub;
    SIZE_T SubLength;

    Out[0] = 0;

    if (Length >= SoftwareLength && !_wcsnicmp(Path, SoftwarePath, SoftwareLength) &&
        (Length == SoftwareLength || Path[SoftwareLength] == L'\\'))
    {
        /* HKLM\SOFTWARE: Sub is what follows it, without Wow6432Node */
        Sub = Path + SoftwareLength + (Length > SoftwareLength);
        SubLength = Length - (Sub - Path);
        if (PathStartsWith(Sub, SubLength, Wow6432Node, &Rest))
        {
            Rest += (*Rest == L'\\');
            SubLength -= Rest - Sub;
            Sub = Rest;
        }

        wcsncpy(Out, SoftwarePath, OutSize - 1);
        Out[OutSize - 1] = 0;
        wcsncat(Out, L"\\", OutSize - wcslen(Out) - 1);

        if (PathStartsWith(Sub, SubLength, L"Classes", &Rest))
        {
            /* the classes: only some have a 32-bit version */
            wcsncat(Out, L"Classes\\", OutSize - wcslen(Out) - 1);
            Rest += (*Rest == L'\\');
            ClassesView(Rest, SubLength - (Rest - Sub), Out, OutSize);
        }
        else if (SubLength && IsInList(Sub, SubLength, SharedKeys, ARRAY_SIZE(SharedKeys)))
        {
            /* shared */
            wcsncat(Out, Sub, min(SubLength, OutSize - wcslen(Out) - 1));
        }
        else
        {
            /* the 32-bit view */
            wcsncat(Out, Wow6432Node, OutSize - wcslen(Out) - 1);
            if (SubLength)
            {
                wcsncat(Out, L"\\", OutSize - wcslen(Out) - 1);
                wcsncat(Out, Sub, min(SubLength, OutSize - wcslen(Out) - 1));
            }
        }
    }
    else if (Length > ARRAY_SIZE(UserPath) - 1 && !_wcsnicmp(Path, UserPath, ARRAY_SIZE(UserPath) - 1))
    {
        /* HKCU\Software\Classes: \REGISTRY\USER\<SID>_Classes\... */
        const WCHAR *Sid = Path + ARRAY_SIZE(UserPath) - 1, *End;
        SIZE_T Prefix;

        for (End = Sid; End < Path + Length && *End != L'\\'; End++);
        if (End - Sid < 8 || _wcsnicmp(End - 8, L"_Classes", 8) || End == Path + Length)
            return FALSE;
        Prefix = End + 1 - Path;
        wcsncpy(Out, Path, min(Prefix, OutSize - 1));
        Out[min(Prefix, OutSize - 1)] = 0;
        ClassesView(End + 1, Length - Prefix, Out, OutSize);
    }
    else
    {
        return FALSE;
    }

    /* (a key with nothing after "Classes\", i.e. Classes itself, is the same
       key in both views: no trailing backslash) */
    {
        SIZE_T OutLength = wcslen(Out);
        while (OutLength && Out[OutLength - 1] == L'\\')
            Out[--OutLength] = 0;
    }

    return wcslen(Out) != Length || _wcsnicmp(Out, Path, Length);
}

