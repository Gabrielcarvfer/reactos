/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test GetIpForwardTable2, GetIfTable2, GetIfEntry2 and ConvertLengthToIpv4Mask
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

#include <apitest.h>
#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <netioapi.h>

/*
 * These were stubs that returned NO_ERROR without filling anything: AnyDesk's
 * service read the uninitialized table pointer of GetIpForwardTable2 and
 * crashed. They must give the same routes and interfaces as the old
 * GetIpForwardTable and GetIfTable.
 */

static void TestForwardTable(void)
{
    PMIB_IPFORWARD_TABLE2 Table = (PVOID)(ULONG_PTR)0xdeadbeef;
    PMIB_IPFORWARDTABLE OldTable;
    ULONG Size = 0, i, Defaults = 0;
    DWORD Error;

    Error = GetIpForwardTable2(AF_INET, &Table);
    ok(Error == NO_ERROR, "GetIpForwardTable2 returned %lu\n", Error);
    if (Error != NO_ERROR) return;
    ok(Table != NULL && Table != (PVOID)(ULONG_PTR)0xdeadbeef, "No table\n");

    GetIpForwardTable(NULL, &Size, FALSE);
    OldTable = HeapAlloc(GetProcessHeap(), 0, Size);
    Error = GetIpForwardTable(OldTable, &Size, FALSE);
    ok(Error == NO_ERROR, "GetIpForwardTable returned %lu\n", Error);
    if (Error == NO_ERROR)
        ok(Table->NumEntries >= OldTable->dwNumEntries, "%lu routes, GetIpForwardTable has %lu\n",
           Table->NumEntries, OldTable->dwNumEntries);

    for (i = 0; i < Table->NumEntries; i++)
    {
        const MIB_IPFORWARD_ROW2 *Row = &Table->Table[i];

        ok(Row->DestinationPrefix.Prefix.si_family == AF_INET, "Route %lu: family %u\n", i, Row->DestinationPrefix.Prefix.si_family);
        ok(Row->DestinationPrefix.PrefixLength <= 32, "Route %lu: prefix length %u\n", i, Row->DestinationPrefix.PrefixLength);
        ok(Row->InterfaceIndex != 0, "Route %lu: no interface\n", i);
        ok(Row->InterfaceLuid.Value != 0, "Route %lu: no interface LUID\n", i);
        {
            /* The route's interface is an interface of the interface table */
            MIB_IFROW IfRow;
            ZeroMemory(&IfRow, sizeof(IfRow));
            IfRow.dwIndex = Row->InterfaceIndex;
            ok(GetIfEntry(&IfRow) == NO_ERROR, "Route %lu: interface %lu not found\n", i, Row->InterfaceIndex);
        }
        if (Row->DestinationPrefix.PrefixLength == 0) Defaults++;
    }
    /* (the test VMs and machines have a default route) */
    ok(Defaults > 0, "No default route\n");

    HeapFree(GetProcessHeap(), 0, OldTable);
    FreeMibTable(Table);

    Table = (PVOID)(ULONG_PTR)0xdeadbeef;
    Error = GetIpForwardTable2(AF_UNSPEC, &Table);
    ok(Error == NO_ERROR, "GetIpForwardTable2(AF_UNSPEC) returned %lu\n", Error);
    if (Error == NO_ERROR) FreeMibTable(Table);
}

static void TestIfTable(void)
{
    PMIB_IF_TABLE2 Table = (PVOID)(ULONG_PTR)0xdeadbeef;
    MIB_IF_ROW2 Row;
    ULONG i, Up = 0;
    DWORD Error;

    Error = GetIfTable2(&Table);
    ok(Error == NO_ERROR, "GetIfTable2 returned %lu\n", Error);
    if (Error != NO_ERROR) return;
    ok(Table != NULL && Table != (PVOID)(ULONG_PTR)0xdeadbeef, "No table\n");
    ok(Table->NumEntries > 0, "No interfaces\n");

    for (i = 0; i < Table->NumEntries; i++)
    {
        /* (0 is NET_IFINDEX_UNSPECIFIED; ReactOS gave it to the loopback interface) */
        ok(Table->Table[i].InterfaceIndex != 0, "Interface %lu: no index\n", i);
        ok(Table->Table[i].InterfaceLuid.Value != 0, "Interface %lu: no LUID\n", i);
        if (Table->Table[i].OperStatus == IfOperStatusUp) Up++;

        ZeroMemory(&Row, sizeof(Row));
        Row.InterfaceLuid = Table->Table[i].InterfaceLuid;
        Error = GetIfEntry2(&Row);
        ok(Error == NO_ERROR, "GetIfEntry2(%lu) returned %lu\n", i, Error);
        ok(Row.InterfaceIndex == Table->Table[i].InterfaceIndex, "GetIfEntry2(%lu): index %lu\n", i, Row.InterfaceIndex);
        ok(Row.Type == Table->Table[i].Type, "GetIfEntry2(%lu): type %lu\n", i, Row.Type);
    }
    ok(Up > 0, "No interface is up\n");
    FreeMibTable(Table);
}

START_TEST(NetioTables)
{
    ULONG Mask = 0;

    ok(ConvertLengthToIpv4Mask(24, &Mask) == NO_ERROR, "ConvertLengthToIpv4Mask(24) failed\n");
    ok(Mask == htonl(0xffffff00), "Mask %08lx\n", Mask);
    ok(ConvertLengthToIpv4Mask(0, &Mask) == NO_ERROR && Mask == 0, "Mask(0) %08lx\n", Mask);
    ok(ConvertLengthToIpv4Mask(32, &Mask) == NO_ERROR && Mask == 0xffffffff, "Mask(32) %08lx\n", Mask);
    ok(ConvertLengthToIpv4Mask(33, &Mask) == ERROR_INVALID_PARAMETER, "Mask(33) accepted\n");

    TestForwardTable();
    TestIfTable();
}
