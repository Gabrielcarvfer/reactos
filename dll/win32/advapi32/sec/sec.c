/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS system libraries
 * FILE:            lib/advapi32/sec/sec.c
 * PURPOSE:         Security descriptor functions
 * PROGRAMMER:      Ariadne ( ariadne@xs4all.nl)
 *                  Steven Edwards ( Steven_Ed4153@yahoo.com )
 *                  Andrew Greenwood ( silverblade_uk@hotmail.com )
 * UPDATE HISTORY:
 *                  Created 01/11/98
 */

#include <advapi32.h>
WINE_DEFAULT_DEBUG_CHANNEL(advapi);

/*
 * @implemented
 */
BOOL
WINAPI
GetSecurityDescriptorControl(PSECURITY_DESCRIPTOR pSecurityDescriptor,
                             PSECURITY_DESCRIPTOR_CONTROL pControl,
                             LPDWORD lpdwRevision)
{
    NTSTATUS Status;

    Status = RtlGetControlSecurityDescriptor(pSecurityDescriptor,
                                             pControl,
                                             (PULONG)lpdwRevision);
    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return FALSE;
    }

    return TRUE;
}


/*
 * @implemented
 */
BOOL
WINAPI
GetSecurityDescriptorDacl(PSECURITY_DESCRIPTOR pSecurityDescriptor,
                          LPBOOL lpbDaclPresent,
                          PACL *pDacl,
                          LPBOOL lpbDaclDefaulted)
{
    BOOLEAN DaclPresent;
    BOOLEAN DaclDefaulted;
    NTSTATUS Status;

    Status = RtlGetDaclSecurityDescriptor(pSecurityDescriptor,
                                          &DaclPresent,
                                          pDacl,
                                          &DaclDefaulted);
    *lpbDaclPresent = (BOOL)DaclPresent;
    *lpbDaclDefaulted = (BOOL)DaclDefaulted;

    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return FALSE;
    }

    return TRUE;
}


/*
 * @implemented
 */
BOOL
WINAPI
GetSecurityDescriptorGroup(PSECURITY_DESCRIPTOR pSecurityDescriptor,
                           PSID *pGroup,
                           LPBOOL lpbGroupDefaulted)
{
    BOOLEAN GroupDefaulted;
    NTSTATUS Status;

    Status = RtlGetGroupSecurityDescriptor(pSecurityDescriptor,
                                           pGroup,
                                           &GroupDefaulted);
    *lpbGroupDefaulted = (BOOL)GroupDefaulted;

    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return FALSE;
    }

    return TRUE;
}


/*
 * @implemented
 */
BOOL
WINAPI
GetSecurityDescriptorOwner(PSECURITY_DESCRIPTOR pSecurityDescriptor,
                           PSID *pOwner,
                           LPBOOL lpbOwnerDefaulted)
{
    BOOLEAN OwnerDefaulted;
    NTSTATUS Status;

    Status = RtlGetOwnerSecurityDescriptor(pSecurityDescriptor,
                                           pOwner,
                                           &OwnerDefaulted);
    *lpbOwnerDefaulted = (BOOL)OwnerDefaulted;

    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return FALSE;
    }

    return TRUE;
}


/*
 * @implemented
 */
DWORD
WINAPI
GetSecurityDescriptorRMControl(PSECURITY_DESCRIPTOR SecurityDescriptor,
                               PUCHAR RMControl)
{
    if (!RtlGetSecurityDescriptorRMControl(SecurityDescriptor,
                                           RMControl))
        return ERROR_INVALID_DATA;

    return ERROR_SUCCESS;
}


/*
 * @implemented
 */
