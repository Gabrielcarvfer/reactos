/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for slim reader/writer locks (try-acquire)
 *
 * RtlTryAcquireSRWLockShared used to store the shared count in the flag bits:
 * the lock was left corrupted, and the next exclusive owner waited forever
 * (Chromium's V8 deadlocked on YouTube).
 */

#include "precomp.h"

typedef VOID (NTAPI *PSRW)(PRTL_SRWLOCK);
typedef BOOLEAN (NTAPI *PTRYSRW)(PRTL_SRWLOCK);

START_TEST(RtlSRWLock)
{
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    PTRYSRW pTryShared = (PTRYSRW)GetProcAddress(hNtdll, "RtlTryAcquireSRWLockShared");
    PTRYSRW pTryExclusive = (PTRYSRW)GetProcAddress(hNtdll, "RtlTryAcquireSRWLockExclusive");
    PSRW pInit = (PSRW)GetProcAddress(hNtdll, "RtlInitializeSRWLock");
    PSRW pAcquireExclusive = (PSRW)GetProcAddress(hNtdll, "RtlAcquireSRWLockExclusive");
    PSRW pReleaseExclusive = (PSRW)GetProcAddress(hNtdll, "RtlReleaseSRWLockExclusive");
    PSRW pAcquireShared = (PSRW)GetProcAddress(hNtdll, "RtlAcquireSRWLockShared");
    PSRW pReleaseShared = (PSRW)GetProcAddress(hNtdll, "RtlReleaseSRWLockShared");
    RTL_SRWLOCK Lock;

    if (!pTryShared || !pTryExclusive || !pInit)
    {
        skip("SRW try-acquire functions are not exported (Windows 7+)\n");
        return;
    }

    pInit(&Lock);

    /* Free lock: two shared owners, then free again */
    ok(pTryShared(&Lock), "TryShared on a free lock failed\n");
    ok(pTryShared(&Lock), "Second TryShared failed\n");
    ok(!pTryExclusive(&Lock), "TryExclusive succeeded while shared\n");
    pReleaseShared(&Lock);
    pReleaseShared(&Lock);
    ok(Lock.Ptr == NULL, "Lock not free after the shared releases: %p\n", Lock.Ptr);

    /* Exclusive after shared ones must work */
    ok(pTryExclusive(&Lock), "TryExclusive failed after the shared owners left\n");
    ok(!pTryShared(&Lock), "TryShared succeeded while owned exclusively\n");
    ok(!pTryExclusive(&Lock), "Second TryExclusive succeeded\n");
    pReleaseExclusive(&Lock);
    ok(Lock.Ptr == NULL, "Lock not free after the exclusive release: %p\n", Lock.Ptr);

    /* Mixed with the blocking functions */
    pAcquireShared(&Lock);
    ok(pTryShared(&Lock), "TryShared failed next to a shared owner\n");
    pReleaseShared(&Lock);
    pReleaseShared(&Lock);
    pAcquireExclusive(&Lock);
    pReleaseExclusive(&Lock);
    ok(Lock.Ptr == NULL, "Lock not free at the end: %p\n", Lock.Ptr);
}
