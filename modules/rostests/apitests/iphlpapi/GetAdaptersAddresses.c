/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test that GetAdaptersAddresses gives records of the Vista+ size
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

#include <apitest.h>
#include <winsock2.h>
#include <iphlpapi.h>

/*
 * Programs built for Vista and later read every field of
 * IP_ADAPTER_ADDRESSES_LH (FirstGatewayAddress...). ReactOS gave records of
 * the XP size, followed by the adapter name: Wine's wbemprox (WMI
 * Win32_NetworkAdapterConfiguration) read the name's characters as the
 * gateway list and crashed (AnyDesk's service).
 */
START_TEST(GetAdaptersAddresses)
{
    IP_ADAPTER_ADDRESSES_LH *Buffer, *Aa;
    IP_ADAPTER_GATEWAY_ADDRESS_LH *Gateway;
    ULONG Size = 0, Err, Count = 0, i;
    ULONG_PTR Name, End;

    Err = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_GATEWAYS, NULL, NULL, &Size);
    ok(Err == ERROR_BUFFER_OVERFLOW, "GetAdaptersAddresses returned %lu\n", Err);
    if (Err != ERROR_BUFFER_OVERFLOW) return;

    Buffer = HeapAlloc(GetProcessHeap(), 0, Size);
    /* Garbage in the buffer, as in a real program's */
    memset(Buffer, 0x5a, Size);
    Err = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_GATEWAYS, NULL, (PIP_ADAPTER_ADDRESSES)Buffer, &Size);
    ok(Err == ERROR_SUCCESS, "GetAdaptersAddresses returned %lu\n", Err);
    if (Err != ERROR_SUCCESS)
    {
        HeapFree(GetProcessHeap(), 0, Buffer);
        return;
    }

    for (Aa = Buffer; Aa && Count < 32; Aa = Aa->Next, Count++)
    {
        ok(Aa->Length >= FIELD_OFFSET(IP_ADAPTER_ADDRESSES_LH, Dhcpv6Iaid) + sizeof(ULONG),
           "Adapter %lu: Length %lu\n", Aa->IfIndex, Aa->Length);

        /* The adapter name is not inside the record */
        Name = (ULONG_PTR)Aa->AdapterName;
        End = (ULONG_PTR)Aa + FIELD_OFFSET(IP_ADAPTER_ADDRESSES_LH, Dhcpv6Iaid) + sizeof(ULONG);
        ok(Name < (ULONG_PTR)Aa || Name >= End, "Adapter %lu: the name is inside the record\n", Aa->IfIndex);

        /* The gateway list is a list */
        for (Gateway = Aa->FirstGatewayAddress, i = 0; Gateway && i < 16; Gateway = Gateway->Next, i++)
        {
            ok(Gateway->Address.lpSockaddr != NULL, "Adapter %lu: gateway %lu has no address\n", Aa->IfIndex, i);
            ok(Gateway->Address.iSockaddrLength >= sizeof(SOCKADDR_IN),
               "Adapter %lu: gateway %lu address length %d\n", Aa->IfIndex, i, Gateway->Address.iSockaddrLength);
        }
        ok(i < 16, "Adapter %lu: endless gateway list\n", Aa->IfIndex);
    }
    ok(Count > 0, "No adapters\n");

    HeapFree(GetProcessHeap(), 0, Buffer);
}
