/*
 * PROJECT:     ReactOS api tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     The connection pool lets a kept-alive connection expire safely
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 *
 * winhttp keeps a finished request's connection for 30 s; its collector then
 * closes it. Closing a host's last connection freed the host while the
 * collector still walked the host's connection list (a read of freed memory,
 * found with page heap in Chrome's updater). The test runs itself again with
 * page heap (Image File Execution Options) and lets a connection expire: the
 * read of freed memory faults there.
 */

#include <apitest.h>
#include <winsock2.h>
#include <windows.h>
#include <winhttp.h>

/* (Loaded at run time: WinHttpOpen checks that winsock is not loaded before winhttp) */
static int (WINAPI *pWSAStartup)(WORD, LPWSADATA);
static SOCKET (WINAPI *psocket)(int, int, int);
static int (WINAPI *pbind)(SOCKET, const struct sockaddr *, int);
static int (WINAPI *pgetsockname)(SOCKET, struct sockaddr *, int *);
static int (WINAPI *plisten)(SOCKET, int);
static SOCKET (WINAPI *paccept)(SOCKET, struct sockaddr *, int *);
static int (WINAPI *precv)(SOCKET, char *, int, int);
static int (WINAPI *psend)(SOCKET, const char *, int, int);
static int (WINAPI *pclosesocket)(SOCKET);
static HINTERNET (WINAPI *pWinHttpOpen)(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD);
static HINTERNET (WINAPI *pWinHttpConnect)(HINTERNET, LPCWSTR, INTERNET_PORT, DWORD);
static HINTERNET (WINAPI *pWinHttpOpenRequest)(HINTERNET, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR *, DWORD);
static BOOL (WINAPI *pWinHttpSendRequest)(HINTERNET, LPCWSTR, DWORD, LPVOID, DWORD, DWORD, DWORD_PTR);
static BOOL (WINAPI *pWinHttpReceiveResponse)(HINTERNET, LPVOID);
static BOOL (WINAPI *pWinHttpReadData)(HINTERNET, LPVOID, DWORD, LPDWORD);
static BOOL (WINAPI *pWinHttpCloseHandle)(HINTERNET);

static BOOL LoadFunctions(void)
{
    HMODULE Ws = LoadLibraryW(L"ws2_32.dll"), Http = LoadLibraryW(L"winhttp.dll");
    if (!Ws || !Http) return FALSE;
#define GET(m, f) if (!(p##f = (void *)GetProcAddress(m, #f))) return FALSE
    GET(Ws, WSAStartup); GET(Ws, socket); GET(Ws, bind); GET(Ws, getsockname); GET(Ws, listen);
    GET(Ws, accept); GET(Ws, recv); GET(Ws, send); GET(Ws, closesocket);
    GET(Http, WinHttpOpen); GET(Http, WinHttpConnect); GET(Http, WinHttpOpenRequest);
    GET(Http, WinHttpSendRequest); GET(Http, WinHttpReceiveResponse); GET(Http, WinHttpReadData);
    GET(Http, WinHttpCloseHandle);
#undef GET
    return TRUE;
}

#define CHILD_VARIABLE L"WINHTTP_APITEST_POOL_CHILD"
#define IFEO_KEY L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\winhttp_apitest.exe"

static USHORT ServerPort;
static SOCKET Listener;

static DWORD WINAPI Server(PVOID Param)
{
    static const char Reply[] = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: keep-alive\r\n\r\nok";
    char Buffer[1024];
    SOCKET s;
    int n;

    while ((s = paccept(Listener, NULL, NULL)) != INVALID_SOCKET)
    {
        /* One request per connection is enough: answer it, keep it open */
        n = precv(s, Buffer, sizeof(Buffer), 0);
        if (n > 0)
            psend(s, Reply, sizeof(Reply) - 1, 0);
        while (precv(s, Buffer, sizeof(Buffer), 0) > 0);
        pclosesocket(s);
    }
    return 0;
}

