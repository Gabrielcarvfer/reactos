/*
 * PROJECT:     ReactOS IP Helper API
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Interface identifiers, unicast addresses and best route (Vista)
 *
 * Built on the older functions (GetIfEntry, GetIpAddrTable, GetAdaptersInfo,
 * GetBestRoute), so IPv4 only. An interface's LUID is its type and its index;
 * its name is "<type>_<index>", as Windows' names look ("ethernet_32769").
 * Qt's network module and c-ares (Wireshark, curl) import these.
 */

#include "iphlpapi_private.h"

/* (iphlpapi.h has it only for Vista and later builds) */
#include <netioapi.h>

WINE_DEFAULT_DEBUG_CHANNEL(iphlpapi);

static const struct
{
    ULONG Type;
    const WCHAR *Name;
} InterfaceTypeNames[] =
{
    { IF_TYPE_ETHERNET_CSMACD,   L"ethernet" },
    { IF_TYPE_SOFTWARE_LOOPBACK, L"loopback" },
    { IF_TYPE_IEEE80211,         L"wireless" },
    { IF_TYPE_PPP,               L"ppp" },
    { IF_TYPE_TUNNEL,            L"tunnel" },
    { IF_TYPE_OTHER,             L"other" },
};

static BOOL GetInterfaceRow(NET_IFINDEX Index, PMIB_IFROW Row)
{
    ZeroMemory(Row, sizeof(*Row));
    Row->dwIndex = Index;
    return GetIfEntry(Row) == NO_ERROR;
}

static void MakeLuid(PNET_LUID Luid, const MIB_IFROW *Row)
{
    Luid->Value = 0;
    Luid->Info.IfType = Row->dwType;
    Luid->Info.NetLuidIndex = Row->dwIndex;
}

/* The interface of a LUID, checked against its type */
static BOOL GetLuidRow(const NET_LUID *Luid, PMIB_IFROW Row)
{
    return GetInterfaceRow((NET_IFINDEX)Luid->Info.NetLuidIndex, Row) && Row->dwType == Luid->Info.IfType;
}

static const WCHAR *GetTypeName(ULONG Type)
{
    UINT i;
    for (i = 0; i < ARRAYSIZE(InterfaceTypeNames); i++)
    {
        if (InterfaceTypeNames[i].Type == Type)
            return InterfaceTypeNames[i].Name;
    }
    return L"other";
}

/* Every interface, from GetIfTable (to free with HeapFree) */
static PMIB_IFTABLE GetInterfaces(void)
{
    PMIB_IFTABLE Table = NULL;
    ULONG Size = 0;
    DWORD Error;

    while ((Error = GetIfTable(Table, &Size, FALSE)) == ERROR_INSUFFICIENT_BUFFER)
    {
        HeapFree(GetProcessHeap(), 0, Table);
        Table = HeapAlloc(GetProcessHeap(), 0, Size);
        if (!Table) return NULL;
    }
    if (Error != NO_ERROR)
    {
        HeapFree(GetProcessHeap(), 0, Table);
        return NULL;
    }
    return Table;
}

static PMIB_IPADDRTABLE GetAddresses(void)
{
    PMIB_IPADDRTABLE Table = NULL;
    ULONG Size = 0;
    DWORD Error;

    while ((Error = GetIpAddrTable(Table, &Size, FALSE)) == ERROR_INSUFFICIENT_BUFFER)
    {
        HeapFree(GetProcessHeap(), 0, Table);
        Table = HeapAlloc(GetProcessHeap(), 0, Size);
        if (!Table) return NULL;
    }
    if (Error != NO_ERROR)
    {
        HeapFree(GetProcessHeap(), 0, Table);
        return NULL;
    }
    return Table;
}

