/*
 * PROJECT:     ReactOS icacls
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Display and change the access control lists of files
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * Supported: icacls NAME [/grant[:r] SID:PERM...] [/deny SID:PERM...]
 *            [/remove[:g|:d] SID...] [/reset] [/inheritance:e|d|r]
 *            [/setowner USER] [/T] [/C] [/L] [/Q]
 * and the display of an ACL (icacls NAME). SID is an account name or *S-1-...;
 * PERM is (OI)(CI)(IO)(NP) inheritance flags then a simple right (N, F, M,
 * RX, R, W, D) or a list of specific rights in parentheses.
 * (Installers run it: Epic Games Launcher's MSI.)
 */

#include <stdio.h>
#include <stdlib.h>
#include <windef.h>
#include <winbase.h>
#include <winnls.h>
#include <aclapi.h>
#include <sddl.h>

typedef enum
{
    ACT_GRANT,
    ACT_GRANT_REPLACE,
    ACT_DENY,
    ACT_REMOVE,
    ACT_REMOVE_GRANT,
    ACT_REMOVE_DENY,
    ACT_RESET,
    ACT_INHERIT_ENABLE,
    ACT_INHERIT_DISABLE_COPY,
    ACT_INHERIT_DISABLE_REMOVE,
    ACT_SETOWNER,
} ACTION_TYPE;

typedef struct
{
    ACTION_TYPE Type;
    PSID Sid;
    DWORD Mask;
    DWORD Flags;
} ACTION;

static ACTION *Actions;
static ULONG ActionCount;
static BOOL Recurse, ContinueOnError, Quiet, NoFollow;
static ULONG Succeeded, Failed;

static const struct { LPCWSTR Name; DWORD Mask; } SimpleRights[] =
{
    { L"N", 0 },
    { L"F", FILE_ALL_ACCESS },
    { L"M", FILE_GENERIC_READ | FILE_GENERIC_WRITE | FILE_GENERIC_EXECUTE | DELETE },
    { L"RX", FILE_GENERIC_READ | FILE_GENERIC_EXECUTE },
    { L"R", FILE_GENERIC_READ },
    { L"W", FILE_GENERIC_WRITE & ~READ_CONTROL },
    { L"D", DELETE | SYNCHRONIZE },
};

static const struct { LPCWSTR Name; DWORD Mask; } SpecificRights[] =
{
    { L"DE", DELETE },
    { L"RC", READ_CONTROL },
    { L"WDAC", WRITE_DAC },
    { L"WO", WRITE_OWNER },
    { L"S", SYNCHRONIZE },
    { L"AS", ACCESS_SYSTEM_SECURITY },
    { L"MA", MAXIMUM_ALLOWED },
    { L"GR", GENERIC_READ },
    { L"GW", GENERIC_WRITE },
    { L"GE", GENERIC_EXECUTE },
    { L"GA", GENERIC_ALL },
    { L"RD", FILE_READ_DATA },
    { L"WD", FILE_WRITE_DATA },
    { L"AD", FILE_APPEND_DATA },
    { L"REA", FILE_READ_EA },
    { L"WEA", FILE_WRITE_EA },
    { L"X", FILE_EXECUTE },
    { L"DC", FILE_DELETE_CHILD },
    { L"RA", FILE_READ_ATTRIBUTES },
    { L"WA", FILE_WRITE_ATTRIBUTES },
    /* The simple rights are also accepted in a list */
    { L"F", FILE_ALL_ACCESS },
    { L"M", FILE_GENERIC_READ | FILE_GENERIC_WRITE | FILE_GENERIC_EXECUTE | DELETE },
    { L"RX", FILE_GENERIC_READ | FILE_GENERIC_EXECUTE },
    { L"R", FILE_GENERIC_READ },
    { L"W", FILE_GENERIC_WRITE & ~READ_CONTROL },
    { L"D", DELETE | SYNCHRONIZE },
};

static const struct { LPCWSTR Name; DWORD Flag; } InheritFlags[] =
{
    { L"OI", OBJECT_INHERIT_ACE },
    { L"CI", CONTAINER_INHERIT_ACE },
    { L"IO", INHERIT_ONLY_ACE },
    { L"NP", NO_PROPAGATE_INHERIT_ACE },
    { L"I", INHERITED_ACE },
};

