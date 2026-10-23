/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test that PCI interrupt links are spread over their possible IRQs
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

#include <apitest.h>

#include <stdio.h>
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <ndk/cmtypes.h>

/* The interrupt allocated to the first PCI device with this hardware ID
 * prefix (its AllocConfig, as the PnP manager wrote it), or ~0 */
static
ULONG
GetDeviceIrq(PCSTR HardwareIdPrefix)
{
    HDEVINFO DevInfo;
    SP_DEVINFO_DATA Data = { sizeof(Data) };
    CHAR HardwareIds[1024], InstanceId[MAX_DEVICE_ID_LEN], KeyName[MAX_DEVICE_ID_LEN + 64];
    BYTE Buffer[1024];
    PCM_RESOURCE_LIST List = (PCM_RESOURCE_LIST)Buffer;
    PCM_PARTIAL_RESOURCE_LIST Partial;
    DWORD Size, Type;
    HKEY Key;
    ULONG Result = ~0UL, Index, i;

    DevInfo = SetupDiGetClassDevsA(NULL, "PCI", NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (DevInfo == INVALID_HANDLE_VALUE)
        return Result;

    for (Index = 0; SetupDiEnumDeviceInfo(DevInfo, Index, &Data); Index++)
    {
        if (!SetupDiGetDeviceRegistryPropertyA(DevInfo, &Data, SPDRP_HARDWAREID, NULL,
                                               (PBYTE)HardwareIds, sizeof(HardwareIds), NULL))
            continue;
        if (_strnicmp(HardwareIds, HardwareIdPrefix, strlen(HardwareIdPrefix)))
            continue;
        if (!SetupDiGetDeviceInstanceIdA(DevInfo, &Data, InstanceId, sizeof(InstanceId), NULL))
            break;

        sprintf(KeyName, "SYSTEM\\CurrentControlSet\\Enum\\%s\\Control", InstanceId);
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, KeyName, 0, KEY_QUERY_VALUE, &Key) == ERROR_SUCCESS)
        {
            Size = sizeof(Buffer);
            if (RegQueryValueExA(Key, "AllocConfig", NULL, &Type, Buffer, &Size) == ERROR_SUCCESS &&
                Type == REG_RESOURCE_LIST && List->Count >= 1)
            {
                Partial = &List->List[0].PartialResourceList;
                for (i = 0; i < Partial->Count; i++)
                {
                    if (Partial->PartialDescriptors[i].Type == CmResourceTypeInterrupt)
                    {
                        Result = Partial->PartialDescriptors[i].u.Interrupt.Level;
                        break;
                    }
                }
            }
            RegCloseKey(Key);
        }
        trace("%s: IRQ %ld\n", InstanceId, (LONG)Result);
        break;
    }

    SetupDiDestroyDeviceInfoList(DevInfo);
    return Result;
}

/* QEMU's PC machine routes its e1000 network card (on link LNKC) and its PIIX3
 * USB controller (on link LNKD) both to IRQ 11, though each link may also use
 * IRQ 5 and 10. Every interrupt of one device then ran the interrupt routine of
 * the other. Windows and Linux move one of the links to a free IRQ */
START_TEST(PciIrqShare)
{
    ULONG NetIrq, UsbIrq;

    NetIrq = GetDeviceIrq("PCI\\VEN_8086&DEV_100E");
    UsbIrq = GetDeviceIrq("PCI\\VEN_8086&DEV_7020");
    if (NetIrq == ~0UL || UsbIrq == ~0UL)
    {
        skip("Not QEMU's e1000 and PIIX3 USB controller (%lx, %lx)\n", NetIrq, UsbIrq);
        return;
    }

    trace("e1000 IRQ %lu, USB IRQ %lu\n", NetIrq, UsbIrq);
    ok(NetIrq != UsbIrq, "the network card and the USB controller share IRQ %lu\n", NetIrq);
}