static void GetAlias(const MIB_IFROW *Row, PWSTR Alias, SIZE_T Length)
{
    if (Row->dwType == IF_TYPE_SOFTWARE_LOOPBACK)
        _snwprintf(Alias, Length, L"Loopback Pseudo-Interface %lu", Row->dwIndex);
    else if (Row->dwDescrLen)
        _snwprintf(Alias, Length, L"%.*S", (int)Row->dwDescrLen, (const char *)Row->bDescr);
    else
        _snwprintf(Alias, Length, L"%s %lu", GetTypeName(Row->dwType), Row->dwIndex);
    Alias[Length - 1] = UNICODE_NULL;
}

static ULONG MaskLength(DWORD Mask)
{
    ULONG Length = 0;
    Mask = ntohl(Mask);
    while (Mask & 0x80000000)
    {
        Length++;
        Mask <<= 1;
    }
    return Length;
}

DWORD WINAPI ConvertInterfaceIndexToLuid(NET_IFINDEX InterfaceIndex, PNET_LUID InterfaceLuid)
{
    MIB_IFROW Row;

    if (!InterfaceLuid) return ERROR_INVALID_PARAMETER;
    InterfaceLuid->Value = 0;
    if (!GetInterfaceRow(InterfaceIndex, &Row)) return ERROR_FILE_NOT_FOUND;
    MakeLuid(InterfaceLuid, &Row);
    return NO_ERROR;
}

DWORD WINAPI ConvertInterfaceLuidToIndex(const NET_LUID *InterfaceLuid, PNET_IFINDEX InterfaceIndex)
{
    MIB_IFROW Row;

    if (!InterfaceLuid || !InterfaceIndex) return ERROR_INVALID_PARAMETER;
    *InterfaceIndex = NET_IFINDEX_UNSPECIFIED;
    if (!GetLuidRow(InterfaceLuid, &Row)) return ERROR_FILE_NOT_FOUND;
    *InterfaceIndex = Row.dwIndex;
    return NO_ERROR;
}

DWORD WINAPI ConvertInterfaceLuidToNameW(const NET_LUID *InterfaceLuid, PWSTR InterfaceName, SIZE_T Length)
{
    WCHAR Name[IF_MAX_STRING_SIZE + 1];
    MIB_IFROW Row;

    if (!InterfaceLuid || !InterfaceName) return ERROR_INVALID_PARAMETER;
    if (!GetLuidRow(InterfaceLuid, &Row)) return ERROR_FILE_NOT_FOUND;
    _snwprintf(Name, ARRAYSIZE(Name), L"%s_%lu", GetTypeName(Row.dwType), Row.dwIndex);
    Name[ARRAYSIZE(Name) - 1] = UNICODE_NULL;
    if (wcslen(Name) >= Length) return ERROR_NOT_ENOUGH_MEMORY;
    wcscpy(InterfaceName, Name);
    return NO_ERROR;
}

DWORD WINAPI ConvertInterfaceLuidToNameA(const NET_LUID *InterfaceLuid, PSTR InterfaceName, SIZE_T Length)
{
    WCHAR Name[IF_MAX_STRING_SIZE + 1];
    DWORD Error;

    if (!InterfaceName) return ERROR_INVALID_PARAMETER;
    Error = ConvertInterfaceLuidToNameW(InterfaceLuid, Name, ARRAYSIZE(Name));
    if (Error != NO_ERROR) return Error;
    if (wcslen(Name) >= Length) return ERROR_NOT_ENOUGH_MEMORY;
    WideCharToMultiByte(CP_ACP, 0, Name, -1, InterfaceName, (int)Length, NULL, NULL);
    return NO_ERROR;
}