static void Usage(void)
{
    wprintf(L"ICACLS name [/grant[:r] Sid:perm[...]] [/deny Sid:perm[...]]\n"
            L"       [/remove[:g|:d] Sid[...]] [/reset] [/inheritance:e|d|r]\n"
            L"       [/setowner user] [/T] [/C] [/L] [/Q]\n");
}

static PSID GetSid(LPCWSTR Name)
{
    PSID Sid = NULL;
    DWORD SidSize = 0, DomainSize = 0;
    SID_NAME_USE Use;
    LPWSTR Domain;

    if (Name[0] == L'*')
    {
        if (!ConvertStringSidToSidW(Name + 1, &Sid))
            return NULL;
        return Sid;
    }

    LookupAccountNameW(NULL, Name, NULL, &SidSize, NULL, &DomainSize, &Use);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        return NULL;
    Sid = LocalAlloc(LPTR, SidSize);
    Domain = LocalAlloc(LPTR, DomainSize * sizeof(WCHAR));
    if (!Sid || !Domain || !LookupAccountNameW(NULL, Name, Sid, &SidSize, Domain, &DomainSize, &Use))
    {
        LocalFree(Domain);
        LocalFree(Sid);
        SetLastError(ERROR_NONE_MAPPED);
        return NULL;
    }
    LocalFree(Domain);
    return Sid;
}

/* "(OI)(CI)F" or "(OI)(RX,W)" */
static BOOL ParsePermissions(LPCWSTR Perm, PDWORD Mask, PDWORD Flags)
{
    *Mask = 0;
    *Flags = 0;

    while (*Perm)
    {
        if (*Perm == L'(')
        {
            LPCWSTR End = wcschr(Perm, L')');
            WCHAR Item[64];
            SIZE_T Len;
            ULONG i;
            BOOL Found = FALSE;

            if (!End || (SIZE_T)(End - Perm - 1) >= _countof(Item))
                return FALSE;
            Len = End - Perm - 1;
            wcsncpy(Item, Perm + 1, Len);
            Item[Len] = 0;

            for (i = 0; i < _countof(InheritFlags); i++)
            {
                if (!_wcsicmp(Item, InheritFlags[i].Name))
                {
                    *Flags |= InheritFlags[i].Flag;
                    Found = TRUE;
                    break;
                }
            }
            if (!Found)
            {
                /* A list of specific rights */
                LPWSTR Context, Token = wcstok_s(Item, L",", &Context);

                while (Token)
                {
                    for (i = 0; i < _countof(SpecificRights); i++)
                    {
                        if (!_wcsicmp(Token, SpecificRights[i].Name))
                        {
                            *Mask |= SpecificRights[i].Mask;
                            break;
                        }
                    }
                    if (i == _countof(SpecificRights))
                        return FALSE;
                    Token = wcstok_s(NULL, L",", &Context);
                }
            }
            Perm = End + 1;
        }
        else
        {
            ULONG i;

            for (i = 0; i < _countof(SimpleRights); i++)
            {
                if (!_wcsicmp(Perm, SimpleRights[i].Name))
                {
                    *Mask |= SimpleRights[i].Mask;
                    break;
                }
            }
            return i < _countof(SimpleRights);
        }
    }
    return TRUE;
}

static BOOL AddAction(ACTION_TYPE Type, LPCWSTR Arg)
{
    ACTION *NewActions;
    ACTION Action = { Type, NULL, 0, 0 };

    if (Arg)
    {
        WCHAR Name[256];
        LPCWSTR Colon = NULL;

        if (Type == ACT_GRANT || Type == ACT_GRANT_REPLACE || Type == ACT_DENY)
        {
            Colon = wcsrchr(Arg, L':');
            if (!Colon || Colon == Arg || !ParsePermissions(Colon + 1, &Action.Mask, &Action.Flags))
            {
                fwprintf(stderr, L"Invalid parameter \"%s\"\n", Arg);
                return FALSE;
            }
        }
        if (Colon)
        {
            if ((SIZE_T)(Colon - Arg) >= _countof(Name))
                return FALSE;
            wcsncpy(Name, Arg, Colon - Arg);
            Name[Colon - Arg] = 0;
        }
        else
        {
            wcsncpy(Name, Arg, _countof(Name) - 1);
            Name[_countof(Name) - 1] = 0;
        }
        Action.Sid = GetSid(Name);
        if (!Action.Sid)
        {
            fwprintf(stderr, L"%s: No mapping between account names and security IDs was done.\n", Name);
            return FALSE;
        }
    }

    NewActions = realloc(Actions, (ActionCount + 1) * sizeof(ACTION));
    if (!NewActions)
        return FALSE;
    Actions = NewActions;
    Actions[ActionCount++] = Action;
    return TRUE;
}

