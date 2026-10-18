#ifndef _NETIOAPI_H_
#define _NETIOAPI_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _IPHLPAPI_H
    #define NETIO_STATUS DWORD
    #define NETIOAPI_API_ WINAPI
#else
    #define NETIO_STATUS NTSTATUS
    #define NETIOAPI_API_ NTAPI
#endif

#define NETIOAPI_API NETIO_STATUS NETIOAPI_API_

#ifdef _WS2IPDEF_
#include <ntddndis.h>
#include <ifdef.h>

typedef struct _MIB_IF_ROW2 {
    NET_LUID InterfaceLuid;
    NET_IFINDEX InterfaceIndex;
    GUID InterfaceGuid;
    WCHAR Alias[IF_MAX_STRING_SIZE + 1];
    WCHAR Description[IF_MAX_STRING_SIZE + 1];
    ULONG PhysicalAddressLength;
    UCHAR PhysicalAddress[IF_MAX_PHYS_ADDRESS_LENGTH];
    UCHAR PermanentPhysicalAddress[IF_MAX_PHYS_ADDRESS_LENGTH];
    ULONG Mtu;
    IFTYPE Type;
    TUNNEL_TYPE TunnelType;
    NDIS_MEDIUM MediaType;
    NDIS_PHYSICAL_MEDIUM PhysicalMediumType;
    NET_IF_ACCESS_TYPE AccessType;
    NET_IF_DIRECTION_TYPE DirectionType;
    struct {
        BOOLEAN HardwareInterface : 1;
        BOOLEAN FilterInterface : 1;
        BOOLEAN ConnectorPresent : 1;
        BOOLEAN NotAuthenticated : 1;
        BOOLEAN NotMediaConnected : 1;
        BOOLEAN Paused : 1;
        BOOLEAN LowPower : 1;
        BOOLEAN EndPointInterface : 1;
    } InterfaceAndOperStatusFlags;
    IF_OPER_STATUS OperStatus;
    NET_IF_ADMIN_STATUS AdminStatus;
    NET_IF_MEDIA_CONNECT_STATE MediaConnectState;
    NET_IF_NETWORK_GUID NetworkGuid;
    NET_IF_CONNECTION_TYPE ConnectionType;
    ULONG64 TransmitLinkSpeed;
    ULONG64 ReceiveLinkSpeed;
    ULONG64 InOctets;
    ULONG64 InUcastPkts;
    ULONG64 InNUcastPkts;
    ULONG64 InDiscards;
    ULONG64 InErrors;
    ULONG64 InUnknownProtos;
    ULONG64 InUcastOctets;
    ULONG64 InMulticastOctets;
    ULONG64 InBroadcastOctets;
    ULONG64 OutOctets;
    ULONG64 OutUcastPkts;
    ULONG64 OutNUcastPkts;
    ULONG64 OutDiscards;
    ULONG64 OutErrors;
    ULONG64 OutUcastOctets;
    ULONG64 OutMulticastOctets;
    ULONG64 OutBroadcastOctets;
    ULONG64 OutQLen;
} MIB_IF_ROW2, *PMIB_IF_ROW2;

typedef struct _MIB_IF_TABLE2
{
    ULONG NumEntries;
    MIB_IF_ROW2 Table[1];
} MIB_IF_TABLE2, *PMIB_IF_TABLE2;

NETIOAPI_API GetIfEntry2(IN OUT PMIB_IF_ROW2 Row);
NETIOAPI_API GetIfTable2(OUT PMIB_IF_TABLE2 *Table);

#include <nldef.h>

typedef struct _IP_ADDRESS_PREFIX {
    SOCKADDR_INET Prefix;
    UINT8 PrefixLength;
} IP_ADDRESS_PREFIX, *PIP_ADDRESS_PREFIX;

typedef struct _MIB_UNICASTIPADDRESS_ROW {
    SOCKADDR_INET Address;
    NET_LUID InterfaceLuid;
    NET_IFINDEX InterfaceIndex;
    NL_PREFIX_ORIGIN PrefixOrigin;
    NL_SUFFIX_ORIGIN SuffixOrigin;
    ULONG ValidLifetime;
    ULONG PreferredLifetime;
    UINT8 OnLinkPrefixLength;
    BOOLEAN SkipAsSource;
    NL_DAD_STATE DadState;
    SCOPE_ID ScopeId;
    LARGE_INTEGER CreationTimeStamp;
} MIB_UNICASTIPADDRESS_ROW, *PMIB_UNICASTIPADDRESS_ROW;

typedef struct _MIB_UNICASTIPADDRESS_TABLE {
    ULONG NumEntries;
    MIB_UNICASTIPADDRESS_ROW Table[1];
} MIB_UNICASTIPADDRESS_TABLE, *PMIB_UNICASTIPADDRESS_TABLE;

