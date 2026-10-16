/*
 * PROJECT:     ReactOS Windows Event Log API
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     wevtapi.dll: the Vista event log API, with no event log
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira
 */

/*
 * ReactOS has no Windows Event Log service (only the classic event log of
 * advapi32). Programs delay-load wevtapi.dll and must find it: Chromium's
 * delay-load failure hook stops the program on purpose when a DLL is
 * missing (Spotify, after an unclean shutdown, reads the System log through
 * EvtQuery). Every function here reports that there is no such log, as
 * Windows does for a channel that doesn't exist; no handle is ever created.
 */

#include <windef.h>
#include <winbase.h>

typedef HANDLE EVT_HANDLE, *PEVT_HANDLE;
typedef PVOID PEVT_VARIANT;
typedef PVOID EVT_SUBSCRIBE_CALLBACK;

#ifndef ERROR_EVT_CHANNEL_NOT_FOUND
#define ERROR_EVT_CHANNEL_NOT_FOUND 15007L
#endif

static BOOL NoLog(VOID)
{
    SetLastError(ERROR_EVT_CHANNEL_NOT_FOUND);
    return FALSE;
}

static BOOL BadHandle(VOID)
{
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
}

/* Opening or querying anything */

EVT_HANDLE WINAPI EvtOpenSession(INT LoginClass, PVOID Login, DWORD Timeout, DWORD Flags)
{ SetLastError(ERROR_NOT_SUPPORTED); return NULL; }

EVT_HANDLE WINAPI EvtQuery(EVT_HANDLE Session, LPCWSTR Path, LPCWSTR Query, DWORD Flags)
{ NoLog(); return NULL; }

EVT_HANDLE WINAPI EvtSubscribe(EVT_HANDLE Session, HANDLE SignalEvent, LPCWSTR ChannelPath, LPCWSTR Query,
                               EVT_HANDLE Bookmark, PVOID Context, EVT_SUBSCRIBE_CALLBACK Callback, DWORD Flags)
{ NoLog(); return NULL; }

EVT_HANDLE WINAPI EvtOpenLog(EVT_HANDLE Session, LPCWSTR Path, DWORD Flags)
{ NoLog(); return NULL; }

EVT_HANDLE WINAPI EvtOpenChannelConfig(EVT_HANDLE Session, LPCWSTR ChannelPath, DWORD Flags)
{ NoLog(); return NULL; }

EVT_HANDLE WINAPI EvtOpenPublisherMetadata(EVT_HANDLE Session, LPCWSTR PublisherId, LPCWSTR LogFilePath,
                                           LCID Locale, DWORD Flags)
{ SetLastError(ERROR_FILE_NOT_FOUND); return NULL; }

EVT_HANDLE WINAPI EvtCreateRenderContext(DWORD ValuePathsCount, LPCWSTR *ValuePaths, DWORD Flags)
{ SetLastError(ERROR_NOT_SUPPORTED); return NULL; }

EVT_HANDLE WINAPI EvtCreateBookmark(LPCWSTR BookmarkXml)
{ SetLastError(ERROR_NOT_SUPPORTED); return NULL; }

BOOL WINAPI EvtClearLog(EVT_HANDLE Session, LPCWSTR ChannelPath, LPCWSTR TargetFilePath, DWORD Flags)
{ return NoLog(); }

BOOL WINAPI EvtExportLog(EVT_HANDLE Session, LPCWSTR Path, LPCWSTR Query, LPCWSTR TargetFilePath, DWORD Flags)
{ return NoLog(); }

BOOL WINAPI EvtArchiveExportedLog(EVT_HANDLE Session, LPCWSTR LogFilePath, LCID Locale, DWORD Flags)
{ return NoLog(); }

/* Enumerations: empty */

EVT_HANDLE WINAPI EvtOpenChannelEnum(EVT_HANDLE Session, DWORD Flags)
{ SetLastError(ERROR_NOT_SUPPORTED); return NULL; }

EVT_HANDLE WINAPI EvtOpenPublisherEnum(EVT_HANDLE Session, DWORD Flags)
{ SetLastError(ERROR_NOT_SUPPORTED); return NULL; }

EVT_HANDLE WINAPI EvtOpenEventMetadataEnum(EVT_HANDLE PublisherMetadata, DWORD Flags)
{ BadHandle(); return NULL; }

/* Everything that takes a handle: there are none */

BOOL WINAPI EvtClose(EVT_HANDLE Object)
{ return BadHandle(); }

BOOL WINAPI EvtCancel(EVT_HANDLE Object)
{ return BadHandle(); }