DWORD WINAPI ConvertInterfaceNameToLuidW(const WCHAR *InterfaceName, NET_LUID *InterfaceLuid)
{
    const WCHAR *Separator;
    WCHAR *End;
    ULONG Index;
    UINT i;

    if (!InterfaceLuid) return ERROR_INVALID_PARAMETER;
    InterfaceLuid->Value = 0;
    if (!InterfaceName) return ERROR_INVALID_NAME;
    Separator = wcsrchr(InterfaceName, L'_');
    if (!Separator || !Separator[1]) return ERROR_INVALID_NAME;
    Index = wcstoul(Separator + 1, &End, 10);
    if (*End) return ERROR_INVALID_NAME;

    for (i = 0; i < ARRAYSIZE(InterfaceTypeNames); i++)
    {
        const WCHAR *TypeName = InterfaceTypeNames[i].Name;
        MIB_IFROW Row;

        if (wcslen(TypeName) != (SIZE_T)(Separator - InterfaceName) ||
            _wcsnicmp(InterfaceName, TypeName, Separator - InterfaceName))
            continue;
        if (!GetInterfaceRow(Index, &Row)) return ERROR_INVALID_NAME;
        if (wcscmp(GetTypeName(Row.dwType), TypeName)) return ERROR_INVALID_NAME;
        MakeLuid(InterfaceLuid, &Row);
        return NO_ERROR;
    }
    return ERROR_INVALID_NAME;
}

DWORD WINAPI ConvertInterfaceNameToLuidA(const CHAR *InterfaceName, NET_LUID *InterfaceLuid)
{
    WCHAR Name[IF_MAX_STRING_SIZE + 1];

    if (!InterfaceLuid) return ERROR_INVALID_PARAMETER;
    InterfaceLuid->Value = 0;
    if (!InterfaceName ||
        !MultiByteToWideChar(CP_ACP, 0, InterfaceName, -1, Name, ARRAYSIZE(Name)))
        return ERROR_INVALID_NAME;
    return ConvertInterfaceNameToLuidW(Name, InterfaceLuid);
}

DWORD WINAPI ConvertInterfaceLuidToAlias(const NET_LUID *InterfaceLuid, PWSTR InterfaceAlias, SIZE_T Length)
{
    WCHAR Alias[IF_MAX_STRING_SIZE + 1];
    MIB_IFROW Row;

    if (!InterfaceLuid || !InterfaceAlias) return ERROR_INVALID_PARAMETER;
    if (!GetLuidRow(InterfaceLuid, &Row)) return ERROR_FILE_NOT_FOUND;
    GetAlias(&Row, Alias, ARRAYSIZE(Alias));
    if (wcslen(Alias) >= Length) return ERROR_NOT_ENOUGH_MEMORY;
    wcscpy(InterfaceAlias, Alias);
    return NO_ERROR;
}

DWORD WINAPI ConvertInterfaceAliasToLuid(const WCHAR *InterfaceAlias, PNET_LUID InterfaceLuid)
{
    PMIB_IFTABLE Table;
    DWORD i, Error = ERROR_INVALID_PARAMETER;

    if (!InterfaceAlias || !InterfaceLuid) return ERROR_INVALID_PARAMETER;
    InterfaceLuid->Value = 0;
    if (!(Table = GetInterfaces())) return ERROR_NOT_ENOUGH_MEMORY;
    for (i = 0; i < Table->dwNumEntries; i++)
    {
        WCHAR Alias[IF_MAX_STRING_SIZE + 1];

        GetAlias(&Table->table[i], Alias, ARRAYSIZE(Alias));
        if (!_wcsicmp(Alias, InterfaceAlias))
        {
            MakeLuid(InterfaceLuid, &Table->table[i]);
            Error = NO_ERROR;
            break;
        }
    }
    HeapFree(GetProcessHeap(), 0, Table);
    return Error;
}

/* The adapter GUID of an interface (from its name, "{...}"); none for the
 * loopback interface */
static BOOL GetInterfaceGuid(NET_IFINDEX Index, GUID *Guid)
{
    PIP_ADAPTER_INFO Adapters = NULL, Adapter;
    ULONG Size = 0;
    BOOL Found = FALSE;

    if (GetAdaptersInfo(NULL, &Size) != ERROR_BUFFER_OVERFLOW) return FALSE;
    if (!(Adapters = HeapAlloc(GetProcessHeap(), 0, Size))) return FALSE;
    if (GetAdaptersInfo(Adapters, &Size) == NO_ERROR)
    {
        for (Adapter = Adapters; Adapter; Adapter = Adapter->Next)
        {
            WCHAR Name[MAX_ADAPTER_NAME_LENGTH + 4];
            UNICODE_STRING String;

            if (Adapter->Index != Index) continue;
            MultiByteToWideChar(CP_ACP, 0, Adapter->AdapterName, -1, Name, ARRAYSIZE(Name));
            RtlInitUnicodeString(&String, Name);
            Found = NT_SUCCESS(RtlGUIDFromString(&String, Guid));
            break;
        }
    }
    HeapFree(GetProcessHeap(), 0, Adapters);
    return Found;
}

