/*
 * PROJECT:     ReactOS system libraries
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     advapi32.dll Event tracing stubs
 * COPYRIGHT:   Copyright 2017 Mark Jansen (mark.jansen@reactos.org)
 */

#include <advapi32.h>
#include <wmistr.h>
#include <evntrace.h>

WINE_DEFAULT_DEBUG_CHANNEL(advapi);


TRACEHANDLE
WINAPI
OpenTraceA(IN PEVENT_TRACE_LOGFILEA Logfile)
{
    UNIMPLEMENTED;
    SetLastError(ERROR_ACCESS_DENIED);
    return INVALID_PROCESSTRACE_HANDLE;
}

TRACEHANDLE
WINAPI
OpenTraceW(IN PEVENT_TRACE_LOGFILEW Logfile)
{
    UNIMPLEMENTED;
    SetLastError(ERROR_ACCESS_DENIED);
    return INVALID_PROCESSTRACE_HANDLE;
}

ULONG
WINAPI
ProcessTrace(IN PTRACEHANDLE HandleArray,
             IN ULONG HandleCount,
             IN LPFILETIME StartTime,
             IN LPFILETIME EndTime)
{
    UNIMPLEMENTED;
    return ERROR_NOACCESS;
}


/* OpenTrace opens none (see above): no handle can be closed */
ULONG
WINAPI
CloseTrace(IN TRACEHANDLE TraceHandle)
{
    return ERROR_INVALID_HANDLE;
}

/* Enabling providers needs a trace session, which ReactOS cannot start yet
   (StartTrace): report it like OpenTrace does. Programs only need the exports
   (Wireshark's etwdump.exe imports EnableTraceEx2). */
ULONG
WINAPI
EnableTraceEx(IN LPCGUID ProviderId,
              IN LPCGUID SourceId OPTIONAL,
              IN TRACEHANDLE TraceHandle,
              IN ULONG IsEnabled,
              IN UCHAR Level,
              IN ULONGLONG MatchAnyKeyword,
              IN ULONGLONG MatchAllKeyword,
              IN ULONG EnableProperty,
              IN PEVENT_FILTER_DESCRIPTOR EnableFilterDesc OPTIONAL)
{
    UNIMPLEMENTED;
    return ERROR_ACCESS_DENIED;
}

ULONG
WINAPI
EnableTraceEx2(IN TRACEHANDLE TraceHandle,
               IN LPCGUID ProviderId,
               IN ULONG ControlCode,
               IN UCHAR Level,
               IN ULONGLONG MatchAnyKeyword,
               IN ULONGLONG MatchAllKeyword,
               IN ULONG Timeout,
               IN PENABLE_TRACE_PARAMETERS EnableParameters OPTIONAL)
{
    UNIMPLEMENTED;
    return ERROR_ACCESS_DENIED;
}