BOOL WINAPI EvtNext(EVT_HANDLE ResultSet, DWORD EventsSize, PEVT_HANDLE Events, DWORD Timeout, DWORD Flags,
                    PDWORD Returned)
{ if (Returned) *Returned = 0; return BadHandle(); }

BOOL WINAPI EvtSeek(EVT_HANDLE ResultSet, LONGLONG Position, EVT_HANDLE Bookmark, DWORD Timeout, DWORD Flags)
{ return BadHandle(); }

BOOL WINAPI EvtRender(EVT_HANDLE Context, EVT_HANDLE Fragment, DWORD Flags, DWORD BufferSize, PVOID Buffer,
                      PDWORD BufferUsed, PDWORD PropertyCount)
{ return BadHandle(); }

BOOL WINAPI EvtFormatMessage(EVT_HANDLE PublisherMetadata, EVT_HANDLE Event, DWORD MessageId, DWORD ValueCount,
                             PEVT_VARIANT Values, DWORD Flags, DWORD BufferSize, LPWSTR Buffer, PDWORD BufferUsed)
{ return BadHandle(); }

BOOL WINAPI EvtGetEventInfo(EVT_HANDLE Event, INT PropertyId, DWORD BufferSize, PVOID Buffer, PDWORD BufferUsed)
{ return BadHandle(); }

BOOL WINAPI EvtGetLogInfo(EVT_HANDLE Log, INT PropertyId, DWORD BufferSize, PVOID Buffer, PDWORD BufferUsed)
{ return BadHandle(); }

BOOL WINAPI EvtGetQueryInfo(EVT_HANDLE QueryOrSubscription, INT PropertyId, DWORD BufferSize, PVOID Buffer,
                            PDWORD BufferUsed)
{ return BadHandle(); }

BOOL WINAPI EvtGetChannelConfigProperty(EVT_HANDLE ChannelConfig, INT PropertyId, DWORD Flags, DWORD BufferSize,
                                        PVOID Buffer, PDWORD BufferUsed)
{ return BadHandle(); }

BOOL WINAPI EvtSetChannelConfigProperty(EVT_HANDLE ChannelConfig, INT PropertyId, DWORD Flags,
                                        PEVT_VARIANT PropertyValue)
{ return BadHandle(); }

BOOL WINAPI EvtSaveChannelConfig(EVT_HANDLE ChannelConfig, DWORD Flags)
{ return BadHandle(); }

BOOL WINAPI EvtGetPublisherMetadataProperty(EVT_HANDLE PublisherMetadata, INT PropertyId, DWORD Flags,
                                            DWORD BufferSize, PVOID Buffer, PDWORD BufferUsed)
{ return BadHandle(); }

BOOL WINAPI EvtGetEventMetadataProperty(EVT_HANDLE EventMetadata, INT PropertyId, DWORD Flags, DWORD BufferSize,
                                        PVOID Buffer, PDWORD BufferUsed)
{ return BadHandle(); }

EVT_HANDLE WINAPI EvtNextEventMetadata(EVT_HANDLE EventMetadataEnum, DWORD Flags)
{ BadHandle(); return NULL; }

BOOL WINAPI EvtNextChannelPath(EVT_HANDLE ChannelEnum, DWORD ChannelPathBufferSize, LPWSTR ChannelPathBuffer,
                               PDWORD ChannelPathBufferUsed)
{ return BadHandle(); }

BOOL WINAPI EvtNextPublisherId(EVT_HANDLE PublisherEnum, DWORD PublisherIdBufferSize, LPWSTR PublisherIdBuffer,
                               PDWORD PublisherIdBufferUsed)
{ return BadHandle(); }

BOOL WINAPI EvtGetObjectArraySize(EVT_HANDLE ObjectArray, PDWORD ObjectArraySize)
{ return BadHandle(); }

BOOL WINAPI EvtGetObjectArrayProperty(EVT_HANDLE ObjectArray, DWORD PropertyId, DWORD ArrayIndex, DWORD Flags,
                                      DWORD PropertyValueBufferSize, PVOID PropertyValueBuffer,
                                      PDWORD PropertyValueBufferUsed)
{ return BadHandle(); }

BOOL WINAPI EvtUpdateBookmark(EVT_HANDLE Bookmark, EVT_HANDLE Event)
{ return BadHandle(); }

DWORD WINAPI EvtGetExtendedStatus(DWORD BufferSize, LPWSTR Buffer, PDWORD BufferUsed)
{
    if (BufferUsed) *BufferUsed = 1;
    if (Buffer && BufferSize) Buffer[0] = UNICODE_NULL;
    return ERROR_SUCCESS;
}