DWORD WINAPI ConvertInterfaceLuidToGuid(const NET_LUID *InterfaceLuid, GUID *InterfaceGuid)
{
    MIB_IFROW Row;

    if (!InterfaceLuid || !InterfaceGuid) return ERROR_INVALID_PARAMETER;
    ZeroMemory(InterfaceGuid, sizeof(*InterfaceGuid));
    if (!GetLuidRow(InterfaceLuid, &Row)) return ERROR_FILE_NOT_FOUND;
    GetInterfaceGuid(Row.dwIndex, InterfaceGuid);
    return NO_ERROR;
}

DWORD WINAPI ConvertInterfaceGuidToLuid(const GUID *InterfaceGuid, PNET_LUID InterfaceLuid)
{
    PMIB_IFTABLE Table;
    DWORD i, Error = ERROR_INVALID_PARAMETER;

    if (!InterfaceGuid || !InterfaceLuid) return ERROR_INVALID_PARAMETER;
    InterfaceLuid->Value = 0;
    if (!(Table = GetInterfaces())) return ERROR_NOT_ENOUGH_MEMORY;
    for (i = 0; i < Table->dwNumEntries; i++)
    {
        GUID Guid;

        if (GetInterfaceGuid(Table->table[i].dwIndex, &Guid) && IsEqualGUID(&Guid, InterfaceGuid))
        {
            MakeLuid(InterfaceLuid, &Table->table[i]);
            Error = NO_ERROR;
            break;
        }
    }
    HeapFree(GetProcessHeap(), 0, Table);
    return Error;
}

NET_IFINDEX WINAPI if_nametoindex(PCSTR InterfaceName)
{
    NET_IFINDEX Index;
    NET_LUID Luid;

    if (ConvertInterfaceNameToLuidA(InterfaceName, &Luid) != NO_ERROR ||
        ConvertInterfaceLuidToIndex(&Luid, &Index) != NO_ERROR)
        return 0;
    return Index;
}

PCHAR WINAPI if_indextoname(NET_IFINDEX InterfaceIndex, PCHAR InterfaceName)
{
    NET_LUID Luid;

    /* (The buffer has IF_NAMESIZE characters) */
    if (!InterfaceName ||
        ConvertInterfaceIndexToLuid(InterfaceIndex, &Luid) != NO_ERROR ||
        ConvertInterfaceLuidToNameA(&Luid, InterfaceName, IF_MAX_STRING_SIZE + 1) != NO_ERROR)
        return NULL;
    return InterfaceName;
}

VOID WINAPI FreeMibTable(PVOID Memory)
{
    HeapFree(GetProcessHeap(), 0, Memory);
}

