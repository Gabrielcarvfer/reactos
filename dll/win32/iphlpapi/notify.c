/*
 * PROJECT:     ReactOS IP Helper API
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Change notifications of interfaces, addresses and routes (Vista)
 *
 * ReactOS does not report these changes yet. A registration only gives the
 * initial notification the caller may ask for, from a worker thread like
 * Windows, and can be cancelled. GLib (GIMP, Inkscape) imports them.
 */

#include "iphlpapi_private.h"

typedef enum _MIB_NOTIFICATION_TYPE_ROS
{
    MibParameterNotificationRos,
    MibAddInstanceRos,
    MibDeleteInstanceRos,
    MibInitialNotificationRos
} MIB_NOTIFICATION_TYPE_ROS;

typedef VOID (WINAPI *PMIB_CHANGE_CALLBACK_ROS)(PVOID CallerContext, PVOID Row,
                                                MIB_NOTIFICATION_TYPE_ROS NotificationType);

typedef struct _MIB_CHANGE_REGISTRATION
{
    PMIB_CHANGE_CALLBACK_ROS Callback;
    PVOID CallerContext;
} MIB_CHANGE_REGISTRATION, *PMIB_CHANGE_REGISTRATION;

static
DWORD
WINAPI
InitialNotificationWorker(PVOID Parameter)
{
    PMIB_CHANGE_REGISTRATION Registration = Parameter;
    Registration->Callback(Registration->CallerContext, NULL, MibInitialNotificationRos);
    return 0;
}

static
DWORD
RegisterChangeNotification(PMIB_CHANGE_CALLBACK_ROS Callback,
                           PVOID CallerContext,
                           BOOLEAN InitialNotification,
                           HANDLE *NotificationHandle)
{
    PMIB_CHANGE_REGISTRATION Registration;

    if (!Callback || !NotificationHandle)
        return ERROR_INVALID_PARAMETER;

    Registration = HeapAlloc(GetProcessHeap(), 0, sizeof(*Registration));
    if (!Registration)
        return ERROR_NOT_ENOUGH_MEMORY;
    Registration->Callback = Callback;
    Registration->CallerContext = CallerContext;
    *NotificationHandle = Registration;

    if (InitialNotification)
        QueueUserWorkItem(InitialNotificationWorker, Registration, WT_EXECUTEDEFAULT);
    return NO_ERROR;
}

DWORD
WINAPI
NotifyIpInterfaceChange(USHORT Family,
                        PMIB_CHANGE_CALLBACK_ROS Callback,
                        PVOID CallerContext,
                        BOOLEAN InitialNotification,
                        HANDLE *NotificationHandle)
{
    UNREFERENCED_PARAMETER(Family);
    return RegisterChangeNotification(Callback, CallerContext, InitialNotification, NotificationHandle);
}

DWORD
WINAPI
NotifyUnicastIpAddressChange(USHORT Family,
                             PMIB_CHANGE_CALLBACK_ROS Callback,
                             PVOID CallerContext,
                             BOOLEAN InitialNotification,
                             HANDLE *NotificationHandle)
{
    UNREFERENCED_PARAMETER(Family);
    return RegisterChangeNotification(Callback, CallerContext, InitialNotification, NotificationHandle);
}

DWORD
WINAPI
NotifyRouteChange2(USHORT Family,
                   PMIB_CHANGE_CALLBACK_ROS Callback,
                   PVOID CallerContext,
                   BOOLEAN InitialNotification,
                   HANDLE *NotificationHandle)
{
    UNREFERENCED_PARAMETER(Family);
    return RegisterChangeNotification(Callback, CallerContext, InitialNotification, NotificationHandle);
}

DWORD
WINAPI
CancelMibChangeNotify2(HANDLE NotificationHandle)
{
    if (!NotificationHandle)
        return ERROR_INVALID_PARAMETER;
    /* (an initial notification still queued keeps working on it: leak
       rather than free it under the worker) */
    return NO_ERROR;
}