typedef struct _MIB_IPFORWARD_ROW2 {
    NET_LUID InterfaceLuid;
    NET_IFINDEX InterfaceIndex;
    IP_ADDRESS_PREFIX DestinationPrefix;
    SOCKADDR_INET NextHop;
    UCHAR SitePrefixLength;
    ULONG ValidLifetime;
    ULONG PreferredLifetime;
    ULONG Metric;
    NL_ROUTE_PROTOCOL Protocol;
    BOOLEAN Loopback;
    BOOLEAN AutoconfigureAddress;
    BOOLEAN Publish;
    BOOLEAN Immortal;
    ULONG Age;
    NL_ROUTE_ORIGIN Origin;
} MIB_IPFORWARD_ROW2, *PMIB_IPFORWARD_ROW2;

typedef struct _MIB_IPFORWARD_TABLE2 {
    ULONG NumEntries;
    MIB_IPFORWARD_ROW2 Table[1];
} MIB_IPFORWARD_TABLE2, *PMIB_IPFORWARD_TABLE2;

typedef enum _MIB_IF_TABLE_LEVEL {
    MibIfTableNormal,
    MibIfTableRaw,
    MibIfTableNormalWithoutStatistics
} MIB_IF_TABLE_LEVEL, *PMIB_IF_TABLE_LEVEL;

typedef enum _MIB_IF_ENTRY_LEVEL {
    MibIfEntryNormal = 0,
    MibIfEntryNormalWithoutStatistics = 2
} MIB_IF_ENTRY_LEVEL, *PMIB_IF_ENTRY_LEVEL;

NETIOAPI_API GetIfEntry2Ex(IN MIB_IF_ENTRY_LEVEL Level, IN OUT PMIB_IF_ROW2 Row);
NETIOAPI_API GetIfTable2Ex(IN MIB_IF_TABLE_LEVEL Level, OUT PMIB_IF_TABLE2 *Table);
NETIOAPI_API GetIpForwardTable2(IN ADDRESS_FAMILY Family, OUT PMIB_IPFORWARD_TABLE2 *Table);
NETIOAPI_API GetUnicastIpAddressEntry(IN OUT PMIB_UNICASTIPADDRESS_ROW Row);
NETIOAPI_API ConvertLengthToIpv4Mask(IN ULONG MaskLength, OUT PULONG Mask);
NETIOAPI_API GetUnicastIpAddressTable(IN ADDRESS_FAMILY Family, OUT PMIB_UNICASTIPADDRESS_TABLE *Table);
NETIOAPI_API GetBestRoute2(IN NET_LUID *InterfaceLuid OPTIONAL, IN NET_IFINDEX InterfaceIndex,
                           IN CONST SOCKADDR_INET *SourceAddress OPTIONAL,
                           IN CONST SOCKADDR_INET *DestinationAddress, IN ULONG AddressSortOptions,
                           OUT PMIB_IPFORWARD_ROW2 BestRoute, OUT SOCKADDR_INET *BestSourceAddress);

#endif

#include <ifdef.h>

NETIOAPI_API ConvertInterfaceAliasToLuid(IN CONST WCHAR *InterfaceAlias, OUT PNET_LUID InterfaceLuid);
NETIOAPI_API ConvertInterfaceGuidToLuid(IN CONST GUID *InterfaceGuid, OUT PNET_LUID InterfaceLuid);
NETIOAPI_API ConvertInterfaceIndexToLuid(IN NET_IFINDEX InterfaceIndex, OUT PNET_LUID InterfaceLuid);
NETIOAPI_API ConvertInterfaceLuidToAlias(IN CONST NET_LUID *InterfaceLuid, OUT PWSTR InterfaceAlias, IN SIZE_T Length);
NETIOAPI_API ConvertInterfaceLuidToGuid(IN CONST NET_LUID *InterfaceLuid, OUT GUID *InterfaceGuid);
NETIOAPI_API ConvertInterfaceLuidToIndex(IN CONST NET_LUID *InterfaceLuid, OUT PNET_IFINDEX InterfaceIndex);
NETIOAPI_API ConvertInterfaceLuidToNameA(IN CONST NET_LUID *InterfaceLuid, OUT PSTR InterfaceName, IN SIZE_T Length);
NETIOAPI_API ConvertInterfaceLuidToNameW(IN CONST NET_LUID *InterfaceLuid, OUT PWSTR InterfaceName, IN SIZE_T Length);
NETIOAPI_API ConvertInterfaceNameToLuidA(IN CONST CHAR *InterfaceName, OUT NET_LUID *InterfaceLuid);
NETIOAPI_API ConvertInterfaceNameToLuidW(IN CONST WCHAR *InterfaceName, OUT NET_LUID *InterfaceLuid);
VOID NETIOAPI_API_ FreeMibTable(IN PVOID Memory);

#ifdef __cplusplus
}
#endif

#endif
