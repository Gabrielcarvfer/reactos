
#define STANDALONE
#include <apitest.h>

extern void func_AfdPoll(void);
extern void func_bind(void);
extern void func_broadcast(void);
extern void func_close(void);
extern void func_getaddrinfo(void);
extern void func_gethostname(void);
extern void func_getnameinfo(void);
extern void func_getservbyname(void);
extern void func_getservbyport(void);
extern void func_ioctlsocket(void);
extern void func_IoCompletionPort(void);
extern void func_nonblocking(void);
extern void func_nostartup(void);
extern void func_open_osfhandle(void);
extern void func_recv(void);
extern void func_RecvFromCancel(void);
extern void func_SelectTimeout(void);
extern void func_send(void);
extern void func_SkipCompletionPort(void);
extern void func_SocketNames(void);
extern void func_TcpConnect(void);
extern void func_WSAAsync(void);
extern void func_WSAIoctl(void);
extern void func_WSARecv(void);
extern void func_WSAStartup(void);

const struct test winetest_testlist[] =
{
    { "AfdPoll", func_AfdPoll },
    { "bind", func_bind },
    { "broadcast", func_broadcast },
    { "close", func_close },
    { "getaddrinfo", func_getaddrinfo },
    { "gethostname", func_gethostname },
    { "getnameinfo", func_getnameinfo },
    { "getservbyname", func_getservbyname },
    { "getservbyport", func_getservbyport },
    { "ioctlsocket", func_ioctlsocket },
    { "IoCompletionPort", func_IoCompletionPort },
    { "nonblocking", func_nonblocking },
    { "nostartup", func_nostartup },
    { "open_osfhandle", func_open_osfhandle },
    { "recv", func_recv },
    { "RecvFromCancel", func_RecvFromCancel },
    { "SelectTimeout", func_SelectTimeout },
    { "send", func_send },
    { "SkipCompletionPort", func_SkipCompletionPort },
    { "SocketNames", func_SocketNames },
    { "TcpConnect", func_TcpConnect },
    { "WSAAsync", func_WSAAsync },
    { "WSAIoctl", func_WSAIoctl },
    { "WSARecv", func_WSARecv },
    { "WSAStartup", func_WSAStartup },
    { 0, 0 }
};