static void PrintMask(DWORD Mask)
{
    ULONG i;
    BOOL First = TRUE;

    for (i = 0; i < _countof(SimpleRights); i++)
    {
        if (SimpleRights[i].Mask && Mask == SimpleRights[i].Mask)
        {
            wprintf(L"(%s)", SimpleRights[i].Name);
            return;
        }
    }
    if (Mask == 0)
    {
        wprintf(L"(N)");
        return;
    }
    wprintf(L"(");
    for (i = 0; i < _countof(SpecificRights) && Mask; i++)
    {
        if ((Mask & SpecificRights[i].Mask) == SpecificRights[i].Mask)
        {
            wprintf(L"%s%s", First ? L"" : L",", SpecificRights[i].Name);
            Mask &= ~SpecificRights[i].Mask;
            First = FALSE;
        }
    }
    wprintf(L")");
}

static void PrintAcl(LPCWSTR Path, PACL Dacl)
{
    ACL_SIZE_INFORMATION Info;
    ULONG i;
    SIZE_T Indent = wcslen(Path);

    wprintf(L"%s ", Path);
    if (!Dacl)
    {
        wprintf(L"Everyone:(F)\n\n");
        return;
    }
    if (!GetAclInformation(Dacl, &Info, sizeof(Info), AclSizeInformation))
        return;
    for (i = 0; i < Info.AceCount; i++)
    {
        PACE_HEADER Header;
        PSID Sid;
        DWORD Mask;
        WCHAR Name[256], Domain[256];
        DWORD NameSize = _countof(Name), DomainSize = _countof(Domain);
        SID_NAME_USE Use;
        ULONG f;

        if (!GetAce(Dacl, i, (LPVOID *)&Header))
            continue;
        if (Header->AceType != ACCESS_ALLOWED_ACE_TYPE && Header->AceType != ACCESS_DENIED_ACE_TYPE)
            continue;
        Mask = ((ACCESS_ALLOWED_ACE *)Header)->Mask;
        Sid = &((ACCESS_ALLOWED_ACE *)Header)->SidStart;

        if (i != 0)
            wprintf(L"%*s", (int)Indent + 1, L"");
        if (LookupAccountSidW(NULL, Sid, Name, &NameSize, Domain, &DomainSize, &Use))
        {
            if (Domain[0])
                wprintf(L"%s\\%s:", Domain, Name);
            else
                wprintf(L"%s:", Name);
        }
        else
        {
            LPWSTR StringSid;
            if (ConvertSidToStringSidW(Sid, &StringSid))
            {
                wprintf(L"%s:", StringSid);
                LocalFree(StringSid);
            }
        }
        if (Header->AceType == ACCESS_DENIED_ACE_TYPE)
            wprintf(L"(DENY)");
        for (f = 0; f < _countof(InheritFlags); f++)
        {
            if (Header->AceFlags & InheritFlags[f].Flag)
                wprintf(L"(%s)", InheritFlags[f].Name);
        }
        PrintMask(Mask);
        wprintf(L"\n");
    }
    wprintf(L"\n");
}

/* A copy of Dacl without the explicit ACEs of Sid of the given type(s) */
static PACL RemoveSid(PACL Dacl, PSID Sid, BOOL Allowed, BOOL Denied)
{
    ACL_SIZE_INFORMATION Info;
    PACL NewDacl;
    ULONG i;

    if (!GetAclInformation(Dacl, &Info, sizeof(Info), AclSizeInformation))
        return NULL;
    NewDacl = LocalAlloc(LPTR, Info.AclBytesInUse);
    if (!NewDacl || !InitializeAcl(NewDacl, Info.AclBytesInUse, ACL_REVISION))
    {
        LocalFree(NewDacl);
        return NULL;
    }
    for (i = 0; i < Info.AceCount; i++)
    {
        PACE_HEADER Header;

        if (!GetAce(Dacl, i, (LPVOID *)&Header))
            continue;
        if (!(Header->AceFlags & INHERITED_ACE) &&
            ((Allowed && Header->AceType == ACCESS_ALLOWED_ACE_TYPE) ||
             (Denied && Header->AceType == ACCESS_DENIED_ACE_TYPE)) &&
            EqualSid(Sid, &((ACCESS_ALLOWED_ACE *)Header)->SidStart))
        {
            continue;
        }
        AddAce(NewDacl, ACL_REVISION, MAXDWORD, Header, Header->AceSize);
    }
    return NewDacl;
}