BOOL
WINAPI
GetSecurityDescriptorSacl(PSECURITY_DESCRIPTOR pSecurityDescriptor,
                          LPBOOL lpbSaclPresent,
                          PACL *pSacl,
                          LPBOOL lpbSaclDefaulted)
{
    BOOLEAN SaclPresent;
    BOOLEAN SaclDefaulted;
    NTSTATUS Status;

    Status = RtlGetSaclSecurityDescriptor(pSecurityDescriptor,
                                          &SaclPresent,
                                          pSacl,
                                          &SaclDefaulted);
    *lpbSaclPresent = (BOOL)SaclPresent;
    *lpbSaclDefaulted = (BOOL)SaclDefaulted;

    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return FALSE;
    }

    return TRUE;
}

/*
 * @implemented
 */
BOOL
WINAPI
IsValidSecurityDescriptor(PSECURITY_DESCRIPTOR pSecurityDescriptor)
{
    BOOLEAN Result;

    Result = RtlValidSecurityDescriptor (pSecurityDescriptor);
    if (Result == FALSE)
        SetLastError(RtlNtStatusToDosError(STATUS_INVALID_SECURITY_DESCR));

    return (BOOL)Result;
}

/*
 * @implemented
 */
BOOL
WINAPI
MakeAbsoluteSD2(IN OUT PSECURITY_DESCRIPTOR pSelfRelativeSecurityDescriptor,
                OUT LPDWORD lpdwBufferSize)
{
    NTSTATUS Status;

    Status = RtlSelfRelativeToAbsoluteSD2(pSelfRelativeSecurityDescriptor,
                                          lpdwBufferSize);
    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return FALSE;
    }

    return TRUE;
}


/*
 * @implemented
 */
BOOL
WINAPI
MakeSelfRelativeSD(PSECURITY_DESCRIPTOR pAbsoluteSecurityDescriptor,
                   PSECURITY_DESCRIPTOR pSelfRelativeSecurityDescriptor,
                   LPDWORD lpdwBufferLength)
{
    NTSTATUS Status;

    Status = RtlAbsoluteToSelfRelativeSD(pAbsoluteSecurityDescriptor,
                                         pSelfRelativeSecurityDescriptor,
                                         (PULONG)lpdwBufferLength);
    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return FALSE;
    }

    return TRUE;
}


/*
 * @implemented
 */
BOOL
WINAPI
SetSecurityDescriptorControl(PSECURITY_DESCRIPTOR pSecurityDescriptor,
                             SECURITY_DESCRIPTOR_CONTROL ControlBitsOfInterest,
                             SECURITY_DESCRIPTOR_CONTROL ControlBitsToSet)
{
    NTSTATUS Status;

    Status = RtlSetControlSecurityDescriptor(pSecurityDescriptor,
                                             ControlBitsOfInterest,
                                             ControlBitsToSet);
    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return FALSE;
    }

    return TRUE;
}


/*
 * @implemented
 */
BOOL
WINAPI
SetSecurityDescriptorDacl(PSECURITY_DESCRIPTOR pSecurityDescriptor,
                          BOOL bDaclPresent,
                          PACL pDacl,
                          BOOL bDaclDefaulted)
{
    NTSTATUS Status;

    Status = RtlSetDaclSecurityDescriptor(pSecurityDescriptor,
                                          bDaclPresent,
                                          pDacl,
                                          bDaclDefaulted);
    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return FALSE;
    }

    return TRUE;
}


/*
 * @implemented
 */
BOOL
WINAPI
SetSecurityDescriptorGroup(PSECURITY_DESCRIPTOR pSecurityDescriptor,
                           PSID pGroup,
                           BOOL bGroupDefaulted)
{
    NTSTATUS Status;

    Status = RtlSetGroupSecurityDescriptor(pSecurityDescriptor,
                                           pGroup,
                                           bGroupDefaulted);
    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return FALSE;
    }

    return TRUE;
}


/*
 * @implemented
 */
BOOL
WINAPI
SetSecurityDescriptorOwner(PSECURITY_DESCRIPTOR pSecurityDescriptor,
                           PSID pOwner,
                           BOOL bOwnerDefaulted)
{
    NTSTATUS Status;

    Status = RtlSetOwnerSecurityDescriptor(pSecurityDescriptor,
                                           pOwner,
                                           bOwnerDefaulted);
    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return FALSE;
    }

    return TRUE;
}