DWORD WINAPI GetUnicastIpAddressTable(ADDRESS_FAMILY Family, PMIB_UNICASTIPADDRESS_TABLE *Table)
{
    PMIB_IPADDRTABLE Addresses = NULL;
    PMIB_UNICASTIPADDRESS_TABLE Result;
    DWORD i, Count = 0;

    if (!Table || (Family != AF_UNSPEC && Family != AF_INET && Family != AF_INET6))
        return ERROR_INVALID_PARAMETER;
    *Table = NULL;

    /* IPv4 only (there is no IPv6 stack) */
    if (Family != AF_INET6 && !(Addresses = GetAddresses()))
        return ERROR_NOT_ENOUGH_MEMORY;

    Result = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                       FIELD_OFFSET(MIB_UNICASTIPADDRESS_TABLE, Table[Addresses ? Addresses->dwNumEntries : 0]));
    if (!Result)
    {
        HeapFree(GetProcessHeap(), 0, Addresses);
        return ERROR_NOT_ENOUGH_MEMORY;
    }

    for (i = 0; Addresses && i < Addresses->dwNumEntries; i++)
    {
        const MIB_IPADDRROW *Address = &Addresses->table[i];
        PMIB_UNICASTIPADDRESS_ROW Row = &Result->Table[Count];
        MIB_IFROW IfRow;

        if (!Address->dwAddr || !GetInterfaceRow(Address->dwIndex, &IfRow))
            continue;
        Row->Address.Ipv4.sin_family = AF_INET;
        Row->Address.Ipv4.sin_addr.s_addr = Address->dwAddr;
        MakeLuid(&Row->InterfaceLuid, &IfRow);
        Row->InterfaceIndex = Address->dwIndex;
        Row->PrefixOrigin = IpPrefixOriginManual;
        Row->SuffixOrigin = IpSuffixOriginManual;
        Row->ValidLifetime = 0xffffffff;
        Row->PreferredLifetime = 0xffffffff;
        Row->OnLinkPrefixLength = (UINT8)MaskLength(Address->dwMask);
        Row->DadState = IpDadStatePreferred;
        Count++;
    }
    Result->NumEntries = Count;
    HeapFree(GetProcessHeap(), 0, Addresses);
    *Table = Result;
    return NO_ERROR;
}

DWORD WINAPI GetBestRoute2(NET_LUID *InterfaceLuid, NET_IFINDEX InterfaceIndex, const SOCKADDR_INET *SourceAddress,
                           const SOCKADDR_INET *DestinationAddress, ULONG AddressSortOptions,
                           PMIB_IPFORWARD_ROW2 BestRoute, SOCKADDR_INET *BestSourceAddress)
{
    PMIB_IPADDRTABLE Addresses;
    MIB_IPFORWARDROW Route;
    MIB_IFROW IfRow;
    DWORD Destination, Source = 0, i, Error;

    UNREFERENCED_PARAMETER(InterfaceLuid);
    UNREFERENCED_PARAMETER(InterfaceIndex);
    UNREFERENCED_PARAMETER(AddressSortOptions);

    if (!DestinationAddress || !BestRoute || !BestSourceAddress)
        return ERROR_INVALID_PARAMETER;
    ZeroMemory(BestRoute, sizeof(*BestRoute));
    ZeroMemory(BestSourceAddress, sizeof(*BestSourceAddress));

    /* IPv4 only (there is no IPv6 stack) */
    if (DestinationAddress->si_family != AF_INET)
        return ERROR_NETWORK_UNREACHABLE;
    Destination = DestinationAddress->Ipv4.sin_addr.s_addr;
    if (SourceAddress && SourceAddress->si_family == AF_INET)
        Source = SourceAddress->Ipv4.sin_addr.s_addr;

    Error = GetBestRoute(Destination, Source, &Route);
    if (Error != NO_ERROR) return Error;

    if (GetInterfaceRow(Route.dwForwardIfIndex, &IfRow))
        MakeLuid(&BestRoute->InterfaceLuid, &IfRow);
    BestRoute->InterfaceIndex = Route.dwForwardIfIndex;
    BestRoute->DestinationPrefix.Prefix.Ipv4.sin_family = AF_INET;
    BestRoute->DestinationPrefix.Prefix.Ipv4.sin_addr.s_addr = Route.dwForwardDest;
    BestRoute->DestinationPrefix.PrefixLength = (UINT8)MaskLength(Route.dwForwardMask);
    BestRoute->NextHop.Ipv4.sin_family = AF_INET;
    BestRoute->NextHop.Ipv4.sin_addr.s_addr = Route.dwForwardNextHop;
    BestRoute->ValidLifetime = 0xffffffff;
    BestRoute->PreferredLifetime = 0xffffffff;
    BestRoute->Metric = Route.dwForwardMetric1;
    BestRoute->Protocol = (NL_ROUTE_PROTOCOL)Route.dwForwardProto;
    BestRoute->Loopback = ((ntohl(Destination) >> 24) == 127);
    BestRoute->Origin = NlroManual;

    /* The source: the given one, or an address of the interface */
    BestSourceAddress->Ipv4.sin_family = AF_INET;
    BestSourceAddress->Ipv4.sin_addr.s_addr = Source;
    if (!Source && (Addresses = GetAddresses()))
    {
        for (i = 0; i < Addresses->dwNumEntries; i++)
        {
            if (Addresses->table[i].dwIndex == Route.dwForwardIfIndex && Addresses->table[i].dwAddr)
            {
                BestSourceAddress->Ipv4.sin_addr.s_addr = Addresses->table[i].dwAddr;
                break;
            }
        }
        HeapFree(GetProcessHeap(), 0, Addresses);
    }
    return NO_ERROR;
}

