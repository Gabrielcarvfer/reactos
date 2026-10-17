/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     Tests for CreateDispatcherQueueController and Windows.System.DispatcherQueue
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * Paint.NET 5 creates a dispatcher queue on its UI thread with
 * CreateDispatcherQueueController(DQTYPE_THREAD_CURRENT, DQTAT_COM_STA), then
 * finds it again with DispatcherQueue.GetForCurrentThread, queues work with
 * TryEnqueue(Low, ...) and checks HasThreadAccess. Its setup stopped with
 * "DllNotFoundException: coremessaging" before ReactOS had the DLL.
 *
 * The interfaces are called through their vtables (slots from the public
 * Windows.System metadata), so no WinRT headers are needed.
 */

#include <apitest.h>
#include <objbase.h>

typedef void *HSTRING_T;

static const GUID IID_IDispatcherQueue_  = {0x603e88e4, 0xa338, 0x4ffe, {0xa4, 0x57, 0xa5, 0xcf, 0xb9, 0xce, 0xb8, 0x99}};
static const GUID IID_IDispatcherQueue2_ = {0xc822c647, 0x30ef, 0x506e, {0xbd, 0x1e, 0xa6, 0x47, 0xae, 0x66, 0x75, 0xff}};
static const GUID IID_IDispatcherQueueController_ = {0x22f34e66, 0x50db, 0x4e36, {0xa9, 0x8d, 0x61, 0xc0, 0x1b, 0x38, 0x4d, 0x20}};
static const GUID IID_IDispatcherQueueStatics_ = {0xa96d83d7, 0x9371, 0x4517, {0x92, 0x45, 0xd0, 0x82, 0x4a, 0xc1, 0x2c, 0x74}};
static const GUID IID_IDispatcherQueueControllerStatics_ = {0x0a6c98e0, 0x5198, 0x49a2, {0xa3, 0x13, 0x3f, 0x70, 0xd1, 0xf1, 0x3c, 0x27}};
static const GUID IID_IAgileObject_ = {0x94ea2b94, 0xe9cc, 0x49e0, {0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90}};
static const GUID IID_IAsyncInfo_ = {0x00000036, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

/* dispatcherqueue.h */
typedef struct
{
    DWORD dwSize;
    int threadType;      /* DQTYPE_THREAD_DEDICATED = 1, DQTYPE_THREAD_CURRENT = 2 */
    int apartmentType;   /* DQTAT_COM_NONE = 0, DQTAT_COM_ASTA = 1, DQTAT_COM_STA = 2 */
} DQ_OPTIONS;

typedef HRESULT (WINAPI *CREATE_DQC)(DQ_OPTIONS, IUnknown **);
typedef HRESULT (WINAPI *RO_GET_FACTORY)(HSTRING_T, REFIID, void **);
typedef HRESULT (WINAPI *CREATE_STRING)(const WCHAR *, UINT32, HSTRING_T *);
typedef HRESULT (WINAPI *DELETE_STRING)(HSTRING_T);

static CREATE_DQC pCreateDispatcherQueueController;
static RO_GET_FACTORY pRoGetActivationFactory;
static CREATE_STRING pWindowsCreateString;
static DELETE_STRING pWindowsDeleteString;

/* Slot n of an interface's vtable */
#define SLOT(obj, n) (((void **)*(void **)(obj))[n])

/* IInspectable has 6 slots; then: */
#define Controller_get_DispatcherQueue(c, q)  ((HRESULT (WINAPI *)(void *, IUnknown **))SLOT(c, 6))(c, q)
#define Controller_ShutdownQueueAsync(c, a)   ((HRESULT (WINAPI *)(void *, IUnknown **))SLOT(c, 7))(c, a)
#define Queue_TryEnqueue(q, h, r)             ((HRESULT (WINAPI *)(void *, IUnknown *, BYTE *))SLOT(q, 7))(q, h, r)
#define Queue_TryEnqueueWithPriority(q, p, h, r) ((HRESULT (WINAPI *)(void *, int, IUnknown *, BYTE *))SLOT(q, 8))(q, p, h, r)
#define Queue2_get_HasThreadAccess(q, v)      ((HRESULT (WINAPI *)(void *, BYTE *))SLOT(q, 6))(q, v)
#define Statics_GetForCurrentThread(s, q)     ((HRESULT (WINAPI *)(void *, IUnknown **))SLOT(s, 6))(s, q)
#define CtrlStatics_CreateOnDedicatedThread(s, c) ((HRESULT (WINAPI *)(void *, IUnknown **))SLOT(s, 6))(s, c)
#define Action_put_Completed(a, h)            ((HRESULT (WINAPI *)(void *, IUnknown *))SLOT(a, 6))(a, h)
#define Action_GetResults(a)                  ((HRESULT (WINAPI *)(void *))SLOT(a, 8))(a)

/* A DispatcherQueueHandler: IUnknown, then Invoke() */
typedef struct
{
    void **vtbl;
    LONG refs;
    int id;
    DWORD thread;
    HANDLE event;
} Handler;

static int Order[8], OrderCount;

static HRESULT WINAPI Handler_QueryInterface(Handler *This, REFIID riid, void **out)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IAgileObject_))
    {
        *out = This;
        InterlockedIncrement(&This->refs);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI Handler_AddRef(Handler *This) { return InterlockedIncrement(&This->refs); }
static ULONG WINAPI Handler_Release(Handler *This) { return InterlockedDecrement(&This->refs); }
static HRESULT WINAPI Handler_Invoke(Handler *This)
{
    This->thread = GetCurrentThreadId();
    if (OrderCount < ARRAYSIZE(Order)) Order[OrderCount++] = This->id;
    if (This->event) SetEvent(This->event);
    return S_OK;
}
static void *HandlerVtbl[] = { Handler_QueryInterface, Handler_AddRef, Handler_Release, Handler_Invoke };

/* An AsyncActionCompletedHandler: IUnknown, then Invoke(action, status) */
static HRESULT WINAPI Completed_Invoke(Handler *This, IUnknown *action, int status)
{
    This->id = status;
    This->thread = GetCurrentThreadId();
    if (This->event) SetEvent(This->event);
    return S_OK;
}
static void *CompletedVtbl[] = { Handler_QueryInterface, Handler_AddRef, Handler_Release, Completed_Invoke };

static void init_handler(Handler *h, void **vtbl, int id, HANDLE event)
{
    h->vtbl = vtbl;
    h->refs = 1;
    h->id = id;
    h->thread = 0;
    h->event = event;
}

static void pump_messages(void)
{
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

static IUnknown *get_factory(const WCHAR *name, REFIID riid)
{
    HSTRING_T str = NULL;
    IUnknown *factory = NULL;
    HRESULT hr;

    hr = pWindowsCreateString(name, lstrlenW(name), &str);
    ok(hr == S_OK, "WindowsCreateString: 0x%lx\n", hr);
    hr = pRoGetActivationFactory(str, riid, (void **)&factory);
    ok(hr == S_OK, "RoGetActivationFactory(%ls): 0x%lx\n", name, hr);
    pWindowsDeleteString(str);
    return SUCCEEDED(hr) ? factory : NULL;
}

static DWORD WINAPI other_thread(LPVOID param)
{
    IUnknown *queue2 = param;
    BYTE access = 0xcc;
    HRESULT hr = Queue2_get_HasThreadAccess(queue2, &access);
    ok(hr == S_OK, "get_HasThreadAccess: 0x%lx\n", hr);
    ok(access == FALSE, "HasThreadAccess on another thread: %u\n", access);
    return 0;
}

static void test_current_thread(void)
{
    DQ_OPTIONS options = { sizeof(options), 2 /* current */, 2 /* STA */ };
    IUnknown *controller = NULL, *queue = NULL, *queue2 = NULL, *agile = NULL, *statics, *found = NULL;
    Handler low, normal, high;
    BYTE result, access;
    HANDLE thread;
    HRESULT hr;

    options.dwSize = 4;
    hr = pCreateDispatcherQueueController(options, &controller);
    ok(hr == E_INVALIDARG, "bad size: 0x%lx\n", hr);
    options.dwSize = sizeof(options);

    hr = pCreateDispatcherQueueController(options, &controller);
    ok(hr == S_OK, "CreateDispatcherQueueController: 0x%lx\n", hr);
    if (FAILED(hr)) return;

    hr = Controller_get_DispatcherQueue(controller, &queue);
    ok(hr == S_OK && queue, "get_DispatcherQueue: 0x%lx %p\n", hr, queue);
    if (!queue) return;
    hr = queue->lpVtbl->QueryInterface(queue, &IID_IAgileObject_, (void **)&agile);
    ok(hr == S_OK, "IAgileObject: 0x%lx\n", hr);
    if (agile) agile->lpVtbl->Release(agile);

    /* GetForCurrentThread finds the same queue */
    statics = get_factory(L"Windows.System.DispatcherQueue", &IID_IDispatcherQueueStatics_);
    if (statics)
    {
        hr = Statics_GetForCurrentThread(statics, &found);
        ok(hr == S_OK, "GetForCurrentThread: 0x%lx\n", hr);
        ok(found == queue, "GetForCurrentThread: %p, expected %p\n", found, queue);
        if (found) found->lpVtbl->Release(found);
        statics->lpVtbl->Release(statics);
    }

    hr = queue->lpVtbl->QueryInterface(queue, &IID_IDispatcherQueue2_, (void **)&queue2);
    ok(hr == S_OK, "IDispatcherQueue2: 0x%lx\n", hr);
    if (queue2)
    {
        access = 0xcc;
        hr = Queue2_get_HasThreadAccess(queue2, &access);
        ok(hr == S_OK && access == TRUE, "HasThreadAccess: 0x%lx %u\n", hr, access);
        thread = CreateThread(NULL, 0, other_thread, queue2, 0, NULL);
        WaitForSingleObject(thread, 5000);
        CloseHandle(thread);
    }

    /* Work runs from the message loop, the highest priority first */
    init_handler(&low, HandlerVtbl, 1, NULL);
    init_handler(&normal, HandlerVtbl, 2, NULL);
    init_handler(&high, HandlerVtbl, 3, NULL);
    OrderCount = 0;
    result = 0;
    hr = Queue_TryEnqueueWithPriority(queue, -10, (IUnknown *)&low, &result);
    ok(hr == S_OK && result == TRUE, "TryEnqueue(Low): 0x%lx %u\n", hr, result);
    hr = Queue_TryEnqueue(queue, (IUnknown *)&normal, &result);
    ok(hr == S_OK && result == TRUE, "TryEnqueue: 0x%lx %u\n", hr, result);
    hr = Queue_TryEnqueueWithPriority(queue, 10, (IUnknown *)&high, &result);
    ok(hr == S_OK && result == TRUE, "TryEnqueue(High): 0x%lx %u\n", hr, result);
    ok(OrderCount == 0, "ran before the message loop: %d\n", OrderCount);
    ok(low.refs == 2, "queued handler refs: %ld\n", low.refs);
    pump_messages();
    ok(OrderCount == 3, "ran %d work items\n", OrderCount);
    ok(Order[0] == 3 && Order[1] == 2 && Order[2] == 1, "order %d %d %d\n", Order[0], Order[1], Order[2]);
    ok(high.thread == GetCurrentThreadId(), "ran on thread %lx\n", high.thread);
    ok(low.refs == 1, "handler refs after running: %ld\n", low.refs);

    if (queue2) queue2->lpVtbl->Release(queue2);
    queue->lpVtbl->Release(queue);
    /* (the controller stays: like Paint.NET, keep the queue for the thread) */
}

static void test_dedicated_thread(void)
{
    DQ_OPTIONS options = { sizeof(options), 1 /* dedicated */, 0 /* none */ };
    IUnknown *controller = NULL, *queue = NULL, *action = NULL, *statics;
    HANDLE event = CreateEventW(NULL, FALSE, FALSE, NULL);
    Handler work, completed;
    BYTE result;
    HRESULT hr;
    DWORD wait;

    hr = pCreateDispatcherQueueController(options, &controller);
    ok(hr == S_OK, "CreateDispatcherQueueController(dedicated): 0x%lx\n", hr);
    if (FAILED(hr)) return;
    hr = Controller_get_DispatcherQueue(controller, &queue);
    ok(hr == S_OK && queue, "get_DispatcherQueue: 0x%lx\n", hr);
    if (!queue) return;

    init_handler(&work, HandlerVtbl, 7, event);
    result = 0;
    hr = Queue_TryEnqueue(queue, (IUnknown *)&work, &result);
    ok(hr == S_OK && result == TRUE, "TryEnqueue: 0x%lx %u\n", hr, result);
    wait = WaitForSingleObject(event, 5000);
    ok(wait == WAIT_OBJECT_0, "work item did not run: %lu\n", wait);
    ok(work.thread && work.thread != GetCurrentThreadId(), "ran on thread %lx (this one %lx)\n",
       work.thread, GetCurrentThreadId());

    /* ShutdownQueueAsync completes, then nothing more is accepted */
    hr = Controller_ShutdownQueueAsync(controller, &action);
    ok(hr == S_OK && action, "ShutdownQueueAsync: 0x%lx\n", hr);
    if (action)
    {
        IUnknown *info = NULL;
        hr = action->lpVtbl->QueryInterface(action, &IID_IAsyncInfo_, (void **)&info);
        ok(hr == S_OK, "IAsyncInfo: 0x%lx\n", hr);
        if (info) info->lpVtbl->Release(info);

        init_handler(&completed, CompletedVtbl, -1, event);
        hr = Action_put_Completed(action, (IUnknown *)&completed);
        ok(hr == S_OK, "put_Completed: 0x%lx\n", hr);
        wait = WaitForSingleObject(event, 5000);
        ok(wait == WAIT_OBJECT_0, "shutdown did not complete: %lu\n", wait);
        ok(completed.id == 1 /* Completed */, "status %d\n", completed.id);
        ok(Action_GetResults(action) == S_OK, "GetResults failed\n");
        action->lpVtbl->Release(action);
    }
    init_handler(&work, HandlerVtbl, 8, NULL);
    result = 0xcc;
    hr = Queue_TryEnqueue(queue, (IUnknown *)&work, &result);
    ok(hr == S_OK && result == FALSE, "TryEnqueue after shutdown: 0x%lx %u\n", hr, result);
    ok(work.refs == 1, "refused handler kept: %ld\n", work.refs);

    queue->lpVtbl->Release(queue);
    controller->lpVtbl->Release(controller);

    /* The same through Windows.System.DispatcherQueueController */
    statics = get_factory(L"Windows.System.DispatcherQueueController", &IID_IDispatcherQueueControllerStatics_);
    if (statics)
    {
        controller = NULL;
        hr = CtrlStatics_CreateOnDedicatedThread(statics, &controller);
        ok(hr == S_OK && controller, "CreateOnDedicatedThread: 0x%lx\n", hr);
        if (controller)
        {
            IUnknown *same = NULL;
            hr = controller->lpVtbl->QueryInterface(controller, &IID_IDispatcherQueueController_, (void **)&same);
            ok(hr == S_OK, "IDispatcherQueueController: 0x%lx\n", hr);
            if (same) same->lpVtbl->Release(same);
            queue = NULL;
            Controller_get_DispatcherQueue(controller, &queue);
            if (queue)
            {
                init_handler(&work, HandlerVtbl, 9, event);
                hr = Queue_TryEnqueue(queue, (IUnknown *)&work, &result);
                ok(hr == S_OK && result == TRUE, "TryEnqueue: 0x%lx %u\n", hr, result);
                ok(WaitForSingleObject(event, 5000) == WAIT_OBJECT_0, "work item did not run\n");
                queue->lpVtbl->Release(queue);
            }
            controller->lpVtbl->Release(controller);
        }
        statics->lpVtbl->Release(statics);
    }
    CloseHandle(event);
}

START_TEST(DispatcherQueue)
{
    HMODULE coremessaging = LoadLibraryW(L"coremessaging.dll");
    HMODULE combase = LoadLibraryW(L"combase.dll");

    ok(coremessaging != NULL, "coremessaging.dll not found: %lu\n", GetLastError());
    if (!coremessaging || !combase) return;
    pCreateDispatcherQueueController = (CREATE_DQC)GetProcAddress(coremessaging, "CreateDispatcherQueueController");
    pRoGetActivationFactory = (RO_GET_FACTORY)GetProcAddress(combase, "RoGetActivationFactory");
    pWindowsCreateString = (CREATE_STRING)GetProcAddress(combase, "WindowsCreateString");
    pWindowsDeleteString = (DELETE_STRING)GetProcAddress(combase, "WindowsDeleteString");
    ok(pCreateDispatcherQueueController != NULL, "no CreateDispatcherQueueController\n");
    if (!pCreateDispatcherQueueController || !pRoGetActivationFactory) return;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    test_current_thread();
    test_dedicated_thread();
    CoUninitialize();
}