/* A copy of Dacl with (Protect) or without (!Protect) its inherited ACEs as explicit ones */
static PACL CopyInherited(PACL Dacl, BOOL Keep)
{
    ACL_SIZE_INFORMATION Info;
    PACL NewDacl;
    ULONG i;

    if (!GetAclInformation(Dacl, &Info, sizeof(Info), AclSizeInformation))
        return NULL;
    NewDacl = LocalAlloc(LPTR, Info.AclBytesInUse);
    if (!NewDacl || !InitializeAcl(NewDacl, Info.AclBytesInUse, ACL_REVISION))
    {
        LocalFree(NewDacl);
        return NULL;
    }
    for (i = 0; i < Info.AceCount; i++)
    {
        PACE_HEADER Header;

        if (!GetAce(Dacl, i, (LPVOID *)&Header))
            continue;
        if (Header->AceFlags & INHERITED_ACE)
        {
            if (!Keep)
                continue;
            Header->AceFlags &= ~INHERITED_ACE;
        }
        AddAce(NewDacl, ACL_REVISION, MAXDWORD, Header, Header->AceSize);
    }
    return NewDacl;
}

static DWORD ApplyActions(LPWSTR Path)
{
    PSECURITY_DESCRIPTOR Sd = NULL;
    PACL Dacl = NULL, NewDacl;
    SECURITY_DESCRIPTOR_CONTROL Control = 0;
    DWORD Revision, Error;
    ULONG i;

    Error = GetNamedSecurityInfoW(Path, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                  NULL, NULL, &Dacl, NULL, &Sd);
    if (Error != ERROR_SUCCESS)
        return Error;
    GetSecurityDescriptorControl(Sd, &Control, &Revision);

    if (ActionCount == 0)
    {
        PrintAcl(Path, Dacl);
        LocalFree(Sd);
        return ERROR_SUCCESS;
    }

    for (i = 0; i < ActionCount && Error == ERROR_SUCCESS; i++)
    {
        ACTION *Action = &Actions[i];
        SECURITY_INFORMATION SecInfo = DACL_SECURITY_INFORMATION;
        EXPLICIT_ACCESSW Access;

        NewDacl = NULL;
        switch (Action->Type)
        {
            case ACT_GRANT:
            case ACT_GRANT_REPLACE:
            case ACT_DENY:
                ZeroMemory(&Access, sizeof(Access));
                Access.grfAccessPermissions = Action->Mask;
                Access.grfAccessMode = (Action->Type == ACT_GRANT) ? GRANT_ACCESS :
                                       (Action->Type == ACT_DENY) ? DENY_ACCESS : SET_ACCESS;
                Access.grfInheritance = Action->Flags & ~INHERITED_ACE;
                Access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
                Access.Trustee.ptstrName = Action->Sid;
                Error = SetEntriesInAclW(1, &Access, Dacl, &NewDacl);
                break;

            case ACT_REMOVE:
            case ACT_REMOVE_GRANT:
            case ACT_REMOVE_DENY:
                if (!Dacl)
                    continue;
                NewDacl = RemoveSid(Dacl, Action->Sid,
                                    Action->Type != ACT_REMOVE_DENY,
                                    Action->Type != ACT_REMOVE_GRANT);
                if (!NewDacl)
                    Error = GetLastError();
                break;

            case ACT_RESET:
                /* An empty DACL that inherits from the parent */
                NewDacl = LocalAlloc(LPTR, sizeof(ACL));
                if (!NewDacl || !InitializeAcl(NewDacl, sizeof(ACL), ACL_REVISION))
                    Error = ERROR_NOT_ENOUGH_MEMORY;
                SecInfo |= UNPROTECTED_DACL_SECURITY_INFORMATION;
                break;

            case ACT_INHERIT_ENABLE:
                NewDacl = Dacl ? CopyInherited(Dacl, FALSE) : NULL;
                SecInfo |= UNPROTECTED_DACL_SECURITY_INFORMATION;
                break;

            case ACT_INHERIT_DISABLE_COPY:
            case ACT_INHERIT_DISABLE_REMOVE:
                if (!Dacl)
                    continue;
                NewDacl = CopyInherited(Dacl, Action->Type == ACT_INHERIT_DISABLE_COPY);
                if (!NewDacl)
                    Error = GetLastError();
                SecInfo |= PROTECTED_DACL_SECURITY_INFORMATION;
                break;

            case ACT_SETOWNER:
                Error = SetNamedSecurityInfoW(Path, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION,
                                              Action->Sid, NULL, NULL, NULL);
                continue;
        }
        if (Error != ERROR_SUCCESS)
            break;

        /* Keep the protection the DACL had */
        if (!(SecInfo & (PROTECTED_DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION)))
        {
            SecInfo |= (Control & SE_DACL_PROTECTED) ? PROTECTED_DACL_SECURITY_INFORMATION
                                                     : UNPROTECTED_DACL_SECURITY_INFORMATION;
        }
        /* SetNamedSecurityInfo adds the inherited ACEs itself */
        if (NewDacl && (SecInfo & UNPROTECTED_DACL_SECURITY_INFORMATION) && Action->Type != ACT_INHERIT_ENABLE)
        {
            PACL Explicit = CopyInherited(NewDacl, FALSE);
            if (Explicit)
            {
                LocalFree(NewDacl);
                NewDacl = Explicit;
            }
        }
        Error = SetNamedSecurityInfoW(Path, SE_FILE_OBJECT, SecInfo, NULL, NULL, NewDacl, NULL);
        LocalFree(NewDacl);

        /* The next action works on the result */
        LocalFree(Sd);
        Sd = NULL;
        Dacl = NULL;
        if (Error == ERROR_SUCCESS)
        {
            Error = GetNamedSecurityInfoW(Path, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                          NULL, NULL, &Dacl, NULL, &Sd);
            if (Sd)
                GetSecurityDescriptorControl(Sd, &Control, &Revision);
        }
    }

    LocalFree(Sd);
    if (Error == ERROR_SUCCESS && !Quiet)
        wprintf(L"processed file: %s\n", Path);
    return Error;
}

