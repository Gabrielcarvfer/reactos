#include <lwip/netif.h>
#include <lwip/tcpip.h>

typedef struct netif* PNETIF;

VOID LibTCPSetLwipThread(VOID);

void
sys_shutdown(void);

void
LibIPInsertPacket(void *ifarg,
                  const void *const data,
                  const u32_t size)
{
    struct pbuf *p;

    ASSERT(ifarg);
    ASSERT(data);
    ASSERT(size > 0);

    p = pbuf_alloc(PBUF_RAW, size, PBUF_RAM);
    if (p)
    {
        ASSERT(p->tot_len == p->len);
        ASSERT(p->len == size);

        RtlCopyMemory(p->payload, data, p->len);

        ((PNETIF)ifarg)->input(p, (PNETIF)ifarg);
    }
}

static
void
LibIPThreadStarted(void *arg)
{
    /* (in the lwIP thread) */
    LibTCPSetLwipThread();
}

void
LibIPInitialize(void)
{
    /* This completes asynchronously */
    tcpip_init(LibIPThreadStarted, NULL);
}

void
LibIPShutdown(void)
{
    /* This is synchronous */
    sys_shutdown();
}