/*
 * @implemented
 */
DWORD
WINAPI
SetSecurityDescriptorRMControl(PSECURITY_DESCRIPTOR SecurityDescriptor,
                               PUCHAR RMControl)
{
    RtlSetSecurityDescriptorRMControl(SecurityDescriptor,
                                      RMControl);

    return ERROR_SUCCESS;
}


/*
 * @implemented
 */
BOOL
WINAPI
SetSecurityDescriptorSacl(PSECURITY_DESCRIPTOR pSecurityDescriptor,
                          BOOL bSaclPresent,
                          PACL pSacl,
                          BOOL bSaclDefaulted)
{
    NTSTATUS Status;

    Status = RtlSetSaclSecurityDescriptor(pSecurityDescriptor,
                                          bSaclPresent,
                                          pSacl,
                                          bSaclDefaulted);
    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return FALSE;
    }

    return TRUE;
}


/*
 * @implemented
 */
VOID
WINAPI
QuerySecurityAccessMask(IN SECURITY_INFORMATION SecurityInformation,
                        OUT LPDWORD DesiredAccess)
{
    *DesiredAccess = 0;

    if (SecurityInformation & (OWNER_SECURITY_INFORMATION |
                               GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION))
    {
        *DesiredAccess |= READ_CONTROL;
    }

    if (SecurityInformation & SACL_SECURITY_INFORMATION)
        *DesiredAccess |= ACCESS_SYSTEM_SECURITY;
}


/*
 * @implemented
 */
VOID
WINAPI
SetSecurityAccessMask(IN SECURITY_INFORMATION SecurityInformation,
                      OUT LPDWORD DesiredAccess)
{
    *DesiredAccess = 0;

    if (SecurityInformation & (OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION))
        *DesiredAccess |= WRITE_OWNER;

    if (SecurityInformation & DACL_SECURITY_INFORMATION)
        *DesiredAccess |= WRITE_DAC;

    if (SecurityInformation & SACL_SECURITY_INFORMATION)
        *DesiredAccess |= ACCESS_SYSTEM_SECURITY;
}


/*
 * @unimplemented
 */
BOOL
WINAPI
ConvertToAutoInheritPrivateObjectSecurity(IN PSECURITY_DESCRIPTOR ParentDescriptor,
                                          IN PSECURITY_DESCRIPTOR CurrentSecurityDescriptor,
                                          OUT PSECURITY_DESCRIPTOR* NewSecurityDescriptor,
                                          IN GUID* ObjectType,
                                          IN BOOLEAN IsDirectoryObject,
                                          IN PGENERIC_MAPPING GenericMapping)
{
    UNIMPLEMENTED;
    return FALSE;
}


/* The SID a trustee names, for the owner and group of BuildSecurityDescriptorW.
 * Returns a SID to free with LocalFree in *pFreeSid when one was allocated. */
