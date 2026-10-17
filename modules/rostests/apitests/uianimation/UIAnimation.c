/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Tests for the Windows Animation Manager (uianimation.dll)
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 *
 * Paint.NET 5 creates the animation manager, timer, transition factory and
 * transition library at startup, schedules storyboards and calls Update.
 */

#define COBJMACROS
#include <apitest.h>
#include <objbase.h>
#include <initguid.h>
#include <uianimation.h>
#include <math.h>

#define EPSILON 1e-6
#define IsNear(a, b) (fabs((a) - (b)) < EPSILON)

/* A manager event handler that records the status changes */
typedef struct
{
    IUIAnimationManagerEventHandler IUIAnimationManagerEventHandler_iface;
    LONG Ref;
    int Changes;
    UI_ANIMATION_MANAGER_STATUS Last;
} STATUS_HANDLER;

static HRESULT WINAPI StatusHandler_QueryInterface(IUIAnimationManagerEventHandler *iface, REFIID riid, void **out)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IUIAnimationManagerEventHandler))
    {
        *out = iface;
        IUIAnimationManagerEventHandler_AddRef(iface);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI StatusHandler_AddRef(IUIAnimationManagerEventHandler *iface)
{
    return InterlockedIncrement(&((STATUS_HANDLER *)iface)->Ref);
}

static ULONG WINAPI StatusHandler_Release(IUIAnimationManagerEventHandler *iface)
{
    return InterlockedDecrement(&((STATUS_HANDLER *)iface)->Ref);
}

static HRESULT WINAPI StatusHandler_OnManagerStatusChanged(IUIAnimationManagerEventHandler *iface,
                                                           UI_ANIMATION_MANAGER_STATUS Status,
                                                           UI_ANIMATION_MANAGER_STATUS Previous)
{
    STATUS_HANDLER *This = (STATUS_HANDLER *)iface;
    This->Changes++;
    This->Last = Status;
    return S_OK;
}

static const struct
{
    HRESULT (WINAPI *QueryInterface)(IUIAnimationManagerEventHandler *, REFIID, void **);
    ULONG (WINAPI *AddRef)(IUIAnimationManagerEventHandler *);
    ULONG (WINAPI *Release)(IUIAnimationManagerEventHandler *);
    HRESULT (WINAPI *OnManagerStatusChanged)(IUIAnimationManagerEventHandler *, UI_ANIMATION_MANAGER_STATUS,
                                             UI_ANIMATION_MANAGER_STATUS);
} StatusHandlerVtbl =
{
    StatusHandler_QueryInterface, StatusHandler_AddRef, StatusHandler_Release, StatusHandler_OnManagerStatusChanged
};

/* A variable change handler that counts the calls */
typedef struct
{
    IUIAnimationVariableChangeHandler IUIAnimationVariableChangeHandler_iface;
    LONG Ref;
    int Calls;
    double LastValue;
} CHANGE_HANDLER;

static HRESULT WINAPI ChangeHandler_QueryInterface(IUIAnimationVariableChangeHandler *iface, REFIID riid, void **out)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IUIAnimationVariableChangeHandler))
    {
        *out = iface;
        IUIAnimationVariableChangeHandler_AddRef(iface);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI ChangeHandler_AddRef(IUIAnimationVariableChangeHandler *iface)
{
    return InterlockedIncrement(&((CHANGE_HANDLER *)iface)->Ref);
}

static ULONG WINAPI ChangeHandler_Release(IUIAnimationVariableChangeHandler *iface)
{
    return InterlockedDecrement(&((CHANGE_HANDLER *)iface)->Ref);
}

static HRESULT WINAPI ChangeHandler_OnValueChanged(IUIAnimationVariableChangeHandler *iface,
                                                   IUIAnimationStoryboard *Storyboard, IUIAnimationVariable *Variable,
                                                   DOUBLE Value, DOUBLE Previous)
{
    CHANGE_HANDLER *This = (CHANGE_HANDLER *)iface;
    This->Calls++;
    This->LastValue = Value;
    return S_OK;
}

static const struct
{
    HRESULT (WINAPI *QueryInterface)(IUIAnimationVariableChangeHandler *, REFIID, void **);
    ULONG (WINAPI *AddRef)(IUIAnimationVariableChangeHandler *);
    ULONG (WINAPI *Release)(IUIAnimationVariableChangeHandler *);
    HRESULT (WINAPI *OnValueChanged)(IUIAnimationVariableChangeHandler *, IUIAnimationStoryboard *,
                                     IUIAnimationVariable *, DOUBLE, DOUBLE);
} ChangeHandlerVtbl =
{
    ChangeHandler_QueryInterface, ChangeHandler_AddRef, ChangeHandler_Release, ChangeHandler_OnValueChanged
};

static double GetValue(IUIAnimationVariable *Variable)
{
    DOUBLE Value = -12345.0;
    IUIAnimationVariable_GetValue(Variable, &Value);
    return Value;
}

static void TestTransitions(IUIAnimationManager *Manager, IUIAnimationTransitionLibrary *Library)
{
    STATUS_HANDLER Status = { { (void *)&StatusHandlerVtbl }, 1 };
    CHANGE_HANDLER Change = { { (void *)&ChangeHandlerVtbl }, 1 };
    IUIAnimationVariable *A, *B;
    IUIAnimationTransition *Linear, *Smooth, *AccelDecel, *Instant, *Constant, *Linear2;
    IUIAnimationStoryboard *Storyboard;
    UI_ANIMATION_STORYBOARD_STATUS SbStatus;
    UI_ANIMATION_SCHEDULING_RESULT Scheduling;
    UI_ANIMATION_UPDATE_RESULT Update;
    UI_ANIMATION_MANAGER_STATUS MgrStatus;
    UI_ANIMATION_KEYFRAME Key;
    UI_ANIMATION_SECONDS Duration;
    DOUBLE Final;
    HRESULT hr;

    hr = IUIAnimationManager_SetManagerEventHandler(Manager, &Status.IUIAnimationManagerEventHandler_iface);
    ok(hr == S_OK, "SetManagerEventHandler returned %#lx\n", hr);

    hr = IUIAnimationManager_CreateAnimationVariable(Manager, 0.0, &A);
    ok(hr == S_OK, "CreateAnimationVariable returned %#lx\n", hr);
    hr = IUIAnimationManager_CreateAnimationVariable(Manager, 100.0, &B);
    ok(hr == S_OK, "CreateAnimationVariable returned %#lx\n", hr);
    if (FAILED(hr))
        return;
    ok(IsNear(GetValue(A), 0.0), "A is %f\n", GetValue(A));
    hr = IUIAnimationVariable_SetVariableChangeHandler(A, &Change.IUIAnimationVariableChangeHandler_iface);
    ok(hr == S_OK, "SetVariableChangeHandler returned %#lx\n", hr);

    /* A: linear 0 -> 10 in 1 s, then accelerate-decelerate 10 -> 20 in 1 s.
     * B: from the end of A's first transition, smooth stop 100 -> 0 in 1 s */
    IUIAnimationTransitionLibrary_CreateLinearTransition(Library, 1.0, 10.0, &Linear);
    IUIAnimationTransitionLibrary_CreateAccelerateDecelerateTransition(Library, 1.0, 20.0, 0.5, 0.5, &AccelDecel);
    IUIAnimationTransitionLibrary_CreateSmoothStopTransition(Library, 1.0, 0.0, &Smooth);
    hr = IUIAnimationTransition_GetDuration(Linear, &Duration);
    ok(hr == S_OK && IsNear(Duration, 1.0), "GetDuration returned %#lx, %f\n", hr, Duration);
    ok(IUIAnimationTransition_IsDurationKnown(Smooth) == S_FALSE, "the smooth stop duration is known\n");

    IUIAnimationManager_CreateStoryboard(Manager, &Storyboard);
    hr = IUIAnimationStoryboard_AddTransition(Storyboard, A, Linear);
    ok(hr == S_OK, "AddTransition returned %#lx\n", hr);
    hr = IUIAnimationStoryboard_AddTransition(Storyboard, A, AccelDecel);
    ok(hr == S_OK, "AddTransition returned %#lx\n", hr);
    hr = IUIAnimationStoryboard_AddKeyframeAfterTransition(Storyboard, Linear, &Key);
    ok(hr == S_OK, "AddKeyframeAfterTransition returned %#lx\n", hr);
    hr = IUIAnimationStoryboard_AddTransitionAtKeyframe(Storyboard, B, Smooth, Key);
    ok(hr == S_OK, "AddTransitionAtKeyframe returned %#lx\n", hr);
    hr = IUIAnimationStoryboard_GetStatus(Storyboard, &SbStatus);
    ok(hr == S_OK && SbStatus == UI_ANIMATION_STORYBOARD_BUILDING, "status %d\n", SbStatus);

    hr = IUIAnimationStoryboard_Schedule(Storyboard, 100.0, &Scheduling);
    ok(hr == S_OK && Scheduling == UI_ANIMATION_SCHEDULING_SUCCEEDED, "Schedule returned %#lx, %d\n", hr, Scheduling);
    IUIAnimationManager_GetStatus(Manager, &MgrStatus);
    ok(MgrStatus == UI_ANIMATION_MANAGER_BUSY, "manager status %d\n", MgrStatus);
    ok(Status.Changes == 1 && Status.Last == UI_ANIMATION_MANAGER_BUSY, "%d changes, last %d\n", Status.Changes, Status.Last);
    hr = IUIAnimationVariable_GetFinalValue(A, &Final);
    ok(hr == S_OK && IsNear(Final, 20.0), "final value %f\n", Final);

    hr = IUIAnimationManager_Update(Manager, 100.5, &Update);
    ok(hr == S_OK && Update == UI_ANIMATION_UPDATE_VARIABLES_CHANGED, "Update returned %#lx, %d\n", hr, Update);
    ok(IsNear(GetValue(A), 5.0), "A is %f at 0.5 s\n", GetValue(A));
    ok(IsNear(GetValue(B), 100.0), "B is %f at 0.5 s\n", GetValue(B));
    ok(Change.Calls == 1 && IsNear(Change.LastValue, 5.0), "%d calls, last %f\n", Change.Calls, Change.LastValue);
    IUIAnimationStoryboard_GetStatus(Storyboard, &SbStatus);
    ok(SbStatus == UI_ANIMATION_STORYBOARD_PLAYING, "status %d\n", SbStatus);

    IUIAnimationManager_Update(Manager, 101.5, &Update);
    ok(IsNear(GetValue(A), 15.0), "A is %f at 1.5 s\n", GetValue(A));
    ok(GetValue(B) > 0.0 && GetValue(B) < 50.0, "B is %f at 1.5 s\n", GetValue(B));

    IUIAnimationManager_Update(Manager, 102.5, &Update);
    ok(IsNear(GetValue(A), 20.0), "A is %f at the end\n", GetValue(A));
    ok(IsNear(GetValue(B), 0.0), "B is %f at the end\n", GetValue(B));
    IUIAnimationStoryboard_GetStatus(Storyboard, &SbStatus);
    ok(SbStatus == UI_ANIMATION_STORYBOARD_FINISHED, "status %d\n", SbStatus);
    IUIAnimationManager_GetStatus(Manager, &MgrStatus);
    ok(MgrStatus == UI_ANIMATION_MANAGER_IDLE, "manager status %d\n", MgrStatus);
    ok(Status.Changes == 2 && Status.Last == UI_ANIMATION_MANAGER_IDLE, "%d changes, last %d\n", Status.Changes, Status.Last);
    IUIAnimationStoryboard_Release(Storyboard);

    /* ScheduleTransition, instantaneous and constant */
    IUIAnimationTransitionLibrary_CreateInstantaneousTransition(Library, 42.0, &Instant);
    hr = IUIAnimationManager_ScheduleTransition(Manager, A, Instant, 200.0);
    ok(hr == S_OK, "ScheduleTransition returned %#lx\n", hr);
    IUIAnimationManager_Update(Manager, 200.0, NULL);
    ok(IsNear(GetValue(A), 42.0), "A is %f\n", GetValue(A));

    IUIAnimationTransitionLibrary_CreateConstantTransition(Library, 1.0, &Constant);
    IUIAnimationManager_ScheduleTransition(Manager, A, Constant, 300.0);
    IUIAnimationManager_Update(Manager, 300.5, NULL);
    ok(IsNear(GetValue(A), 42.0), "A is %f\n", GetValue(A));
    IUIAnimationManager_Update(Manager, 301.5, NULL);
    IUIAnimationManager_GetStatus(Manager, &MgrStatus);
    ok(MgrStatus == UI_ANIMATION_MANAGER_IDLE, "manager status %d\n", MgrStatus);

    /* Repeated forever until concluded */
    IUIAnimationTransitionLibrary_CreateLinearTransition(Library, 1.0, 52.0, &Linear2);
    IUIAnimationManager_CreateStoryboard(Manager, &Storyboard);
    IUIAnimationStoryboard_AddTransition(Storyboard, A, Linear2);
    IUIAnimationStoryboard_AddKeyframeAfterTransition(Storyboard, Linear2, &Key);
    hr = IUIAnimationStoryboard_RepeatBetweenKeyframes(Storyboard, UI_ANIMATION_KEYFRAME_STORYBOARD_START, Key, -1);
    ok(hr == S_OK, "RepeatBetweenKeyframes returned %#lx\n", hr);
    IUIAnimationStoryboard_Schedule(Storyboard, 400.0, NULL);
    IUIAnimationManager_Update(Manager, 403.5, NULL);
    ok(IsNear(GetValue(A), 47.0), "A is %f in the 4th repetition\n", GetValue(A));
    hr = IUIAnimationStoryboard_Conclude(Storyboard);
    ok(hr == S_OK, "Conclude returned %#lx\n", hr);
    IUIAnimationManager_Update(Manager, 404.5, NULL);
    ok(IsNear(GetValue(A), 52.0), "A is %f after Conclude\n", GetValue(A));
    IUIAnimationStoryboard_GetStatus(Storyboard, &SbStatus);
    ok(SbStatus == UI_ANIMATION_STORYBOARD_FINISHED, "status %d\n", SbStatus);
    IUIAnimationStoryboard_Release(Storyboard);

    hr = IUIAnimationManager_Shutdown(Manager);
    ok(hr == S_OK, "Shutdown returned %#lx\n", hr);
    ok(Status.Ref == 1, "the manager event handler has %ld references left\n", Status.Ref);

    IUIAnimationTransition_Release(Linear);
    IUIAnimationTransition_Release(Linear2);
    IUIAnimationTransition_Release(Smooth);
    IUIAnimationTransition_Release(AccelDecel);
    IUIAnimationTransition_Release(Instant);
    IUIAnimationTransition_Release(Constant);
    IUIAnimationVariable_Release(A);
    IUIAnimationVariable_Release(B);
    ok(Change.Ref == 1, "the change handler has %ld references left\n", Change.Ref);
}

static void TestTimer(IUIAnimationTimer *Timer)
{
    UI_ANIMATION_SECONDS T1 = 0, T2 = 0;
    HRESULT hr;

    hr = IUIAnimationTimer_GetTime(Timer, &T1);
    ok(hr == S_OK, "GetTime returned %#lx\n", hr);
    Sleep(20);
    IUIAnimationTimer_GetTime(Timer, &T2);
    ok(T2 > T1, "time did not advance: %f %f\n", T1, T2);

    ok(IUIAnimationTimer_IsEnabled(Timer) == S_FALSE, "the timer is enabled\n");
    hr = IUIAnimationTimer_Enable(Timer);
    ok(hr == S_OK, "Enable returned %#lx\n", hr);
    ok(IUIAnimationTimer_IsEnabled(Timer) == S_OK, "the timer is not enabled\n");
    hr = IUIAnimationTimer_Disable(Timer);
    ok(hr == S_OK, "Disable returned %#lx\n", hr);
    ok(IUIAnimationTimer_IsEnabled(Timer) == S_FALSE, "the timer is enabled\n");
}

START_TEST(UIAnimation)
{
    IUIAnimationTransitionFactory *Factory = NULL;
    IUIAnimationTransitionLibrary *Library = NULL;
    IUIAnimationManager *Manager = NULL;
    IUIAnimationTimer *Timer = NULL;
    HRESULT hr;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    hr = CoCreateInstance(&CLSID_UIAnimationManager, NULL, CLSCTX_INPROC_SERVER, &IID_IUIAnimationManager, (void **)&Manager);
    ok(hr == S_OK, "UIAnimationManager: %#lx\n", hr);
    hr = CoCreateInstance(&CLSID_UIAnimationTimer, NULL, CLSCTX_INPROC_SERVER, &IID_IUIAnimationTimer, (void **)&Timer);
    ok(hr == S_OK, "UIAnimationTimer: %#lx\n", hr);
    hr = CoCreateInstance(&CLSID_UIAnimationTransitionFactory, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IUIAnimationTransitionFactory, (void **)&Factory);
    ok(hr == S_OK, "UIAnimationTransitionFactory: %#lx\n", hr);
    hr = CoCreateInstance(&CLSID_UIAnimationTransitionLibrary, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IUIAnimationTransitionLibrary, (void **)&Library);
    ok(hr == S_OK, "UIAnimationTransitionLibrary: %#lx\n", hr);

    if (Manager && Library)
        TestTransitions(Manager, Library);
    if (Timer)
        TestTimer(Timer);

    if (Manager) IUIAnimationManager_Release(Manager);
    if (Timer) IUIAnimationTimer_Release(Timer);
    if (Factory) IUIAnimationTransitionFactory_Release(Factory);
    if (Library) IUIAnimationTransitionLibrary_Release(Library);
    CoUninitialize();
}