DWORD WINAPI ConvertLengthToIpv4Mask(ULONG MaskLength, PULONG Mask)
{
    if (!Mask) return ERROR_INVALID_PARAMETER;
    if (MaskLength > 32)
    {
        *Mask = INADDR_NONE;
        return ERROR_INVALID_PARAMETER;
    }
    *Mask = MaskLength ? htonl(~0u << (32 - MaskLength)) : 0;
    return NO_ERROR;
}

/* An interface as a MIB_IF_ROW2 (IPv4 interfaces of the TCP/IP driver) */
static void FillIfRow2(PMIB_IF_ROW2 Row, const MIB_IFROW *IfRow)
{
    ZeroMemory(Row, sizeof(*Row));
    MakeLuid(&Row->InterfaceLuid, IfRow);
    Row->InterfaceIndex = IfRow->dwIndex;
    GetInterfaceGuid(IfRow->dwIndex, &Row->InterfaceGuid);
    GetAlias(IfRow, Row->Alias, ARRAYSIZE(Row->Alias));
    _snwprintf(Row->Description, ARRAYSIZE(Row->Description), L"%.*S",
               (int)min(IfRow->dwDescrLen, IF_MAX_STRING_SIZE), (const char *)IfRow->bDescr);
    Row->PhysicalAddressLength = min(IfRow->dwPhysAddrLen, IF_MAX_PHYS_ADDRESS_LENGTH);
    CopyMemory(Row->PhysicalAddress, IfRow->bPhysAddr, Row->PhysicalAddressLength);
    CopyMemory(Row->PermanentPhysicalAddress, IfRow->bPhysAddr, Row->PhysicalAddressLength);
    Row->Mtu = IfRow->dwMtu;
    Row->Type = IfRow->dwType;
    Row->TunnelType = TUNNEL_TYPE_NONE;
    Row->MediaType = (IfRow->dwType == IF_TYPE_SOFTWARE_LOOPBACK) ? (NDIS_MEDIUM)17 /* NdisMediumLoopback */ : NdisMedium802_3;
    Row->PhysicalMediumType = (IfRow->dwType == IF_TYPE_SOFTWARE_LOOPBACK) ? NdisPhysicalMediumUnspecified
                                                                           : NdisPhysicalMedium802_3;
    Row->AccessType = (IfRow->dwType == IF_TYPE_SOFTWARE_LOOPBACK) ? NET_IF_ACCESS_LOOPBACK : NET_IF_ACCESS_BROADCAST;
    Row->DirectionType = NET_IF_DIRECTION_SENDRECEIVE;
    Row->InterfaceAndOperStatusFlags.HardwareInterface = (IfRow->dwType != IF_TYPE_SOFTWARE_LOOPBACK);
    Row->InterfaceAndOperStatusFlags.ConnectorPresent = (IfRow->dwType != IF_TYPE_SOFTWARE_LOOPBACK);
    Row->OperStatus = (IfRow->dwOperStatus >= IF_OPER_STATUS_CONNECTED) ? IfOperStatusUp : IfOperStatusDown;
    Row->AdminStatus = IfRow->dwAdminStatus ? NET_IF_ADMIN_STATUS_UP : NET_IF_ADMIN_STATUS_DOWN;
    Row->MediaConnectState = (Row->OperStatus == IfOperStatusUp) ? MediaConnectStateConnected
                                                                  : MediaConnectStateDisconnected;
    Row->InterfaceAndOperStatusFlags.NotMediaConnected = (Row->MediaConnectState != MediaConnectStateConnected);
    Row->ConnectionType = NET_IF_CONNECTION_DEDICATED;
    Row->TransmitLinkSpeed = IfRow->dwSpeed;
    Row->ReceiveLinkSpeed = IfRow->dwSpeed;
    Row->InOctets = IfRow->dwInOctets;
    Row->InUcastPkts = IfRow->dwInUcastPkts;
    Row->InNUcastPkts = IfRow->dwInNUcastPkts;
    Row->InDiscards = IfRow->dwInDiscards;
    Row->InErrors = IfRow->dwInErrors;
    Row->InUnknownProtos = IfRow->dwInUnknownProtos;
    Row->OutOctets = IfRow->dwOutOctets;
    Row->OutUcastPkts = IfRow->dwOutUcastPkts;
    Row->OutNUcastPkts = IfRow->dwOutNUcastPkts;
    Row->OutDiscards = IfRow->dwOutDiscards;
    Row->OutErrors = IfRow->dwOutErrors;
    Row->OutQLen = IfRow->dwOutQLen;
}

