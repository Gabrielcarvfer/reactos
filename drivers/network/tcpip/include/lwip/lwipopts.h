/*
   ------------------------------------
   ---------- Memory options ----------
   ------------------------------------
*/

/* This combo allows us to implement malloc, free, and realloc ourselves */
#define MEM_LIBC_MALLOC                 1
#define MEMP_MEM_MALLOC                 1

/* tcpip calls lwIP under its core lock in the calling thread (received
 * packets in the receive worker, requests in the requester's thread) instead
 * of queueing every packet and request to the lwIP thread and waiting: those
 * thread switches cost more than the protocol work itself. The lwIP thread
 * only runs the timers now */
#define LWIP_TCPIP_CORE_LOCKING         1
#define LWIP_TCPIP_CORE_LOCKING_INPUT   1
int sys_core_locked(void);
#define LWIP_ASSERT_CORE_LOCKED()       LWIP_ASSERT("core locked", sys_core_locked())

#define MEM_ALIGNMENT                   4

#define LWIP_ARP                        0

#define ETH_PAD_SIZE                    2

#define IP_REASS_MAX_PBUFS              0xFFFFFFFF

#define IP_DEFAULT_TTL                  128

#define IP_SOF_BROADCAST                1

#define IP_SOF_BROADCAST_RECV           1

#define LWIP_ICMP                       0

#define LWIP_RAW                        0

#define LWIP_UDP                        0

#define SO_REUSE                        1

#define SO_REUSE_RXTOALL                1

/* FIXME: These MSS and TCP Window definitions assume an MTU
 * of 1500. We need to add some code to lwIP which would allow us
 * to change these values based upon the interface we are
 * using. Currently ReactOS only supports Ethernet so we're
 * fine for now but it does need to be fixed later when we
 * add support for other transport mediums */
#define TCP_MSS                         1460

#define TCP_WND                         0xFFFF

#define TCP_SND_BUF                     TCP_WND

#define TCP_MAXRTX                      8

#define TCP_SYNMAXRTX                   4

#define TCP_LISTEN_BACKLOG              1

#define LWIP_TCP_TIMESTAMPS             1

#define LWIP_SOCKET                     0

#define LWIP_NETCONN                    0

#define LWIP_STATS                      0

#define ICMP_STATS                      0

/*
   ---------------------------------------
   ---------- Debugging options ----------
   ---------------------------------------
*/

/**
 * TCP_DEBUG: Enable debugging for TCP.
 */
#define TCP_DEBUG                       LWIP_DBG_ON