static void PrintError(LPCWSTR Path, DWORD Error)
{
    WCHAR Message[512];

    if (!FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, Error, 0,
                        Message, _countof(Message), NULL))
    {
        _snwprintf(Message, _countof(Message), L"Error %lu\n", Error);
    }
    fwprintf(stderr, L"%s: %s", Path, Message);
}

static DWORD ProcessPath(LPCWSTR Path, BOOL *Stop)
{
    WCHAR Buffer[MAX_PATH * 2];
    DWORD Error, LastError = ERROR_SUCCESS;
    DWORD Attributes;

    wcsncpy(Buffer, Path, _countof(Buffer) - 1);
    Buffer[_countof(Buffer) - 1] = 0;
    Error = ApplyActions(Buffer);
    if (Error != ERROR_SUCCESS)
    {
        PrintError(Path, Error);
        Failed++;
        if (!ContinueOnError)
        {
            *Stop = TRUE;
            return Error;
        }
        LastError = Error;
    }
    else
    {
        Succeeded++;
    }

    Attributes = GetFileAttributesW(Path);
    if (Recurse && Attributes != INVALID_FILE_ATTRIBUTES && (Attributes & FILE_ATTRIBUTE_DIRECTORY) &&
        !(NoFollow && (Attributes & FILE_ATTRIBUTE_REPARSE_POINT)))
    {
        WIN32_FIND_DATAW Data;
        HANDLE Find;

        _snwprintf(Buffer, _countof(Buffer) - 1, L"%s\\*", Path);
        Find = FindFirstFileW(Buffer, &Data);
        if (Find != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (!wcscmp(Data.cFileName, L".") || !wcscmp(Data.cFileName, L".."))
                    continue;
                _snwprintf(Buffer, _countof(Buffer) - 1, L"%s\\%s", Path, Data.cFileName);
                Buffer[_countof(Buffer) - 1] = 0;
                Error = ProcessPath(Buffer, Stop);
                if (Error != ERROR_SUCCESS)
                    LastError = Error;
            } while (!*Stop && FindNextFileW(Find, &Data));
            FindClose(Find);
        }
    }
    return LastError;
}