DWORD WINAPI GetIfEntry2(PMIB_IF_ROW2 Row)
{
    MIB_IFROW IfRow;

    if (!Row) return ERROR_INVALID_PARAMETER;
    /* The interface is given by its LUID or, without one, its index */
    if (Row->InterfaceLuid.Value)
    {
        if (!GetLuidRow(&Row->InterfaceLuid, &IfRow)) return ERROR_FILE_NOT_FOUND;
    }
    else if (!Row->InterfaceIndex || !GetInterfaceRow(Row->InterfaceIndex, &IfRow))
    {
        return ERROR_FILE_NOT_FOUND;
    }
    FillIfRow2(Row, &IfRow);
    return NO_ERROR;
}

DWORD WINAPI GetIfEntry2Ex(MIB_IF_ENTRY_LEVEL Level, PMIB_IF_ROW2 Row)
{
    UNREFERENCED_PARAMETER(Level);
    return GetIfEntry2(Row);
}

DWORD WINAPI GetIfTable2Ex(MIB_IF_TABLE_LEVEL Level, PMIB_IF_TABLE2 *Table)
{
    PMIB_IFTABLE Interfaces;
    PMIB_IF_TABLE2 Result;
    DWORD i;

    UNREFERENCED_PARAMETER(Level);
    if (!Table) return ERROR_INVALID_PARAMETER;
    *Table = NULL;
    if (!(Interfaces = GetInterfaces())) return ERROR_NOT_ENOUGH_MEMORY;

    Result = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                       FIELD_OFFSET(MIB_IF_TABLE2, Table[Interfaces->dwNumEntries]));
    if (!Result)
    {
        HeapFree(GetProcessHeap(), 0, Interfaces);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    for (i = 0; i < Interfaces->dwNumEntries; i++)
        FillIfRow2(&Result->Table[i], &Interfaces->table[i]);
    Result->NumEntries = Interfaces->dwNumEntries;
    HeapFree(GetProcessHeap(), 0, Interfaces);
    *Table = Result;
    return NO_ERROR;
}

DWORD WINAPI GetIfTable2(PMIB_IF_TABLE2 *Table)
{
    return GetIfTable2Ex(MibIfTableNormal, Table);
}

