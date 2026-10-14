/*
 * PROJECT:     ReactOS system libraries
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Performance counter providers (PerfStartProviderEx and co.)
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * A program publishes its performance counters with these functions: it
 * registers as a provider, describes its counter sets, creates instances and
 * points each counter at its own memory. ReactOS has no performance counter
 * consumer yet (no perflib service reads them), so the counters are kept
 * nowhere: the calls only have to succeed, and the instances have to be
 * real memory, which the program may read back.
 */

#include <advapi32.h>
#include <perflib.h>

WINE_DEFAULT_DEBUG_CHANNEL(advapi);

#define PERF_PROVIDER_MAGIC 'frpR'

typedef struct _PERF_PROVIDER
{
    ULONG Magic;
    GUID Guid;
} PERF_PROVIDER, *PPERF_PROVIDER;

static PPERF_PROVIDER
PerfpGetProvider(HANDLE Provider)
{
    PPERF_PROVIDER Data = (PPERF_PROVIDER)Provider;

    if (!Data || Data->Magic != PERF_PROVIDER_MAGIC)
        return NULL;
    return Data;
}

/*
 * @implemented
 */
ULONG
WINAPI
PerfStartProviderEx(GUID *ProviderGuid, PERF_PROVIDER_CONTEXT *ProviderContext, HANDLE *Provider)
{
    PPERF_PROVIDER Data;

    TRACE("%s %p %p\n", debugstr_guid(ProviderGuid), ProviderContext, Provider);

    if (!ProviderGuid || !Provider)
        return ERROR_INVALID_PARAMETER;
    if (ProviderContext && ProviderContext->ContextSize < sizeof(*ProviderContext))
        return ERROR_INVALID_PARAMETER;

    Data = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Data));
    if (!Data)
        return ERROR_NOT_ENOUGH_MEMORY;
    Data->Magic = PERF_PROVIDER_MAGIC;
    Data->Guid = *ProviderGuid;
    *Provider = Data;
    return ERROR_SUCCESS;
}

/*
 * @implemented
 */
ULONG
WINAPI
PerfStartProvider(GUID *ProviderGuid, PERFLIBREQUEST ControlCallback, HANDLE *Provider)
{
    PERF_PROVIDER_CONTEXT Context;

    RtlZeroMemory(&Context, sizeof(Context));
    Context.ContextSize = sizeof(Context);
    Context.ControlCallback = ControlCallback;
    return PerfStartProviderEx(ProviderGuid, &Context, Provider);
}

/*
 * @implemented
 */
ULONG
WINAPI
PerfStopProvider(HANDLE Provider)
{
    PPERF_PROVIDER Data = PerfpGetProvider(Provider);

    TRACE("%p\n", Provider);

    if (!Data)
        return ERROR_INVALID_HANDLE;
    Data->Magic = 0;
    HeapFree(GetProcessHeap(), 0, Data);
    return ERROR_SUCCESS;
}

/*
 * @implemented
 */
ULONG
WINAPI
PerfSetCounterSetInfo(HANDLE Provider, PERF_COUNTERSET_INFO *Template, ULONG TemplateSize)
{
    TRACE("%p %p %lu\n", Provider, Template, TemplateSize);

    if (!PerfpGetProvider(Provider))
        return ERROR_INVALID_HANDLE;
    if (!Template || TemplateSize < sizeof(*Template))
        return ERROR_INVALID_PARAMETER;
    return ERROR_SUCCESS;
}

/*
 * @implemented
 */
PERF_COUNTERSET_INSTANCE *
WINAPI
PerfCreateInstance(HANDLE Provider, const GUID *CounterSetGuid, const WCHAR *Name, ULONG Id)
{
    PERF_COUNTERSET_INSTANCE *Instance;
    ULONG NameSize, Size;

    TRACE("%p %s %s %lu\n", Provider, debugstr_guid(CounterSetGuid), debugstr_w(Name), Id);

    if (!PerfpGetProvider(Provider) || !CounterSetGuid || !Name)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }

    /* The instance, then its name, 8-byte aligned as on Windows */
    NameSize = (ULONG)(wcslen(Name) + 1) * sizeof(WCHAR);
    Size = (sizeof(*Instance) + NameSize + 7) & ~7;
    Instance = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Size);
    if (!Instance)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    Instance->CounterSetGuid = *CounterSetGuid;
    Instance->dwSize = Size;
    Instance->InstanceId = Id;
    Instance->InstanceNameOffset = sizeof(*Instance);
    Instance->InstanceNameSize = NameSize;
    RtlCopyMemory(Instance + 1, Name, NameSize);
    return Instance;
}

/*
 * @implemented
 */
ULONG
WINAPI
PerfDeleteInstance(HANDLE Provider, PERF_COUNTERSET_INSTANCE *Instance)
{
    TRACE("%p %p\n", Provider, Instance);

    if (!PerfpGetProvider(Provider) || !Instance)
        return ERROR_INVALID_PARAMETER;
    HeapFree(GetProcessHeap(), 0, Instance);
    return ERROR_SUCCESS;
}

/*
 * @implemented
 */
ULONG
WINAPI
PerfSetCounterRefValue(HANDLE Provider, PERF_COUNTERSET_INSTANCE *Instance, ULONG CounterId, void *Address)
{
    TRACE("%p %p %lu %p\n", Provider, Instance, CounterId, Address);

    if (!PerfpGetProvider(Provider) || !Instance || !Address)
        return ERROR_INVALID_PARAMETER;
    return ERROR_SUCCESS;
}