int wmain(int argc, WCHAR *argv[])
{
    DWORD Error = ERROR_SUCCESS, LastError = ERROR_SUCCESS;
    BOOL Stop = FALSE;
    int i, First;
    LPWSTR Path;
    WIN32_FIND_DATAW Data;
    HANDLE Find;

    if (argc < 2 || !wcscmp(argv[1], L"/?"))
    {
        Usage();
        return argc < 2 ? ERROR_INVALID_PARAMETER : 0;
    }
    Path = argv[1];

    for (i = 2; i < argc; i++)
    {
        LPCWSTR Opt = argv[i];
        ACTION_TYPE Type;
        BOOL TakesSids = TRUE;

        if (!_wcsicmp(Opt, L"/T")) { Recurse = TRUE; continue; }
        if (!_wcsicmp(Opt, L"/C")) { ContinueOnError = TRUE; continue; }
        if (!_wcsicmp(Opt, L"/Q")) { Quiet = TRUE; continue; }
        if (!_wcsicmp(Opt, L"/L")) { NoFollow = TRUE; continue; }

        if (!_wcsicmp(Opt, L"/grant")) Type = ACT_GRANT;
        else if (!_wcsicmp(Opt, L"/grant:r")) Type = ACT_GRANT_REPLACE;
        else if (!_wcsicmp(Opt, L"/deny")) Type = ACT_DENY;
        else if (!_wcsicmp(Opt, L"/remove")) Type = ACT_REMOVE;
        else if (!_wcsicmp(Opt, L"/remove:g")) Type = ACT_REMOVE_GRANT;
        else if (!_wcsicmp(Opt, L"/remove:d")) Type = ACT_REMOVE_DENY;
        else if (!_wcsicmp(Opt, L"/setowner")) Type = ACT_SETOWNER;
        else
        {
            TakesSids = FALSE;
            if (!_wcsicmp(Opt, L"/reset")) Type = ACT_RESET;
            else if (!_wcsicmp(Opt, L"/inheritance:e")) Type = ACT_INHERIT_ENABLE;
            else if (!_wcsicmp(Opt, L"/inheritance:d")) Type = ACT_INHERIT_DISABLE_COPY;
            else if (!_wcsicmp(Opt, L"/inheritance:r")) Type = ACT_INHERIT_DISABLE_REMOVE;
            else
            {
                fwprintf(stderr, L"Invalid parameter \"%s\"\n", Opt);
                return ERROR_INVALID_PARAMETER;
            }
        }

        if (!TakesSids)
        {
            if (!AddAction(Type, NULL))
                return ERROR_INVALID_PARAMETER;
            continue;
        }

        /* One or more SIDs (with their permissions) up to the next option */
        First = i + 1;
        while (i + 1 < argc && argv[i + 1][0] != L'/')
        {
            i++;
            if (!AddAction(Type, argv[i]))
                return ERROR_NONE_MAPPED;
            if (Type == ACT_SETOWNER)
                break;
        }
        if (i < First)
        {
            fwprintf(stderr, L"Invalid parameter \"%s\"\n", Opt);
            return ERROR_INVALID_PARAMETER;
        }
    }

    /* Wildcards in the last part of the name */
    if (wcspbrk(Path, L"*?"))
    {
        WCHAR Dir[MAX_PATH], Full[MAX_PATH * 2];
        LPWSTR Slash;

        wcsncpy(Dir, Path, _countof(Dir) - 1);
        Dir[_countof(Dir) - 1] = 0;
        Slash = wcsrchr(Dir, L'\\');
        if (Slash)
            Slash[1] = 0;
        else
            Dir[0] = 0;

        Find = FindFirstFileW(Path, &Data);
        if (Find == INVALID_HANDLE_VALUE)
        {
            Error = GetLastError();
            PrintError(Path, Error);
            Failed++;
            LastError = Error;
        }
        else
        {
            do
            {
                if (!wcscmp(Data.cFileName, L".") || !wcscmp(Data.cFileName, L".."))
                    continue;
                _snwprintf(Full, _countof(Full) - 1, L"%s%s", Dir, Data.cFileName);
                Full[_countof(Full) - 1] = 0;
                Error = ProcessPath(Full, &Stop);
                if (Error != ERROR_SUCCESS)
                    LastError = Error;
            } while (!Stop && FindNextFileW(Find, &Data));
            FindClose(Find);
        }
    }
    else
    {
        LastError = ProcessPath(Path, &Stop);
    }

    wprintf(L"Successfully processed %lu files; Failed processing %lu files\n", Succeeded, Failed);
    return LastError;
}