DWORD WINAPI GetIpForwardTable2(ADDRESS_FAMILY Family, PMIB_IPFORWARD_TABLE2 *Table)
{
    PMIB_IPFORWARDTABLE Routes = NULL;
    PMIB_IPFORWARD_TABLE2 Result;
    ULONG Size = 0;
    DWORD Error, i;

    if (!Table || (Family != AF_UNSPEC && Family != AF_INET && Family != AF_INET6))
        return ERROR_INVALID_PARAMETER;
    *Table = NULL;

    /* IPv4 only (there is no IPv6 stack) */
    if (Family != AF_INET6)
    {
        while ((Error = GetIpForwardTable(Routes, &Size, FALSE)) == ERROR_INSUFFICIENT_BUFFER)
        {
            HeapFree(GetProcessHeap(), 0, Routes);
            if (!(Routes = HeapAlloc(GetProcessHeap(), 0, Size))) return ERROR_NOT_ENOUGH_MEMORY;
        }
        if (Error != NO_ERROR)
        {
            HeapFree(GetProcessHeap(), 0, Routes);
            Routes = NULL;
            if (Error != ERROR_NO_DATA) return Error;
        }
    }

    Result = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                       FIELD_OFFSET(MIB_IPFORWARD_TABLE2, Table[Routes ? Routes->dwNumEntries : 0]));
    if (!Result)
    {
        HeapFree(GetProcessHeap(), 0, Routes);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    for (i = 0; Routes && i < Routes->dwNumEntries; i++)
    {
        const MIB_IPFORWARDROW *Route = &Routes->table[i];
        PMIB_IPFORWARD_ROW2 Row = &Result->Table[i];
        MIB_IFROW IfRow;

        if (GetInterfaceRow(Route->dwForwardIfIndex, &IfRow))
            MakeLuid(&Row->InterfaceLuid, &IfRow);
        Row->InterfaceIndex = Route->dwForwardIfIndex;
        Row->DestinationPrefix.Prefix.Ipv4.sin_family = AF_INET;
        Row->DestinationPrefix.Prefix.Ipv4.sin_addr.s_addr = Route->dwForwardDest;
        Row->DestinationPrefix.PrefixLength = (UINT8)MaskLength(Route->dwForwardMask);
        Row->NextHop.Ipv4.sin_family = AF_INET;
        Row->NextHop.Ipv4.sin_addr.s_addr = Route->dwForwardNextHop;
        Row->ValidLifetime = 0xffffffff;
        Row->PreferredLifetime = 0xffffffff;
        Row->Metric = Route->dwForwardMetric1;
        Row->Protocol = (NL_ROUTE_PROTOCOL)Route->dwForwardProto;
        Row->Loopback = ((ntohl(Route->dwForwardDest) >> 24) == 127);
        Row->Age = Route->dwForwardAge;
        Row->Origin = NlroManual;
    }
    Result->NumEntries = Routes ? Routes->dwNumEntries : 0;
    HeapFree(GetProcessHeap(), 0, Routes);
    *Table = Result;
    return NO_ERROR;
}

DWORD WINAPI GetUnicastIpAddressEntry(PMIB_UNICASTIPADDRESS_ROW Row)
{
    PMIB_UNICASTIPADDRESS_TABLE Table;
    DWORD Error, i;

    if (!Row) return ERROR_INVALID_PARAMETER;
    if (Row->Address.si_family != AF_INET) return ERROR_NOT_FOUND;
    Error = GetUnicastIpAddressTable(AF_INET, &Table);
    if (Error != NO_ERROR) return Error;

    Error = ERROR_NOT_FOUND;
    for (i = 0; i < Table->NumEntries; i++)
    {
        const MIB_UNICASTIPADDRESS_ROW *Entry = &Table->Table[i];

        if (Entry->Address.Ipv4.sin_addr.s_addr != Row->Address.Ipv4.sin_addr.s_addr)
            continue;
        if ((Row->InterfaceLuid.Value && Row->InterfaceLuid.Value != Entry->InterfaceLuid.Value) ||
            (!Row->InterfaceLuid.Value && Row->InterfaceIndex && Row->InterfaceIndex != Entry->InterfaceIndex))
            continue;
        *Row = *Entry;
        Error = NO_ERROR;
        break;
    }
    FreeMibTable(Table);
    return Error;
}