static DWORD
BuildSdpGetTrusteeSid(IN PTRUSTEE_W Trustee,
                      OUT PSID *pSid,
                      OUT PSID *pFreeSid)
{
    *pSid = *pFreeSid = NULL;

    switch (Trustee->TrusteeForm)
    {
        case TRUSTEE_IS_SID:
            *pSid = (PSID)Trustee->ptstrName;
            return ERROR_SUCCESS;

        case TRUSTEE_IS_NAME:
        {
            if (!wcscmp(Trustee->ptstrName, L"CURRENT_USER"))
            {
                HANDLE Token;
                BYTE Buffer[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
                PTOKEN_USER User = (PTOKEN_USER)Buffer;
                DWORD Length;

                if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &Token) &&
                    !OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &Token))
                {
                    return GetLastError();
                }
                if (!GetTokenInformation(Token, TokenUser, Buffer, sizeof(Buffer), &Length))
                {
                    DWORD Error = GetLastError();
                    CloseHandle(Token);
                    return Error;
                }
                CloseHandle(Token);
                Length = GetLengthSid(User->User.Sid);
                *pFreeSid = LocalAlloc(LMEM_FIXED, Length);
                if (!*pFreeSid)
                    return ERROR_NOT_ENOUGH_MEMORY;
                CopySid(Length, *pFreeSid, User->User.Sid);
            }
            else
            {
                DWORD SidSize = 0, DomainSize = 0;
                SID_NAME_USE Use;
                WCHAR Domain[MAX_PATH];

                LookupAccountNameW(NULL, Trustee->ptstrName, NULL, &SidSize, NULL, &DomainSize, &Use);
                if (!SidSize)
                    return GetLastError();
                *pFreeSid = LocalAlloc(LMEM_FIXED, SidSize);
                if (!*pFreeSid)
                    return ERROR_NOT_ENOUGH_MEMORY;
                DomainSize = ARRAYSIZE(Domain);
                if (!LookupAccountNameW(NULL, Trustee->ptstrName, *pFreeSid, &SidSize, Domain, &DomainSize, &Use))
                {
                    DWORD Error = GetLastError();
                    LocalFree(*pFreeSid);
                    *pFreeSid = NULL;
                    return Error;
                }
            }
            *pSid = *pFreeSid;
            return ERROR_SUCCESS;
        }

        default:
            return ERROR_INVALID_PARAMETER;
    }
}

/*
 * @implemented
 *
 * Makes a new self-relative security descriptor (to free with LocalFree): the
 * old one (if any) with the given owner and group, and the access and audit
 * entries merged into its DACL and SACL (like SetEntriesInAcl).
 */