static BOOL StartServer(void)
{
    struct sockaddr_in addr = { 0 };
    int len = sizeof(addr);
    WSADATA wsa;

    if (pWSAStartup(MAKEWORD(2, 2), &wsa)) return FALSE;
    Listener = psocket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = 0x0100007f /* 127.0.0.1 */;
    if (pbind(Listener, (struct sockaddr *)&addr, sizeof(addr)) ||
        pgetsockname(Listener, (struct sockaddr *)&addr, &len) || plisten(Listener, 4))
        return FALSE;
    ServerPort = (USHORT)((addr.sin_port >> 8) | (addr.sin_port << 8));
    CloseHandle(CreateThread(NULL, 0, Server, NULL, 0, NULL));
    return TRUE;
}

/* A request whose connection goes to the pool, then the pool's collector */
static int Child(void)
{
    HINTERNET Session, Connect, Request;
    char Data[16];
    DWORD Read = 0;
    BOOL Ok;

    if (!LoadFunctions()) return 4;
    if (!StartServer()) return 2;
    Session = pWinHttpOpen(L"apitest", WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL, 0);
    Connect = pWinHttpConnect(Session, L"127.0.0.1", ServerPort, 0);
    Request = pWinHttpOpenRequest(Connect, L"GET", L"/", NULL, NULL, NULL, 0);
    Ok = Request && pWinHttpSendRequest(Request, NULL, 0, NULL, 0, 0, 0) &&
         pWinHttpReceiveResponse(Request, NULL) &&
         pWinHttpReadData(Request, Data, sizeof(Data), &Read);
    pWinHttpCloseHandle(Request);
    pWinHttpCloseHandle(Connect);
    pWinHttpCloseHandle(Session);
    if (!Ok || Read != 2) return 3;

    /* The pool keeps the connection 30 s, its collector looks every 5 s */
    Sleep(45000);
    return 0;
}

static void SetPageHeap(BOOL Enable)
{
    static const REGSAM Views[] = { KEY_WOW64_64KEY, KEY_WOW64_32KEY };
    DWORD Flag = 0x02000000; /* FLG_HEAP_PAGE_ALLOCS */
    HKEY Key;
    ULONG i;

    for (i = 0; i < _countof(Views); i++)
    {
        if (Enable)
        {
            if (!RegCreateKeyExW(HKEY_LOCAL_MACHINE, IFEO_KEY, 0, NULL, 0, KEY_SET_VALUE | Views[i], NULL, &Key, NULL))
            {
                RegSetValueExW(Key, L"GlobalFlag", 0, REG_DWORD, (BYTE *)&Flag, sizeof(Flag));
                RegCloseKey(Key);
            }
        }
        else
        {
            RegDeleteKeyExW(HKEY_LOCAL_MACHINE, IFEO_KEY, Views[i], 0);
        }
    }
}

START_TEST(ConnectionPool)
{
    WCHAR Path[MAX_PATH], CommandLine[MAX_PATH + 32];
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    DWORD ExitCode = 0xdeadbeef;

    if (GetEnvironmentVariableW(CHILD_VARIABLE, NULL, 0))
        ExitProcess(Child());

    GetModuleFileNameW(NULL, Path, _countof(Path));
    wcscpy(CommandLine, L"\"");
    wcscat(CommandLine, Path);
    wcscat(CommandLine, L"\" ConnectionPool");
    SetEnvironmentVariableW(CHILD_VARIABLE, L"1");
    SetPageHeap(TRUE);
    if (!CreateProcessW(Path, CommandLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    {
        skip("CreateProcess failed: %lu\n", GetLastError());
    }
    else
    {
        ok(WaitForSingleObject(pi.hProcess, 120000) == WAIT_OBJECT_0, "the child hangs\n");
        GetExitCodeProcess(pi.hProcess, &ExitCode);
        ok(ExitCode == 0, "child exit code %lx (0xc0000005: freed memory touched)\n", ExitCode);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    SetPageHeap(FALSE);
    SetEnvironmentVariableW(CHILD_VARIABLE, NULL);
}