DWORD
WINAPI
BuildSecurityDescriptorW(IN PTRUSTEE_W pOwner  OPTIONAL,
                         IN PTRUSTEE_W pGroup  OPTIONAL,
                         IN ULONG cCountOfAccessEntries,
                         IN PEXPLICIT_ACCESS_W pListOfAccessEntries  OPTIONAL,
                         IN ULONG cCountOfAuditEntries,
                         IN PEXPLICIT_ACCESS_W pListOfAuditEntries  OPTIONAL,
                         IN PSECURITY_DESCRIPTOR pOldSD  OPTIONAL,
                         OUT PULONG pSizeNewSD,
                         OUT PSECURITY_DESCRIPTOR* pNewSD)
{
    SECURITY_DESCRIPTOR Sd;
    PSID Owner = NULL, Group = NULL, FreeOwner = NULL, FreeGroup = NULL;
    PACL Dacl = NULL, Sacl = NULL, NewDacl = NULL, NewSacl = NULL;
    BOOL DaclPresent = FALSE, SaclPresent = FALSE, Defaulted;
    SECURITY_DESCRIPTOR_CONTROL Control = 0;
    DWORD Revision, Size = 0, Error = ERROR_SUCCESS;
    PSECURITY_DESCRIPTOR NewSd = NULL;

    if (!pSizeNewSD || !pNewSD)
        return ERROR_INVALID_PARAMETER;
    *pNewSD = NULL;
    *pSizeNewSD = 0;

    /* Start from the old descriptor */
    if (pOldSD)
    {
        if (!IsValidSecurityDescriptor(pOldSD) ||
            !GetSecurityDescriptorControl(pOldSD, &Control, &Revision) ||
            !GetSecurityDescriptorOwner(pOldSD, &Owner, &Defaulted) ||
            !GetSecurityDescriptorGroup(pOldSD, &Group, &Defaulted) ||
            !GetSecurityDescriptorDacl(pOldSD, &DaclPresent, &Dacl, &Defaulted) ||
            !GetSecurityDescriptorSacl(pOldSD, &SaclPresent, &Sacl, &Defaulted))
        {
            return ERROR_INVALID_SECURITY_DESCR;
        }
    }

    if (pOwner && (Error = BuildSdpGetTrusteeSid(pOwner, &Owner, &FreeOwner)) != ERROR_SUCCESS)
        goto Cleanup;
    if (pGroup && (Error = BuildSdpGetTrusteeSid(pGroup, &Group, &FreeGroup)) != ERROR_SUCCESS)
        goto Cleanup;

    if (cCountOfAccessEntries)
    {
        Error = SetEntriesInAclW(cCountOfAccessEntries, pListOfAccessEntries, Dacl, &NewDacl);
        if (Error != ERROR_SUCCESS)
            goto Cleanup;
        Dacl = NewDacl;
        DaclPresent = TRUE;
    }
    if (cCountOfAuditEntries)
    {
        Error = SetEntriesInAclW(cCountOfAuditEntries, pListOfAuditEntries, Sacl, &NewSacl);
        if (Error != ERROR_SUCCESS)
            goto Cleanup;
        Sacl = NewSacl;
        SaclPresent = TRUE;
    }

    if (!InitializeSecurityDescriptor(&Sd, SECURITY_DESCRIPTOR_REVISION) ||
        (Owner && !SetSecurityDescriptorOwner(&Sd, Owner, FALSE)) ||
        (Group && !SetSecurityDescriptorGroup(&Sd, Group, FALSE)) ||
        (DaclPresent && !SetSecurityDescriptorDacl(&Sd, TRUE, Dacl, FALSE)) ||
        (SaclPresent && !SetSecurityDescriptorSacl(&Sd, TRUE, Sacl, FALSE)))
    {
        Error = GetLastError();
        goto Cleanup;
    }
    /* Keep the inheritance flags of the old descriptor */
    SetSecurityDescriptorControl(&Sd, Control & (SE_DACL_PROTECTED | SE_SACL_PROTECTED |
                                                  SE_DACL_AUTO_INHERITED | SE_SACL_AUTO_INHERITED),
                                 Control & (SE_DACL_PROTECTED | SE_SACL_PROTECTED |
                                            SE_DACL_AUTO_INHERITED | SE_SACL_AUTO_INHERITED));

    MakeSelfRelativeSD(&Sd, NULL, &Size);
    NewSd = LocalAlloc(LMEM_FIXED, Size);
    if (!NewSd)
    {
        Error = ERROR_NOT_ENOUGH_MEMORY;
        goto Cleanup;
    }
    if (!MakeSelfRelativeSD(&Sd, NewSd, &Size))
    {
        Error = GetLastError();
        LocalFree(NewSd);
        goto Cleanup;
    }
    *pNewSD = NewSd;
    *pSizeNewSD = Size;

Cleanup:
    if (NewDacl) LocalFree(NewDacl);
    if (NewSacl) LocalFree(NewSacl);
    if (FreeOwner) LocalFree(FreeOwner);
    if (FreeGroup) LocalFree(FreeGroup);
    return Error;
}


/*
 * @unimplemented
 */
DWORD
WINAPI
BuildSecurityDescriptorA(IN PTRUSTEE_A pOwner  OPTIONAL,
                         IN PTRUSTEE_A pGroup  OPTIONAL,
                         IN ULONG cCountOfAccessEntries,
                         IN PEXPLICIT_ACCESS_A pListOfAccessEntries  OPTIONAL,
                         IN ULONG cCountOfAuditEntries,
                         IN PEXPLICIT_ACCESS_A pListOfAuditEntries  OPTIONAL,
                         IN PSECURITY_DESCRIPTOR pOldSD  OPTIONAL,
                         OUT PULONG pSizeNewSD,
                         OUT PSECURITY_DESCRIPTOR* pNewSD)
{
    UNIMPLEMENTED;
    /* An error code: FALSE would mean ERROR_SUCCESS */
    return ERROR_CALL_NOT_IMPLEMENTED;
}

/* EOF */
