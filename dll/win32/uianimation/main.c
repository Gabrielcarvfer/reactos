/*
 * PROJECT:     ReactOS Windows Animation Manager
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     UIAnimationManager, UIAnimationTimer, UIAnimationTransitionFactory
 *              and UIAnimationTransitionLibrary (version 1 interfaces)
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 *
 * Animation variables hold a value. Storyboards place transitions of
 * variables on a timeline (with keyframes and one repeated section).
 * Scheduling a storyboard starts it at the given time; the manager's Update
 * (called by the program, or by the timer's update handler) moves every
 * scheduled storyboard to the given time and calls the change handlers.
 *
 * The linear, accelerate-decelerate, smooth-stop, cubic, discrete,
 * constant and instantaneous transitions and custom interpolators are
 * interpolated. Sinusoidal and reversal transitions hold the value for
 * their duration; parabolic ones jump to their final value.
 *
 * Paint.NET 5 needs these classes (its AnimationService).
 */

#define COBJMACROS
#include <stdarg.h>
#include <math.h>

#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "winreg.h"
#include "objbase.h"
#include "olectl.h"

#include "initguid.h"
#include "uianimation.h"

#include "wine/debug.h"
#include "wine/list.h"

WINE_DEFAULT_DEBUG_CHANNEL(uianimation);

static HINSTANCE uianimation_instance;
static LONG object_count;

/* All objects of the DLL share one lock: handlers run with it held, and may
 * call back into the animation objects on the same thread. */
static CRITICAL_SECTION uia_cs;
static CRITICAL_SECTION_DEBUG uia_cs_debug =
{
    0, 0, &uia_cs,
    { &uia_cs_debug.ProcessLocksList, &uia_cs_debug.ProcessLocksList },
      0, 0, { (DWORD_PTR)(__FILE__ ": uia_cs") }
};
static CRITICAL_SECTION uia_cs = { &uia_cs_debug, -1, 0, 0, 0, 0 };

#ifndef UI_ANIMATION_REPEAT_INDEFINITELY
#define UI_ANIMATION_REPEAT_INDEFINITELY (-1)
#endif

struct manager;
struct storyboard;

/***********************************************************************
 *          IUIAnimationTransition
 */
enum transition_kind
{
    TRANSITION_INSTANTANEOUS,
    TRANSITION_CONSTANT,
    TRANSITION_DISCRETE,
    TRANSITION_LINEAR,
    TRANSITION_LINEAR_FROM_SPEED,
    TRANSITION_SINUSOIDAL,      /* held for the duration */
    TRANSITION_ACCEL_DECEL,
    TRANSITION_REVERSAL,        /* held for the duration */
    TRANSITION_CUBIC,
    TRANSITION_SMOOTH_STOP,
    TRANSITION_PARABOLIC,       /* jumps to the final value */
    TRANSITION_INTERPOLATOR,
};

struct transition
{
    IUIAnimationTransition IUIAnimationTransition_iface;
    LONG ref;
    enum transition_kind kind;
    double duration;    /* the given duration, delay or maximum duration */
    double final;       /* final value */
    double p1, p2;      /* hold / speed / acceleration ratio, deceleration ratio / final velocity */
    BOOL has_final;     /* FALSE: the final value is the initial value */
    double initial_value, initial_velocity;
    BOOL initial_value_set;
    IUIAnimationInterpolator *interpolator;
};

static inline struct transition *impl_from_IUIAnimationTransition(IUIAnimationTransition *iface)
{
    return CONTAINING_RECORD(iface, struct transition, IUIAnimationTransition_iface);
}

static struct transition *unsafe_transition(IUIAnimationTransition *iface);

/* Length of the transition when it starts at value v0 */
static double transition_duration(struct transition *t, double v0)
{
    UI_ANIMATION_SECONDS duration = 0.0;

    switch (t->kind)
    {
    case TRANSITION_INSTANTANEOUS:
    case TRANSITION_PARABOLIC:
        return 0.0;
    case TRANSITION_DISCRETE:
        return t->duration + t->p1;
    case TRANSITION_LINEAR_FROM_SPEED:
        return t->p1 > 0.0 ? fabs(t->final - v0) / t->p1 : 0.0;
    case TRANSITION_SMOOTH_STOP:
        return v0 == t->final ? 0.0 : t->duration;
    case TRANSITION_INTERPOLATOR:
        IUIAnimationInterpolator_SetInitialValueAndVelocity(t->interpolator, v0, 0.0);
        if (FAILED(IUIAnimationInterpolator_GetDuration(t->interpolator, &duration)) || duration < 0.0)
            duration = 0.0;
        return duration;
    default:
        return t->duration > 0.0 ? t->duration : 0.0;
    }
}

static double transition_final(struct transition *t, double v0)
{
    double value;

    if (t->kind == TRANSITION_INTERPOLATOR)
    {
        IUIAnimationInterpolator_SetInitialValueAndVelocity(t->interpolator, v0, 0.0);
        if (FAILED(IUIAnimationInterpolator_GetFinalValue(t->interpolator, &value)))
            value = v0;
        return value;
    }
    return t->has_final ? t->final : v0;
}

/* Value at "offset" seconds into the transition (0 <= offset < duration) */
static double transition_value(struct transition *t, double v0, double offset, double duration)
{
    double x, v1 = transition_final(t, v0), p, a, d, vmax;

    if (duration <= 0.0)
        return v1;
    x = offset / duration;
    if (x < 0.0) x = 0.0;
    if (x > 1.0) x = 1.0;

    switch (t->kind)
    {
    case TRANSITION_DISCRETE:
        return offset < t->duration ? v0 : v1;
    case TRANSITION_LINEAR:
    case TRANSITION_LINEAR_FROM_SPEED:
        return v0 + (v1 - v0) * x;
    case TRANSITION_ACCEL_DECEL:
        /* Constant acceleration, cruise, constant deceleration */
        a = t->p1; d = t->p2;
        if (a < 0.0 || d < 0.0 || a + d > 1.0) a = d = 0.0;
        vmax = 2.0 / (2.0 - a - d);
        if (x < a)
            p = vmax * x * x / (2.0 * a);
        else if (x <= 1.0 - d)
            p = vmax * a / 2.0 + vmax * (x - a);
        else
            p = 1.0 - vmax * (1.0 - x) * (1.0 - x) / (2.0 * d);
        return v0 + (v1 - v0) * p;
    case TRANSITION_SMOOTH_STOP:
        p = 1.0 - (1.0 - x) * (1.0 - x) * (1.0 - x);
        return v0 + (v1 - v0) * p;
    case TRANSITION_CUBIC:
        /* Hermite curve from v0 (velocity 0) to v1 with the given final velocity */
        return (2 * x * x * x - 3 * x * x + 1) * v0 + (-2 * x * x * x + 3 * x * x) * v1 +
               (x * x * x - x * x) * duration * t->p2;
    case TRANSITION_INTERPOLATOR:
        IUIAnimationInterpolator_SetInitialValueAndVelocity(t->interpolator, v0, 0.0);
        if (FAILED(IUIAnimationInterpolator_InterpolateValue(t->interpolator, offset, &p)))
            p = v1;
        return p;
    default:
        return v0;
    }
}

static HRESULT WINAPI transition_QueryInterface(IUIAnimationTransition *iface, REFIID iid, void **obj)
{
    TRACE("(%p)->(%s %p)\n", iface, debugstr_guid(iid), obj);

    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IUIAnimationTransition))
    {
        IUIAnimationTransition_AddRef(iface);
        *obj = iface;
        return S_OK;
    }
    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI transition_AddRef(IUIAnimationTransition *iface)
{
    struct transition *This = impl_from_IUIAnimationTransition(iface);
    return InterlockedIncrement(&This->ref);
}

static ULONG WINAPI transition_Release(IUIAnimationTransition *iface)
{
    struct transition *This = impl_from_IUIAnimationTransition(iface);
    ULONG ref = InterlockedDecrement(&This->ref);

    if (!ref)
    {
        if (This->interpolator)
            IUIAnimationInterpolator_Release(This->interpolator);
        free(This);
        InterlockedDecrement(&object_count);
    }
    return ref;
}

static HRESULT WINAPI transition_SetInitialValue(IUIAnimationTransition *iface, DOUBLE value)
{
    struct transition *This = impl_from_IUIAnimationTransition(iface);

    This->initial_value = value;
    This->initial_value_set = TRUE;
    return S_OK;
}

static HRESULT WINAPI transition_SetInitialVelocity(IUIAnimationTransition *iface, DOUBLE velocity)
{
    struct transition *This = impl_from_IUIAnimationTransition(iface);

    This->initial_velocity = velocity;
    return S_OK;
}

static BOOL transition_duration_depends_on_value(struct transition *t)
{
    return t->kind == TRANSITION_LINEAR_FROM_SPEED || t->kind == TRANSITION_SMOOTH_STOP ||
           t->kind == TRANSITION_INTERPOLATOR;
}

static HRESULT WINAPI transition_IsDurationKnown(IUIAnimationTransition *iface)
{
    struct transition *This = impl_from_IUIAnimationTransition(iface);

    return (!transition_duration_depends_on_value(This) || This->initial_value_set) ? S_OK : S_FALSE;
}

static HRESULT WINAPI transition_GetDuration(IUIAnimationTransition *iface, UI_ANIMATION_SECONDS *duration)
{
    struct transition *This = impl_from_IUIAnimationTransition(iface);

    if (!duration)
        return E_POINTER;
    if (transition_duration_depends_on_value(This) && !This->initial_value_set)
        return UI_E_VALUE_NOT_DETERMINED;
    *duration = transition_duration(This, This->initial_value);
    return S_OK;
}

static const IUIAnimationTransitionVtbl transition_vtbl =
{
    transition_QueryInterface,
    transition_AddRef,
    transition_Release,
    transition_SetInitialValue,
    transition_SetInitialVelocity,
    transition_IsDurationKnown,
    transition_GetDuration,
};

static struct transition *unsafe_transition(IUIAnimationTransition *iface)
{
    if (!iface || iface->lpVtbl != &transition_vtbl)
        return NULL;
    return impl_from_IUIAnimationTransition(iface);
}

static HRESULT transition_create(enum transition_kind kind, double duration, double final, BOOL has_final,
                                 double p1, double p2, IUIAnimationTransition **out)
{
    struct transition *This;

    if (!out)
        return E_POINTER;
    if (!(This = calloc(1, sizeof(*This))))
        return E_OUTOFMEMORY;
    This->IUIAnimationTransition_iface.lpVtbl = &transition_vtbl;
    This->ref = 1;
    This->kind = kind;
    This->duration = duration;
    This->final = final;
    This->has_final = has_final;
    This->p1 = p1;
    This->p2 = p2;
    InterlockedIncrement(&object_count);
    *out = &This->IUIAnimationTransition_iface;
    return S_OK;
}

/***********************************************************************
 *          IUIAnimationVariable
 */
struct variable
{
    IUIAnimationVariable IUIAnimationVariable_iface;
    LONG ref;
    struct list entry;              /* in manager->variables */
    struct manager *manager;        /* weak; NULL once the manager is gone */
    double value, previous;
    double lower, upper;
    UI_ANIMATION_ROUNDING_MODE rounding;
    IUnknown *tag_object;
    UINT32 tag_id;
    IUIAnimationVariableChangeHandler *change_handler;
    IUIAnimationVariableIntegerChangeHandler *integer_handler;
    struct storyboard *storyboard;  /* the storyboard animating it, weak */
    /* Used by an update */
    double new_value, new_start;
    BOOL touched;
};

static inline struct variable *impl_from_IUIAnimationVariable(IUIAnimationVariable *iface)
{
    return CONTAINING_RECORD(iface, struct variable, IUIAnimationVariable_iface);
}

static INT32 round_value(struct variable *var, double value)
{
    switch (var->rounding)
    {
    case UI_ANIMATION_ROUNDING_FLOOR: return (INT32)floor(value);
    case UI_ANIMATION_ROUNDING_CEILING: return (INT32)ceil(value);
    default: return (INT32)floor(value + 0.5);
    }
}

static double clamp_value(struct variable *var, double value)
{
    if (value < var->lower) value = var->lower;
    if (value > var->upper) value = var->upper;
    return value;
}

static double storyboard_final_value(struct storyboard *sb, struct variable *var, double fallback);

/* Sets a new value and calls the change handlers; returns TRUE if it changed */
static BOOL variable_set_value(struct variable *var, double value, struct storyboard *sb);

/***********************************************************************
 *          IUIAnimationStoryboard
 */
struct keyframe
{
    struct list entry;
    struct keyframe *base;          /* NULL: the storyboard start */
    double offset;
    struct transition *after;       /* or: at the end of this transition */
    double time;
    BOOL resolved;
};

struct sb_transition
{
    struct list entry;
    struct variable *var;           /* holds a reference */
    struct transition *transition;  /* holds a reference */
    struct keyframe *start_key, *end_key;
    double start, duration, v0, v1;
    BOOL dropped;                   /* trimmed by a later storyboard */
};

struct storyboard
{
    IUIAnimationStoryboard IUIAnimationStoryboard_iface;
    LONG ref;
    struct list entry;              /* in manager->storyboards (weak) */
    struct list scheduled_entry;    /* in manager->scheduled (holds a reference) */
    BOOL in_scheduled;
    struct manager *manager;        /* weak */
    UI_ANIMATION_STORYBOARD_STATUS status;
    struct list transitions;
    struct list keyframes;
    struct keyframe *repeat_start, *repeat_end;
    INT32 repeat_count;
    double start_time, length, elapsed;
    IUnknown *tag_object;
    UINT32 tag_id;
    IUIAnimationStoryboardEventHandler *handler;
};

static inline struct storyboard *impl_from_IUIAnimationStoryboard(IUIAnimationStoryboard *iface)
{
    return CONTAINING_RECORD(iface, struct storyboard, IUIAnimationStoryboard_iface);
}

/***********************************************************************
 *          IUIAnimationManager
 */
struct manager
{
    IUIAnimationManager IUIAnimationManager_iface;
    IUIAnimationTimerUpdateHandler IUIAnimationTimerUpdateHandler_iface;
    LONG ref;
    struct list variables;
    struct list storyboards;
    struct list scheduled;
    UI_ANIMATION_MANAGER_STATUS status;
    UI_ANIMATION_MODE mode;
    BOOL paused, shut_down;
    double last_update;
    IUIAnimationManagerEventHandler *handler;
    IUIAnimationTimerClientEventHandler *timer_client;
    IUIAnimationPriorityComparison *comparisons[4];
};

static inline struct manager *impl_from_IUIAnimationManager(IUIAnimationManager *iface)
{
    return CONTAINING_RECORD(iface, struct manager, IUIAnimationManager_iface);
}

static inline struct manager *impl_from_IUIAnimationTimerUpdateHandler(IUIAnimationTimerUpdateHandler *iface)
{
    return CONTAINING_RECORD(iface, struct manager, IUIAnimationTimerUpdateHandler_iface);
}

static void manager_set_status(struct manager *manager, UI_ANIMATION_MANAGER_STATUS status)
{
    UI_ANIMATION_MANAGER_STATUS previous = manager->status;

    if (previous == status)
        return;
    manager->status = status;
    if (manager->handler)
        IUIAnimationManagerEventHandler_OnManagerStatusChanged(manager->handler, status, previous);
    if (manager->timer_client)
        IUIAnimationTimerClientEventHandler_OnTimerClientStatusChanged(manager->timer_client,
                status == UI_ANIMATION_MANAGER_BUSY ? UI_ANIMATION_TIMER_CLIENT_BUSY : UI_ANIMATION_TIMER_CLIENT_IDLE,
                previous == UI_ANIMATION_MANAGER_BUSY ? UI_ANIMATION_TIMER_CLIENT_BUSY : UI_ANIMATION_TIMER_CLIENT_IDLE);
}

static void manager_update_status(struct manager *manager)
{
    manager_set_status(manager, list_empty(&manager->scheduled) ? UI_ANIMATION_MANAGER_IDLE : UI_ANIMATION_MANAGER_BUSY);
}

/* Storyboard helpers ************************************************/

static void storyboard_set_status(struct storyboard *sb, UI_ANIMATION_STORYBOARD_STATUS status)
{
    UI_ANIMATION_STORYBOARD_STATUS previous = sb->status;

    if (previous == status)
        return;
    sb->status = status;
    if (sb->handler)
        IUIAnimationStoryboardEventHandler_OnStoryboardStatusChanged(sb->handler, &sb->IUIAnimationStoryboard_iface,
                                                                     status, previous);
}

static struct sb_transition *storyboard_find_transition(struct storyboard *sb, struct transition *t)
{
    struct sb_transition *entry;

    LIST_FOR_EACH_ENTRY(entry, &sb->transitions, struct sb_transition, entry)
        if (entry->transition == t) return entry;
    return NULL;
}

static double keyframe_time(struct storyboard *sb, struct keyframe *key)
{
    struct sb_transition *entry;

    if (!key || key == (struct keyframe *)UI_ANIMATION_KEYFRAME_STORYBOARD_START)
        return 0.0;
    if (!key->resolved)
    {
        if (key->after)
        {
            entry = storyboard_find_transition(sb, key->after);
            key->time = entry ? entry->start + entry->duration : 0.0;
        }
        else
        {
            key->time = keyframe_time(sb, key->base) + key->offset;
        }
        key->resolved = TRUE;
    }
    return key->time;
}

static BOOL storyboard_valid_keyframe(struct storyboard *sb, UI_ANIMATION_KEYFRAME key)
{
    struct keyframe *k;

    if (key == UI_ANIMATION_KEYFRAME_STORYBOARD_START)
        return TRUE;
    LIST_FOR_EACH_ENTRY(k, &sb->keyframes, struct keyframe, entry)
        if ((UI_ANIMATION_KEYFRAME)k == key) return TRUE;
    return FALSE;
}

/* Places every transition on the timeline, starting from the variables'
 * current values */
static void storyboard_resolve(struct storyboard *sb)
{
    struct sb_transition *entry, *prev;
    struct keyframe *key;
    double natural;

    LIST_FOR_EACH_ENTRY(key, &sb->keyframes, struct keyframe, entry)
        key->resolved = FALSE;

    sb->length = 0.0;
    LIST_FOR_EACH_ENTRY(entry, &sb->transitions, struct sb_transition, entry)
    {
        double start = 0.0, v0 = entry->var->value;

        /* After the previous transition of the same variable */
        for (prev = LIST_ENTRY(entry->entry.prev, struct sb_transition, entry);
             &prev->entry != &sb->transitions;
             prev = LIST_ENTRY(prev->entry.prev, struct sb_transition, entry))
        {
            if (prev->var == entry->var)
            {
                start = prev->start + prev->duration;
                v0 = prev->v1;
                break;
            }
        }
        if (entry->start_key)
            start = keyframe_time(sb, entry->start_key);

        entry->start = start;
        entry->v0 = v0;
        natural = transition_duration(entry->transition, v0);
        if (entry->end_key)
        {
            entry->duration = keyframe_time(sb, entry->end_key) - start;
            if (entry->duration < 0.0) entry->duration = 0.0;
        }
        else
        {
            entry->duration = natural;
        }
        entry->v1 = transition_final(entry->transition, v0);
        if (start + entry->duration > sb->length)
            sb->length = start + entry->duration;
    }
    LIST_FOR_EACH_ENTRY(key, &sb->keyframes, struct keyframe, entry)
    {
        double t = keyframe_time(sb, key);
        if (t > sb->length) sb->length = t;
    }
}

static BOOL storyboard_repeats(struct storyboard *sb, double *start, double *period)
{
    if (!sb->repeat_start || !sb->repeat_end || !sb->repeat_count)
        return FALSE;
    *start = keyframe_time(sb, sb->repeat_start);
    *period = keyframe_time(sb, sb->repeat_end) - *start;
    return *period > 0.0;
}

/* Storyboard time -> time on the timeline without the repeats;
 * returns FALSE once the storyboard is over */
static BOOL storyboard_map_time(struct storyboard *sb, double t, double *mapped)
{
    double start, period, total;

    if (!storyboard_repeats(sb, &start, &period) || t < start)
    {
        *mapped = t;
        return t < sb->length || (sb->repeat_count == UI_ANIMATION_REPEAT_INDEFINITELY && sb->repeat_start);
    }
    if (sb->repeat_count == UI_ANIMATION_REPEAT_INDEFINITELY)
    {
        *mapped = start + fmod(t - start, period);
        return TRUE;
    }
    total = period * sb->repeat_count;
    if (t < start + total)
    {
        *mapped = start + fmod(t - start, period);
        return TRUE;
    }
    *mapped = t - total + period;
    return *mapped < sb->length;
}

/* Moves the storyboard to time "t" (seconds since it started) */
static BOOL storyboard_apply(struct storyboard *sb, double t, BOOL end)
{
    struct sb_transition *entry;
    BOOL changed = FALSE;
    double mapped = sb->length;

    if (!end)
        end = !storyboard_map_time(sb, t, &mapped);
    if (end)
        mapped = sb->length;

    LIST_FOR_EACH_ENTRY(entry, &sb->transitions, struct sb_transition, entry)
    {
        entry->var->touched = FALSE;
        entry->var->new_start = -1.0;
    }
    LIST_FOR_EACH_ENTRY(entry, &sb->transitions, struct sb_transition, entry)
    {
        struct variable *var = entry->var;
        double value;

        if (entry->dropped || entry->start > mapped || entry->start < var->new_start)
            continue;
        if (end || mapped >= entry->start + entry->duration)
            value = entry->v1;
        else if (entry->duration > 0.0)
        {
            double natural = transition_duration(entry->transition, entry->v0);
            double offset = mapped - entry->start;

            /* Between two keyframes: stretch the transition */
            if (entry->end_key && natural > 0.0)
                offset = offset * natural / entry->duration;
            value = transition_value(entry->transition, entry->v0, offset, natural > 0.0 ? natural : entry->duration);
        }
        else
            value = entry->v1;
        var->new_value = value;
        var->new_start = entry->start;
        var->touched = TRUE;
    }
    LIST_FOR_EACH_ENTRY(entry, &sb->transitions, struct sb_transition, entry)
    {
        if (entry->var->touched)
        {
            entry->var->touched = FALSE;
            changed |= variable_set_value(entry->var, entry->var->new_value, sb);
        }
    }
    return changed;
}

static void storyboard_unschedule(struct storyboard *sb, UI_ANIMATION_STORYBOARD_STATUS status)
{
    struct sb_transition *entry;
    struct manager *manager = sb->manager;

    LIST_FOR_EACH_ENTRY(entry, &sb->transitions, struct sb_transition, entry)
        if (entry->var->storyboard == sb) entry->var->storyboard = NULL;

    storyboard_set_status(sb, status);
    if (sb->in_scheduled)
    {
        sb->in_scheduled = FALSE;
        list_remove(&sb->scheduled_entry);
        if (manager)
            manager_update_status(manager);
        IUIAnimationStoryboard_Release(&sb->IUIAnimationStoryboard_iface);
    }
}

static void storyboard_finish_now(struct storyboard *sb)
{
    IUIAnimationStoryboard_AddRef(&sb->IUIAnimationStoryboard_iface);
    if (sb->status == UI_ANIMATION_STORYBOARD_SCHEDULED)
        storyboard_set_status(sb, UI_ANIMATION_STORYBOARD_PLAYING);
    storyboard_apply(sb, 0.0, TRUE);
    if (sb->handler)
        IUIAnimationStoryboardEventHandler_OnStoryboardUpdated(sb->handler, &sb->IUIAnimationStoryboard_iface);
    storyboard_unschedule(sb, UI_ANIMATION_STORYBOARD_FINISHED);
    IUIAnimationStoryboard_Release(&sb->IUIAnimationStoryboard_iface);
}

static double storyboard_final_value(struct storyboard *sb, struct variable *var, double fallback)
{
    struct sb_transition *entry;
    double value = fallback, start = -1.0;

    LIST_FOR_EACH_ENTRY(entry, &sb->transitions, struct sb_transition, entry)
    {
        if (entry->var == var && !entry->dropped && entry->start >= start)
        {
            value = entry->v1;
            start = entry->start;
        }
    }
    return value;
}

static HRESULT WINAPI storyboard_QueryInterface(IUIAnimationStoryboard *iface, REFIID iid, void **obj)
{
    TRACE("(%p)->(%s %p)\n", iface, debugstr_guid(iid), obj);

    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IUIAnimationStoryboard))
    {
        IUIAnimationStoryboard_AddRef(iface);
        *obj = iface;
        return S_OK;
    }
    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI storyboard_AddRef(IUIAnimationStoryboard *iface)
{
    struct storyboard *This = impl_from_IUIAnimationStoryboard(iface);
    return InterlockedIncrement(&This->ref);
}

static ULONG WINAPI storyboard_Release(IUIAnimationStoryboard *iface)
{
    struct storyboard *This = impl_from_IUIAnimationStoryboard(iface);
    ULONG ref = InterlockedDecrement(&This->ref);
    struct sb_transition *entry, *next_entry;
    struct keyframe *key, *next_key;

    if (!ref)
    {
        EnterCriticalSection(&uia_cs);
        if (This->manager)
            list_remove(&This->entry);
        LIST_FOR_EACH_ENTRY_SAFE(entry, next_entry, &This->transitions, struct sb_transition, entry)
        {
            if (entry->var->storyboard == This) entry->var->storyboard = NULL;
            IUIAnimationVariable_Release(&entry->var->IUIAnimationVariable_iface);
            IUIAnimationTransition_Release(&entry->transition->IUIAnimationTransition_iface);
            free(entry);
        }
        LIST_FOR_EACH_ENTRY_SAFE(key, next_key, &This->keyframes, struct keyframe, entry)
            free(key);
        LeaveCriticalSection(&uia_cs);
        if (This->tag_object) IUnknown_Release(This->tag_object);
        if (This->handler) IUIAnimationStoryboardEventHandler_Release(This->handler);
        free(This);
        InterlockedDecrement(&object_count);
    }
    return ref;
}

static HRESULT storyboard_add(struct storyboard *This, IUIAnimationVariable *variable,
                              IUIAnimationTransition *transition, UI_ANIMATION_KEYFRAME start_key,
                              UI_ANIMATION_KEYFRAME end_key)
{
    struct transition *t = unsafe_transition(transition);
    struct sb_transition *entry;
    HRESULT hr = S_OK;

    if (!variable || !t)
        return E_INVALIDARG;
    EnterCriticalSection(&uia_cs);
    if (This->status != UI_ANIMATION_STORYBOARD_BUILDING)
        hr = UI_E_STORYBOARD_ACTIVE;
    else if ((start_key && !storyboard_valid_keyframe(This, start_key)) ||
             (end_key && !storyboard_valid_keyframe(This, end_key)))
        hr = E_INVALIDARG;
    else if (storyboard_find_transition(This, t))
        hr = UI_E_TRANSITION_ALREADY_USED;
    else if (!(entry = calloc(1, sizeof(*entry))))
        hr = E_OUTOFMEMORY;
    else
    {
        entry->var = impl_from_IUIAnimationVariable(variable);
        IUIAnimationVariable_AddRef(variable);
        entry->transition = t;
        IUIAnimationTransition_AddRef(transition);
        entry->start_key = (struct keyframe *)start_key;
        entry->end_key = (struct keyframe *)end_key;
        list_add_tail(&This->transitions, &entry->entry);
    }
    LeaveCriticalSection(&uia_cs);
    return hr;
}

static HRESULT WINAPI storyboard_AddTransition(IUIAnimationStoryboard *iface, IUIAnimationVariable *variable,
                                               IUIAnimationTransition *transition)
{
    TRACE("(%p)->(%p %p)\n", iface, variable, transition);
    return storyboard_add(impl_from_IUIAnimationStoryboard(iface), variable, transition, NULL, NULL);
}

static HRESULT storyboard_add_keyframe(struct storyboard *This, struct keyframe *base, double offset,
                                       struct transition *after, UI_ANIMATION_KEYFRAME *out)
{
    struct keyframe *key;

    if (!out)
        return E_POINTER;
    if (!(key = calloc(1, sizeof(*key))))
        return E_OUTOFMEMORY;
    key->base = base;
    key->offset = offset;
    key->after = after;
    list_add_tail(&This->keyframes, &key->entry);
    *out = (UI_ANIMATION_KEYFRAME)key;
    return S_OK;
}

static HRESULT WINAPI storyboard_AddKeyframeAtOffset(IUIAnimationStoryboard *iface, UI_ANIMATION_KEYFRAME existing,
                                                     UI_ANIMATION_SECONDS offset, UI_ANIMATION_KEYFRAME *keyframe)
{
    struct storyboard *This = impl_from_IUIAnimationStoryboard(iface);
    HRESULT hr;

    TRACE("(%p)->(%p %f %p)\n", iface, existing, offset, keyframe);

    EnterCriticalSection(&uia_cs);
    if (This->status != UI_ANIMATION_STORYBOARD_BUILDING)
        hr = UI_E_STORYBOARD_ACTIVE;
    else if (!storyboard_valid_keyframe(This, existing) || offset < 0.0)
        hr = E_INVALIDARG;
    else
        hr = storyboard_add_keyframe(This, existing == UI_ANIMATION_KEYFRAME_STORYBOARD_START ? NULL :
                                     (struct keyframe *)existing, offset, NULL, keyframe);
    LeaveCriticalSection(&uia_cs);
    return hr;
}

static HRESULT WINAPI storyboard_AddKeyframeAfterTransition(IUIAnimationStoryboard *iface,
                                                            IUIAnimationTransition *transition,
                                                            UI_ANIMATION_KEYFRAME *keyframe)
{
    struct storyboard *This = impl_from_IUIAnimationStoryboard(iface);
    struct transition *t = unsafe_transition(transition);
    HRESULT hr;

    TRACE("(%p)->(%p %p)\n", iface, transition, keyframe);

    EnterCriticalSection(&uia_cs);
    if (This->status != UI_ANIMATION_STORYBOARD_BUILDING)
        hr = UI_E_STORYBOARD_ACTIVE;
    else if (!t || !storyboard_find_transition(This, t))
        hr = UI_E_TRANSITION_NOT_IN_STORYBOARD;
    else
        hr = storyboard_add_keyframe(This, NULL, 0.0, t, keyframe);
    LeaveCriticalSection(&uia_cs);
    return hr;
}

static HRESULT WINAPI storyboard_AddTransitionAtKeyframe(IUIAnimationStoryboard *iface, IUIAnimationVariable *variable,
                                                         IUIAnimationTransition *transition, UI_ANIMATION_KEYFRAME start_key)
{
    TRACE("(%p)->(%p %p %p)\n", iface, variable, transition, start_key);
    if (!start_key)
        return E_INVALIDARG;
    return storyboard_add(impl_from_IUIAnimationStoryboard(iface), variable, transition, start_key, NULL);
}

static HRESULT WINAPI storyboard_AddTransitionBetweenKeyframes(IUIAnimationStoryboard *iface,
        IUIAnimationVariable *variable, IUIAnimationTransition *transition,
        UI_ANIMATION_KEYFRAME start_key, UI_ANIMATION_KEYFRAME end_key)
{
    TRACE("(%p)->(%p %p %p %p)\n", iface, variable, transition, start_key, end_key);
    if (!start_key || !end_key)
        return E_INVALIDARG;
    return storyboard_add(impl_from_IUIAnimationStoryboard(iface), variable, transition, start_key, end_key);
}

static HRESULT WINAPI storyboard_RepeatBetweenKeyframes(IUIAnimationStoryboard *iface, UI_ANIMATION_KEYFRAME start_key,
                                                        UI_ANIMATION_KEYFRAME end_key, INT32 count)
{
    struct storyboard *This = impl_from_IUIAnimationStoryboard(iface);
    HRESULT hr = S_OK;

    TRACE("(%p)->(%p %p %d)\n", iface, start_key, end_key, count);

    EnterCriticalSection(&uia_cs);
    if (This->status != UI_ANIMATION_STORYBOARD_BUILDING)
        hr = UI_E_STORYBOARD_ACTIVE;
    else if (!storyboard_valid_keyframe(This, start_key) || !storyboard_valid_keyframe(This, end_key) ||
             end_key == UI_ANIMATION_KEYFRAME_STORYBOARD_START ||
             (count < 0 && count != UI_ANIMATION_REPEAT_INDEFINITELY))
        hr = E_INVALIDARG;
    else if (This->repeat_end)
        hr = UI_E_LOOPS_OVERLAP; /* one repeated section per storyboard */
    else
    {
        This->repeat_start = start_key == UI_ANIMATION_KEYFRAME_STORYBOARD_START ?
                             (struct keyframe *)UI_ANIMATION_KEYFRAME_STORYBOARD_START : (struct keyframe *)start_key;
        This->repeat_end = (struct keyframe *)end_key;
        This->repeat_count = count;
    }
    LeaveCriticalSection(&uia_cs);
    return hr;
}

static HRESULT WINAPI storyboard_HoldVariable(IUIAnimationStoryboard *iface, IUIAnimationVariable *variable)
{
    TRACE("(%p)->(%p)\n", iface, variable);
    /* The variable keeps its value until the storyboard ends anyway */
    return variable ? S_OK : E_INVALIDARG;
}

static HRESULT WINAPI storyboard_SetLongestAcceptableDelay(IUIAnimationStoryboard *iface, UI_ANIMATION_SECONDS delay)
{
    TRACE("(%p)->(%f)\n", iface, delay);
    return S_OK;
}

static HRESULT storyboard_schedule(struct storyboard *This, double now, UI_ANIMATION_SCHEDULING_RESULT *result)
{
    struct manager *manager = This->manager;
    struct storyboard *other, *next;
    struct sb_transition *entry, *other_entry;

    if (result) *result = UI_ANIMATION_SCHEDULING_UNEXPECTED_FAILURE;
    if (!manager || manager->shut_down)
        return UI_E_SHUTDOWN_CALLED;
    if (This->status != UI_ANIMATION_STORYBOARD_BUILDING && This->status != UI_ANIMATION_STORYBOARD_READY)
    {
        if (result) *result = UI_ANIMATION_SCHEDULING_ALREADY_SCHEDULED;
        return S_OK;
    }

    storyboard_resolve(This);

    /* A variable is animated by one storyboard at a time: the new one wins */
    LIST_FOR_EACH_ENTRY_SAFE(other, next, &manager->scheduled, struct storyboard, scheduled_entry)
    {
        BOOL any_left = FALSE, conflict = FALSE;

        LIST_FOR_EACH_ENTRY(other_entry, &other->transitions, struct sb_transition, entry)
        {
            LIST_FOR_EACH_ENTRY(entry, &This->transitions, struct sb_transition, entry)
            {
                if (entry->var == other_entry->var)
                {
                    other_entry->dropped = TRUE;
                    conflict = TRUE;
                }
            }
            if (!other_entry->dropped) any_left = TRUE;
        }
        if (conflict && !any_left)
            storyboard_unschedule(other, other->status == UI_ANIMATION_STORYBOARD_PLAYING ?
                                  UI_ANIMATION_STORYBOARD_TRUNCATED : UI_ANIMATION_STORYBOARD_CANCELLED);
    }

    This->start_time = now;
    This->elapsed = 0.0;
    LIST_FOR_EACH_ENTRY(entry, &This->transitions, struct sb_transition, entry)
        entry->var->storyboard = This;

    IUIAnimationStoryboard_AddRef(&This->IUIAnimationStoryboard_iface);
    This->in_scheduled = TRUE;
    list_add_tail(&manager->scheduled, &This->scheduled_entry);
    storyboard_set_status(This, UI_ANIMATION_STORYBOARD_SCHEDULED);
    if (result) *result = UI_ANIMATION_SCHEDULING_SUCCEEDED;

    if (manager->mode == UI_ANIMATION_MODE_DISABLED)
        storyboard_finish_now(This);
    else
        manager_update_status(manager);
    return S_OK;
}

static HRESULT WINAPI storyboard_Schedule(IUIAnimationStoryboard *iface, UI_ANIMATION_SECONDS now,
                                          UI_ANIMATION_SCHEDULING_RESULT *result)
{
    struct storyboard *This = impl_from_IUIAnimationStoryboard(iface);
    HRESULT hr;

    TRACE("(%p)->(%f %p)\n", iface, now, result);

    EnterCriticalSection(&uia_cs);
    hr = storyboard_schedule(This, now, result);
    LeaveCriticalSection(&uia_cs);
    return hr;
}

static HRESULT WINAPI storyboard_Conclude(IUIAnimationStoryboard *iface)
{
    struct storyboard *This = impl_from_IUIAnimationStoryboard(iface);
    double start, period;

    TRACE("(%p)\n", iface);

    EnterCriticalSection(&uia_cs);
    /* Play the current repetition to its end, then the rest */
    if (This->repeat_count == UI_ANIMATION_REPEAT_INDEFINITELY && storyboard_repeats(This, &start, &period))
    {
        if (This->elapsed > start)
            This->repeat_count = (INT32)floor((This->elapsed - start) / period) + 1;
        else
            This->repeat_count = 1;
    }
    LeaveCriticalSection(&uia_cs);
    return S_OK;
}

static HRESULT WINAPI storyboard_Finish(IUIAnimationStoryboard *iface, UI_ANIMATION_SECONDS deadline)
{
    struct storyboard *This = impl_from_IUIAnimationStoryboard(iface);

    TRACE("(%p)->(%f)\n", iface, deadline);

    EnterCriticalSection(&uia_cs);
    if (This->in_scheduled)
        storyboard_finish_now(This);
    LeaveCriticalSection(&uia_cs);
    return S_OK;
}

static HRESULT WINAPI storyboard_Abandon(IUIAnimationStoryboard *iface)
{
    struct storyboard *This = impl_from_IUIAnimationStoryboard(iface);

    TRACE("(%p)\n", iface);

    EnterCriticalSection(&uia_cs);
    if (This->in_scheduled)
        storyboard_unschedule(This, UI_ANIMATION_STORYBOARD_CANCELLED);
    LeaveCriticalSection(&uia_cs);
    return S_OK;
}

static HRESULT WINAPI storyboard_SetTag(IUIAnimationStoryboard *iface, IUnknown *object, UINT32 id)
{
    struct storyboard *This = impl_from_IUIAnimationStoryboard(iface);

    if (object) IUnknown_AddRef(object);
    if (This->tag_object) IUnknown_Release(This->tag_object);
    This->tag_object = object;
    This->tag_id = id;
    return S_OK;
}

static HRESULT WINAPI storyboard_GetTag(IUIAnimationStoryboard *iface, IUnknown **object, UINT32 *id)
{
    struct storyboard *This = impl_from_IUIAnimationStoryboard(iface);

    if (!This->tag_object && !This->tag_id)
        return UI_E_VALUE_NOT_SET;
    if (object)
    {
        *object = This->tag_object;
        if (*object) IUnknown_AddRef(*object);
    }
    if (id) *id = This->tag_id;
    return S_OK;
}

static HRESULT WINAPI storyboard_GetStatus(IUIAnimationStoryboard *iface, UI_ANIMATION_STORYBOARD_STATUS *status)
{
    struct storyboard *This = impl_from_IUIAnimationStoryboard(iface);

    if (!status)
        return E_POINTER;
    *status = This->status;
    return S_OK;
}

static HRESULT WINAPI storyboard_GetElapsedTime(IUIAnimationStoryboard *iface, UI_ANIMATION_SECONDS *elapsed)
{
    struct storyboard *This = impl_from_IUIAnimationStoryboard(iface);

    if (!elapsed)
        return E_POINTER;
    if (This->status != UI_ANIMATION_STORYBOARD_PLAYING)
        return UI_E_STORYBOARD_NOT_PLAYING;
    *elapsed = This->elapsed;
    return S_OK;
}

static HRESULT WINAPI storyboard_SetStoryboardEventHandler(IUIAnimationStoryboard *iface,
                                                           IUIAnimationStoryboardEventHandler *handler)
{
    struct storyboard *This = impl_from_IUIAnimationStoryboard(iface);

    if (handler) IUIAnimationStoryboardEventHandler_AddRef(handler);
    if (This->handler) IUIAnimationStoryboardEventHandler_Release(This->handler);
    This->handler = handler;
    return S_OK;
}

static const IUIAnimationStoryboardVtbl storyboard_vtbl =
{
    storyboard_QueryInterface,
    storyboard_AddRef,
    storyboard_Release,
    storyboard_AddTransition,
    storyboard_AddKeyframeAtOffset,
    storyboard_AddKeyframeAfterTransition,
    storyboard_AddTransitionAtKeyframe,
    storyboard_AddTransitionBetweenKeyframes,
    storyboard_RepeatBetweenKeyframes,
    storyboard_HoldVariable,
    storyboard_SetLongestAcceptableDelay,
    storyboard_Schedule,
    storyboard_Conclude,
    storyboard_Finish,
    storyboard_Abandon,
    storyboard_SetTag,
    storyboard_GetTag,
    storyboard_GetStatus,
    storyboard_GetElapsedTime,
    storyboard_SetStoryboardEventHandler,
};

/* Variable methods ***************************************************/

static BOOL variable_set_value(struct variable *var, double value, struct storyboard *sb)
{
    double previous = var->value;
    INT32 int_previous, int_value;

    value = clamp_value(var, value);
    if (value == previous)
        return FALSE;
    var->previous = previous;
    var->value = value;
    if (var->change_handler)
        IUIAnimationVariableChangeHandler_OnValueChanged(var->change_handler,
                sb ? &sb->IUIAnimationStoryboard_iface : NULL, &var->IUIAnimationVariable_iface, value, previous);
    int_previous = round_value(var, previous);
    int_value = round_value(var, value);
    if (var->integer_handler && int_value != int_previous)
        IUIAnimationVariableIntegerChangeHandler_OnIntegerValueChanged(var->integer_handler,
                sb ? &sb->IUIAnimationStoryboard_iface : NULL, &var->IUIAnimationVariable_iface, int_value, int_previous);
    return TRUE;
}

static HRESULT WINAPI variable_QueryInterface(IUIAnimationVariable *iface, REFIID iid, void **obj)
{
    TRACE("(%p)->(%s %p)\n", iface, debugstr_guid(iid), obj);

    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IUIAnimationVariable))
    {
        IUIAnimationVariable_AddRef(iface);
        *obj = iface;
        return S_OK;
    }
    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI variable_AddRef(IUIAnimationVariable *iface)
{
    struct variable *This = impl_from_IUIAnimationVariable(iface);
    return InterlockedIncrement(&This->ref);
}

static ULONG WINAPI variable_Release(IUIAnimationVariable *iface)
{
    struct variable *This = impl_from_IUIAnimationVariable(iface);
    ULONG ref = InterlockedDecrement(&This->ref);

    if (!ref)
    {
        EnterCriticalSection(&uia_cs);
        if (This->manager)
            list_remove(&This->entry);
        LeaveCriticalSection(&uia_cs);
        if (This->tag_object) IUnknown_Release(This->tag_object);
        if (This->change_handler) IUIAnimationVariableChangeHandler_Release(This->change_handler);
        if (This->integer_handler) IUIAnimationVariableIntegerChangeHandler_Release(This->integer_handler);
        free(This);
        InterlockedDecrement(&object_count);
    }
    return ref;
}

static HRESULT WINAPI variable_GetValue(IUIAnimationVariable *iface, DOUBLE *value)
{
    struct variable *This = impl_from_IUIAnimationVariable(iface);

    if (!value) return E_POINTER;
    *value = This->value;
    return S_OK;
}

static HRESULT WINAPI variable_GetFinalValue(IUIAnimationVariable *iface, DOUBLE *value)
{
    struct variable *This = impl_from_IUIAnimationVariable(iface);

    if (!value) return E_POINTER;
    EnterCriticalSection(&uia_cs);
    *value = This->storyboard ? clamp_value(This, storyboard_final_value(This->storyboard, This, This->value)) : This->value;
    LeaveCriticalSection(&uia_cs);
    return S_OK;
}

static HRESULT WINAPI variable_GetPreviousValue(IUIAnimationVariable *iface, DOUBLE *value)
{
    struct variable *This = impl_from_IUIAnimationVariable(iface);

    if (!value) return E_POINTER;
    *value = This->previous;
    return S_OK;
}

static HRESULT WINAPI variable_GetIntegerValue(IUIAnimationVariable *iface, INT32 *value)
{
    struct variable *This = impl_from_IUIAnimationVariable(iface);

    if (!value) return E_POINTER;
    *value = round_value(This, This->value);
    return S_OK;
}

static HRESULT WINAPI variable_GetFinalIntegerValue(IUIAnimationVariable *iface, INT32 *value)
{
    struct variable *This = impl_from_IUIAnimationVariable(iface);
    DOUBLE final;

    if (!value) return E_POINTER;
    variable_GetFinalValue(iface, &final);
    *value = round_value(This, final);
    return S_OK;
}

static HRESULT WINAPI variable_GetPreviousIntegerValue(IUIAnimationVariable *iface, INT32 *value)
{
    struct variable *This = impl_from_IUIAnimationVariable(iface);

    if (!value) return E_POINTER;
    *value = round_value(This, This->previous);
    return S_OK;
}

static HRESULT WINAPI variable_GetCurrentStoryboard(IUIAnimationVariable *iface, IUIAnimationStoryboard **storyboard)
{
    struct variable *This = impl_from_IUIAnimationVariable(iface);

    if (!storyboard) return E_POINTER;
    EnterCriticalSection(&uia_cs);
    *storyboard = This->storyboard ? &This->storyboard->IUIAnimationStoryboard_iface : NULL;
    if (*storyboard) IUIAnimationStoryboard_AddRef(*storyboard);
    LeaveCriticalSection(&uia_cs);
    return *storyboard ? S_OK : S_FALSE;
}

static HRESULT WINAPI variable_SetLowerBound(IUIAnimationVariable *iface, DOUBLE bound)
{
    struct variable *This = impl_from_IUIAnimationVariable(iface);

    if (bound > This->upper) return E_INVALIDARG;
    This->lower = bound;
    return S_OK;
}

static HRESULT WINAPI variable_SetUpperBound(IUIAnimationVariable *iface, DOUBLE bound)
{
    struct variable *This = impl_from_IUIAnimationVariable(iface);

    if (bound < This->lower) return E_INVALIDARG;
    This->upper = bound;
    return S_OK;
}

static HRESULT WINAPI variable_SetRoundingMode(IUIAnimationVariable *iface, UI_ANIMATION_ROUNDING_MODE mode)
{
    struct variable *This = impl_from_IUIAnimationVariable(iface);

    if (mode > UI_ANIMATION_ROUNDING_CEILING) return E_INVALIDARG;
    This->rounding = mode;
    return S_OK;
}

static HRESULT WINAPI variable_SetTag(IUIAnimationVariable *iface, IUnknown *object, UINT32 id)
{
    struct variable *This = impl_from_IUIAnimationVariable(iface);

    if (object) IUnknown_AddRef(object);
    if (This->tag_object) IUnknown_Release(This->tag_object);
    This->tag_object = object;
    This->tag_id = id;
    return S_OK;
}

static HRESULT WINAPI variable_GetTag(IUIAnimationVariable *iface, IUnknown **object, UINT32 *id)
{
    struct variable *This = impl_from_IUIAnimationVariable(iface);

    if (!This->tag_object && !This->tag_id)
        return UI_E_VALUE_NOT_SET;
    if (object)
    {
        *object = This->tag_object;
        if (*object) IUnknown_AddRef(*object);
    }
    if (id) *id = This->tag_id;
    return S_OK;
}

static HRESULT WINAPI variable_SetVariableChangeHandler(IUIAnimationVariable *iface, IUIAnimationVariableChangeHandler *handler)
{
    struct variable *This = impl_from_IUIAnimationVariable(iface);

    if (handler) IUIAnimationVariableChangeHandler_AddRef(handler);
    if (This->change_handler) IUIAnimationVariableChangeHandler_Release(This->change_handler);
    This->change_handler = handler;
    return S_OK;
}

static HRESULT WINAPI variable_SetVariableIntegerChangeHandler(IUIAnimationVariable *iface,
                                                               IUIAnimationVariableIntegerChangeHandler *handler)
{
    struct variable *This = impl_from_IUIAnimationVariable(iface);

    if (handler) IUIAnimationVariableIntegerChangeHandler_AddRef(handler);
    if (This->integer_handler) IUIAnimationVariableIntegerChangeHandler_Release(This->integer_handler);
    This->integer_handler = handler;
    return S_OK;
}

static const IUIAnimationVariableVtbl variable_vtbl =
{
    variable_QueryInterface,
    variable_AddRef,
    variable_Release,
    variable_GetValue,
    variable_GetFinalValue,
    variable_GetPreviousValue,
    variable_GetIntegerValue,
    variable_GetFinalIntegerValue,
    variable_GetPreviousIntegerValue,
    variable_GetCurrentStoryboard,
    variable_SetLowerBound,
    variable_SetUpperBound,
    variable_SetRoundingMode,
    variable_SetTag,
    variable_GetTag,
    variable_SetVariableChangeHandler,
    variable_SetVariableIntegerChangeHandler,
};

/* Manager methods ****************************************************/

static HRESULT WINAPI manager_QueryInterface(IUIAnimationManager *iface, REFIID iid, void **obj)
{
    struct manager *This = impl_from_IUIAnimationManager(iface);

    TRACE("(%p)->(%s %p)\n", iface, debugstr_guid(iid), obj);

    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IUIAnimationManager))
        *obj = &This->IUIAnimationManager_iface;
    else if (IsEqualIID(iid, &IID_IUIAnimationTimerUpdateHandler))
        *obj = &This->IUIAnimationTimerUpdateHandler_iface;
    else
    {
        *obj = NULL;
        return E_NOINTERFACE;
    }
    IUnknown_AddRef((IUnknown *)*obj);
    return S_OK;
}

static ULONG WINAPI manager_AddRef(IUIAnimationManager *iface)
{
    struct manager *This = impl_from_IUIAnimationManager(iface);
    return InterlockedIncrement(&This->ref);
}

static void manager_shutdown(struct manager *This)
{
    struct storyboard *sb, *next;
    unsigned int i;

    This->shut_down = TRUE;
    LIST_FOR_EACH_ENTRY_SAFE(sb, next, &This->scheduled, struct storyboard, scheduled_entry)
        storyboard_unschedule(sb, UI_ANIMATION_STORYBOARD_CANCELLED);
    if (This->handler)
    {
        IUIAnimationManagerEventHandler_Release(This->handler);
        This->handler = NULL;
    }
    if (This->timer_client)
    {
        IUIAnimationTimerClientEventHandler_Release(This->timer_client);
        This->timer_client = NULL;
    }
    for (i = 0; i < ARRAY_SIZE(This->comparisons); i++)
    {
        if (This->comparisons[i])
        {
            IUIAnimationPriorityComparison_Release(This->comparisons[i]);
            This->comparisons[i] = NULL;
        }
    }
}

static ULONG WINAPI manager_Release(IUIAnimationManager *iface)
{
    struct manager *This = impl_from_IUIAnimationManager(iface);
    ULONG ref = InterlockedDecrement(&This->ref);
    struct variable *var, *next_var;
    struct storyboard *sb, *next_sb;

    if (!ref)
    {
        EnterCriticalSection(&uia_cs);
        This->ref = 1; /* handlers may be called */
        manager_shutdown(This);
        LIST_FOR_EACH_ENTRY_SAFE(var, next_var, &This->variables, struct variable, entry)
        {
            list_remove(&var->entry);
            var->manager = NULL;
        }
        LIST_FOR_EACH_ENTRY_SAFE(sb, next_sb, &This->storyboards, struct storyboard, entry)
        {
            list_remove(&sb->entry);
            sb->manager = NULL;
        }
        LeaveCriticalSection(&uia_cs);
        free(This);
        InterlockedDecrement(&object_count);
    }
    return ref;
}

static HRESULT WINAPI manager_CreateAnimationVariable(IUIAnimationManager *iface, DOUBLE initial_value,
                                                      IUIAnimationVariable **variable)
{
    struct manager *This = impl_from_IUIAnimationManager(iface);
    struct variable *var;

    TRACE("(%p)->(%f %p)\n", iface, initial_value, variable);

    if (!variable)
        return E_POINTER;
    if (This->shut_down)
        return UI_E_SHUTDOWN_CALLED;
    if (!(var = calloc(1, sizeof(*var))))
        return E_OUTOFMEMORY;
    var->IUIAnimationVariable_iface.lpVtbl = &variable_vtbl;
    var->ref = 1;
    var->value = var->previous = initial_value;
    var->lower = -HUGE_VAL;
    var->upper = HUGE_VAL;
    var->manager = This;
    EnterCriticalSection(&uia_cs);
    list_add_tail(&This->variables, &var->entry);
    LeaveCriticalSection(&uia_cs);
    InterlockedIncrement(&object_count);
    *variable = &var->IUIAnimationVariable_iface;
    return S_OK;
}

static HRESULT WINAPI manager_CreateStoryboard(IUIAnimationManager *iface, IUIAnimationStoryboard **storyboard)
{
    struct manager *This = impl_from_IUIAnimationManager(iface);
    struct storyboard *sb;

    TRACE("(%p)->(%p)\n", iface, storyboard);

    if (!storyboard)
        return E_POINTER;
    if (This->shut_down)
        return UI_E_SHUTDOWN_CALLED;
    if (!(sb = calloc(1, sizeof(*sb))))
        return E_OUTOFMEMORY;
    sb->IUIAnimationStoryboard_iface.lpVtbl = &storyboard_vtbl;
    sb->ref = 1;
    sb->manager = This;
    sb->status = UI_ANIMATION_STORYBOARD_BUILDING;
    list_init(&sb->transitions);
    list_init(&sb->keyframes);
    EnterCriticalSection(&uia_cs);
    list_add_tail(&This->storyboards, &sb->entry);
    LeaveCriticalSection(&uia_cs);
    InterlockedIncrement(&object_count);
    *storyboard = &sb->IUIAnimationStoryboard_iface;
    return S_OK;
}

static HRESULT WINAPI manager_ScheduleTransition(IUIAnimationManager *iface, IUIAnimationVariable *variable,
                                                 IUIAnimationTransition *transition, UI_ANIMATION_SECONDS current_time)
{
    IUIAnimationStoryboard *storyboard;
    HRESULT hr;

    TRACE("(%p)->(%p %p %f)\n", iface, variable, transition, current_time);

    if (FAILED(hr = manager_CreateStoryboard(iface, &storyboard)))
        return hr;
    if (SUCCEEDED(hr = IUIAnimationStoryboard_AddTransition(storyboard, variable, transition)))
        hr = IUIAnimationStoryboard_Schedule(storyboard, current_time, NULL);
    IUIAnimationStoryboard_Release(storyboard);
    return hr;
}

static HRESULT WINAPI manager_FinishAllStoryboards(IUIAnimationManager *iface, UI_ANIMATION_SECONDS max_time)
{
    struct manager *This = impl_from_IUIAnimationManager(iface);
    struct storyboard *sb, *next;

    TRACE("(%p)->(%f)\n", iface, max_time);

    EnterCriticalSection(&uia_cs);
    LIST_FOR_EACH_ENTRY_SAFE(sb, next, &This->scheduled, struct storyboard, scheduled_entry)
        storyboard_finish_now(sb);
    LeaveCriticalSection(&uia_cs);
    return S_OK;
}

static HRESULT WINAPI manager_AbandonAllStoryboards(IUIAnimationManager *iface)
{
    struct manager *This = impl_from_IUIAnimationManager(iface);
    struct storyboard *sb, *next;

    TRACE("(%p)\n", iface);

    EnterCriticalSection(&uia_cs);
    LIST_FOR_EACH_ENTRY_SAFE(sb, next, &This->scheduled, struct storyboard, scheduled_entry)
        storyboard_unschedule(sb, UI_ANIMATION_STORYBOARD_CANCELLED);
    LeaveCriticalSection(&uia_cs);
    return S_OK;
}

static HRESULT manager_update(struct manager *This, UI_ANIMATION_SECONDS now, UI_ANIMATION_UPDATE_RESULT *result)
{
    struct storyboard *sb, *next;
    BOOL changed = FALSE;

    if (result) *result = UI_ANIMATION_UPDATE_NO_CHANGE;
    if (This->shut_down)
        return UI_E_SHUTDOWN_CALLED;
    if (This->paused)
        return S_OK;
    This->last_update = now;

    LIST_FOR_EACH_ENTRY_SAFE(sb, next, &This->scheduled, struct storyboard, scheduled_entry)
    {
        double t = now - sb->start_time, mapped;
        BOOL over;

        if (t < 0.0)
            continue;
        IUIAnimationStoryboard_AddRef(&sb->IUIAnimationStoryboard_iface);
        if (sb->status == UI_ANIMATION_STORYBOARD_SCHEDULED)
            storyboard_set_status(sb, UI_ANIMATION_STORYBOARD_PLAYING);
        sb->elapsed = t;
        over = !storyboard_map_time(sb, t, &mapped);
        changed |= storyboard_apply(sb, t, over);
        if (sb->handler)
            IUIAnimationStoryboardEventHandler_OnStoryboardUpdated(sb->handler, &sb->IUIAnimationStoryboard_iface);
        if (over && sb->in_scheduled)
            storyboard_unschedule(sb, UI_ANIMATION_STORYBOARD_FINISHED);
        IUIAnimationStoryboard_Release(&sb->IUIAnimationStoryboard_iface);
    }
    if (result && changed) *result = UI_ANIMATION_UPDATE_VARIABLES_CHANGED;
    return S_OK;
}

static HRESULT WINAPI manager_Update(IUIAnimationManager *iface, UI_ANIMATION_SECONDS cur_time,
                                     UI_ANIMATION_UPDATE_RESULT *update_result)
{
    struct manager *This = impl_from_IUIAnimationManager(iface);
    HRESULT hr;

    TRACE("(%p)->(%f %p)\n", iface, cur_time, update_result);

    EnterCriticalSection(&uia_cs);
    hr = manager_update(This, cur_time, update_result);
    LeaveCriticalSection(&uia_cs);
    return hr;
}

static HRESULT WINAPI manager_GetVariableFromTag(IUIAnimationManager *iface, IUnknown *object, UINT32 id,
                                                 IUIAnimationVariable **variable)
{
    struct manager *This = impl_from_IUIAnimationManager(iface);
    struct variable *var;
    HRESULT hr = UI_E_VALUE_NOT_SET;

    if (!variable)
        return E_POINTER;
    *variable = NULL;
    EnterCriticalSection(&uia_cs);
    LIST_FOR_EACH_ENTRY(var, &This->variables, struct variable, entry)
    {
        if (var->tag_object == object && var->tag_id == id)
        {
            if (*variable)
            {
                IUIAnimationVariable_Release(*variable);
                *variable = NULL;
                hr = UI_E_AMBIGUOUS_MATCH;
                break;
            }
            *variable = &var->IUIAnimationVariable_iface;
            IUIAnimationVariable_AddRef(*variable);
            hr = S_OK;
        }
    }
    LeaveCriticalSection(&uia_cs);
    return hr;
}

static HRESULT WINAPI manager_GetStoryboardFromTag(IUIAnimationManager *iface, IUnknown *object, UINT32 id,
                                                   IUIAnimationStoryboard **storyboard)
{
    struct manager *This = impl_from_IUIAnimationManager(iface);
    struct storyboard *sb;
    HRESULT hr = UI_E_VALUE_NOT_SET;

    if (!storyboard)
        return E_POINTER;
    *storyboard = NULL;
    EnterCriticalSection(&uia_cs);
    LIST_FOR_EACH_ENTRY(sb, &This->storyboards, struct storyboard, entry)
    {
        if (sb->tag_object == object && sb->tag_id == id)
        {
            if (*storyboard)
            {
                IUIAnimationStoryboard_Release(*storyboard);
                *storyboard = NULL;
                hr = UI_E_AMBIGUOUS_MATCH;
                break;
            }
            *storyboard = &sb->IUIAnimationStoryboard_iface;
            IUIAnimationStoryboard_AddRef(*storyboard);
            hr = S_OK;
        }
    }
    LeaveCriticalSection(&uia_cs);
    return hr;
}

static HRESULT WINAPI manager_GetStatus(IUIAnimationManager *iface, UI_ANIMATION_MANAGER_STATUS *status)
{
    struct manager *This = impl_from_IUIAnimationManager(iface);

    if (!status)
        return E_POINTER;
    *status = This->status;
    return S_OK;
}

static HRESULT WINAPI manager_SetAnimationMode(IUIAnimationManager *iface, UI_ANIMATION_MODE mode)
{
    struct manager *This = impl_from_IUIAnimationManager(iface);

    TRACE("(%p)->(%d)\n", iface, mode);

    if (mode > UI_ANIMATION_MODE_ENABLED)
        return E_INVALIDARG;
    EnterCriticalSection(&uia_cs);
    This->mode = mode;
    if (mode == UI_ANIMATION_MODE_DISABLED)
        manager_FinishAllStoryboards(iface, 0.0);
    LeaveCriticalSection(&uia_cs);
    return S_OK;
}

static HRESULT WINAPI manager_Pause(IUIAnimationManager *iface)
{
    struct manager *This = impl_from_IUIAnimationManager(iface);

    This->paused = TRUE;
    return S_OK;
}

static HRESULT WINAPI manager_Resume(IUIAnimationManager *iface)
{
    struct manager *This = impl_from_IUIAnimationManager(iface);

    This->paused = FALSE;
    return S_OK;
}

static HRESULT WINAPI manager_SetManagerEventHandler(IUIAnimationManager *iface, IUIAnimationManagerEventHandler *handler)
{
    struct manager *This = impl_from_IUIAnimationManager(iface);

    TRACE("(%p)->(%p)\n", iface, handler);

    EnterCriticalSection(&uia_cs);
    if (handler) IUIAnimationManagerEventHandler_AddRef(handler);
    if (This->handler) IUIAnimationManagerEventHandler_Release(This->handler);
    This->handler = handler;
    LeaveCriticalSection(&uia_cs);
    return S_OK;
}

static HRESULT manager_set_comparison(struct manager *This, unsigned int index, IUIAnimationPriorityComparison *comparison)
{
    /* Stored only: a new storyboard always wins over the scheduled ones */
    EnterCriticalSection(&uia_cs);
    if (comparison) IUIAnimationPriorityComparison_AddRef(comparison);
    if (This->comparisons[index]) IUIAnimationPriorityComparison_Release(This->comparisons[index]);
    This->comparisons[index] = comparison;
    LeaveCriticalSection(&uia_cs);
    return S_OK;
}

static HRESULT WINAPI manager_SetCancelPriorityComparison(IUIAnimationManager *iface, IUIAnimationPriorityComparison *comparison)
{
    return manager_set_comparison(impl_from_IUIAnimationManager(iface), 0, comparison);
}

static HRESULT WINAPI manager_SetTrimPriorityComparison(IUIAnimationManager *iface, IUIAnimationPriorityComparison *comparison)
{
    return manager_set_comparison(impl_from_IUIAnimationManager(iface), 1, comparison);
}

static HRESULT WINAPI manager_SetCompressPriorityComparison(IUIAnimationManager *iface, IUIAnimationPriorityComparison *comparison)
{
    return manager_set_comparison(impl_from_IUIAnimationManager(iface), 2, comparison);
}

static HRESULT WINAPI manager_SetConcludePriorityComparison(IUIAnimationManager *iface, IUIAnimationPriorityComparison *comparison)
{
    return manager_set_comparison(impl_from_IUIAnimationManager(iface), 3, comparison);
}

static HRESULT WINAPI manager_SetDefaultLongestAcceptableDelay(IUIAnimationManager *iface, UI_ANIMATION_SECONDS delay)
{
    TRACE("(%p)->(%f)\n", iface, delay);
    return S_OK;
}

static HRESULT WINAPI manager_Shutdown(IUIAnimationManager *iface)
{
    struct manager *This = impl_from_IUIAnimationManager(iface);

    TRACE("(%p)\n", iface);

    EnterCriticalSection(&uia_cs);
    manager_shutdown(This);
    LeaveCriticalSection(&uia_cs);
    return S_OK;
}

static const IUIAnimationManagerVtbl manager_vtbl =
{
    manager_QueryInterface,
    manager_AddRef,
    manager_Release,
    manager_CreateAnimationVariable,
    manager_ScheduleTransition,
    manager_CreateStoryboard,
    manager_FinishAllStoryboards,
    manager_AbandonAllStoryboards,
    manager_Update,
    manager_GetVariableFromTag,
    manager_GetStoryboardFromTag,
    manager_GetStatus,
    manager_SetAnimationMode,
    manager_Pause,
    manager_Resume,
    manager_SetManagerEventHandler,
    manager_SetCancelPriorityComparison,
    manager_SetTrimPriorityComparison,
    manager_SetCompressPriorityComparison,
    manager_SetConcludePriorityComparison,
    manager_SetDefaultLongestAcceptableDelay,
    manager_Shutdown,
};

/* The manager as the timer's update handler */

static HRESULT WINAPI manager_update_handler_QueryInterface(IUIAnimationTimerUpdateHandler *iface, REFIID iid, void **obj)
{
    struct manager *This = impl_from_IUIAnimationTimerUpdateHandler(iface);
    return manager_QueryInterface(&This->IUIAnimationManager_iface, iid, obj);
}

static ULONG WINAPI manager_update_handler_AddRef(IUIAnimationTimerUpdateHandler *iface)
{
    struct manager *This = impl_from_IUIAnimationTimerUpdateHandler(iface);
    return manager_AddRef(&This->IUIAnimationManager_iface);
}

static ULONG WINAPI manager_update_handler_Release(IUIAnimationTimerUpdateHandler *iface)
{
    struct manager *This = impl_from_IUIAnimationTimerUpdateHandler(iface);
    return manager_Release(&This->IUIAnimationManager_iface);
}

static HRESULT WINAPI manager_update_handler_OnUpdate(IUIAnimationTimerUpdateHandler *iface, UI_ANIMATION_SECONDS now,
                                                      UI_ANIMATION_UPDATE_RESULT *result)
{
    struct manager *This = impl_from_IUIAnimationTimerUpdateHandler(iface);
    return manager_Update(&This->IUIAnimationManager_iface, now, result);
}

static HRESULT WINAPI manager_update_handler_SetTimerClientEventHandler(IUIAnimationTimerUpdateHandler *iface,
                                                                        IUIAnimationTimerClientEventHandler *handler)
{
    struct manager *This = impl_from_IUIAnimationTimerUpdateHandler(iface);
    HRESULT hr = S_OK;

    EnterCriticalSection(&uia_cs);
    if (This->timer_client && handler)
        hr = UI_E_TIMER_CLIENT_ALREADY_CONNECTED;
    else
    {
        if (handler) IUIAnimationTimerClientEventHandler_AddRef(handler);
        if (This->timer_client) IUIAnimationTimerClientEventHandler_Release(This->timer_client);
        This->timer_client = handler;
    }
    LeaveCriticalSection(&uia_cs);
    return hr;
}

static HRESULT WINAPI manager_update_handler_ClearTimerClientEventHandler(IUIAnimationTimerUpdateHandler *iface)
{
    struct manager *This = impl_from_IUIAnimationTimerUpdateHandler(iface);

    EnterCriticalSection(&uia_cs);
    if (This->timer_client) IUIAnimationTimerClientEventHandler_Release(This->timer_client);
    This->timer_client = NULL;
    LeaveCriticalSection(&uia_cs);
    return S_OK;
}

static const IUIAnimationTimerUpdateHandlerVtbl manager_update_handler_vtbl =
{
    manager_update_handler_QueryInterface,
    manager_update_handler_AddRef,
    manager_update_handler_Release,
    manager_update_handler_OnUpdate,
    manager_update_handler_SetTimerClientEventHandler,
    manager_update_handler_ClearTimerClientEventHandler,
};

static HRESULT manager_create(IUnknown *outer, REFIID iid, void **obj)
{
    struct manager *This;
    HRESULT hr;

    if (outer) return CLASS_E_NOAGGREGATION;
    if (!(This = calloc(1, sizeof(*This))))
        return E_OUTOFMEMORY;
    This->IUIAnimationManager_iface.lpVtbl = &manager_vtbl;
    This->IUIAnimationTimerUpdateHandler_iface.lpVtbl = &manager_update_handler_vtbl;
    This->ref = 1;
    This->status = UI_ANIMATION_MANAGER_IDLE;
    This->mode = UI_ANIMATION_MODE_SYSTEM_DEFAULT;
    list_init(&This->variables);
    list_init(&This->storyboards);
    list_init(&This->scheduled);
    InterlockedIncrement(&object_count);

    hr = IUIAnimationManager_QueryInterface(&This->IUIAnimationManager_iface, iid, obj);
    IUIAnimationManager_Release(&This->IUIAnimationManager_iface);
    return hr;
}

/***********************************************************************
 *          IUIAnimationTimer
 */
struct timer
{
    IUIAnimationTimer IUIAnimationTimer_iface;
    IUIAnimationTimerClientEventHandler IUIAnimationTimerClientEventHandler_iface;
    LONG ref;
    struct list entry;          /* in timer_list while enabled */
    UINT_PTR timer_id;
    UINT fps;
    IUIAnimationTimerUpdateHandler *update_handler;
    IUIAnimationTimerEventHandler *event_handler;
};

static struct list timer_list = LIST_INIT(timer_list);

static inline struct timer *impl_from_IUIAnimationTimer(IUIAnimationTimer *iface)
{
    return CONTAINING_RECORD(iface, struct timer, IUIAnimationTimer_iface);
}

static inline struct timer *impl_from_IUIAnimationTimerClientEventHandler(IUIAnimationTimerClientEventHandler *iface)
{
    return CONTAINING_RECORD(iface, struct timer, IUIAnimationTimerClientEventHandler_iface);
}

static double timer_now(void)
{
    LARGE_INTEGER counter, frequency;

    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    return (double)counter.QuadPart / (double)frequency.QuadPart;
}

static void CALLBACK timer_proc(HWND hwnd, UINT msg, UINT_PTR id, DWORD time)
{
    UI_ANIMATION_UPDATE_RESULT result;
    struct timer *timer, *found = NULL;

    EnterCriticalSection(&uia_cs);
    LIST_FOR_EACH_ENTRY(timer, &timer_list, struct timer, entry)
    {
        if (timer->timer_id == id)
        {
            found = timer;
            IUIAnimationTimer_AddRef(&found->IUIAnimationTimer_iface);
            break;
        }
    }
    LeaveCriticalSection(&uia_cs);
    if (!found)
        return;

    if (found->event_handler)
        IUIAnimationTimerEventHandler_OnPreUpdate(found->event_handler);
    if (found->update_handler)
        IUIAnimationTimerUpdateHandler_OnUpdate(found->update_handler, timer_now(), &result);
    if (found->event_handler)
        IUIAnimationTimerEventHandler_OnPostUpdate(found->event_handler);
    IUIAnimationTimer_Release(&found->IUIAnimationTimer_iface);
}

static HRESULT WINAPI timer_QueryInterface(IUIAnimationTimer *iface, REFIID iid, void **obj)
{
    struct timer *This = impl_from_IUIAnimationTimer(iface);

    TRACE("(%p)->(%s %p)\n", iface, debugstr_guid(iid), obj);

    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IUIAnimationTimer))
        *obj = &This->IUIAnimationTimer_iface;
    else if (IsEqualIID(iid, &IID_IUIAnimationTimerClientEventHandler))
        *obj = &This->IUIAnimationTimerClientEventHandler_iface;
    else
    {
        *obj = NULL;
        return E_NOINTERFACE;
    }
    IUnknown_AddRef((IUnknown *)*obj);
    return S_OK;
}

static ULONG WINAPI timer_AddRef(IUIAnimationTimer *iface)
{
    struct timer *This = impl_from_IUIAnimationTimer(iface);
    return InterlockedIncrement(&This->ref);
}

static HRESULT WINAPI timer_Disable(IUIAnimationTimer *iface);

static ULONG WINAPI timer_Release(IUIAnimationTimer *iface)
{
    struct timer *This = impl_from_IUIAnimationTimer(iface);
    ULONG ref = InterlockedDecrement(&This->ref);

    if (!ref)
    {
        This->ref = 1;
        timer_Disable(iface);
        if (This->update_handler) IUIAnimationTimerUpdateHandler_Release(This->update_handler);
        if (This->event_handler) IUIAnimationTimerEventHandler_Release(This->event_handler);
        free(This);
        InterlockedDecrement(&object_count);
    }
    return ref;
}

static HRESULT WINAPI timer_SetTimerUpdateHandler(IUIAnimationTimer *iface, IUIAnimationTimerUpdateHandler *handler,
                                                  UI_ANIMATION_IDLE_BEHAVIOR idle_behavior)
{
    struct timer *This = impl_from_IUIAnimationTimer(iface);
    IUIAnimationTimerUpdateHandler *old;
    HRESULT hr = S_OK;

    TRACE("(%p)->(%p %d)\n", iface, handler, idle_behavior);

    EnterCriticalSection(&uia_cs);
    old = This->update_handler;
    if (old)
        IUIAnimationTimerUpdateHandler_ClearTimerClientEventHandler(old);
    if (handler)
    {
        IUIAnimationTimerUpdateHandler_AddRef(handler);
        /* With IDLE_BEHAVIOR_DISABLE the timer runs only while the client is busy.
         * The client keeps no reference to us: we clear it when going away. */
        if (idle_behavior == UI_ANIMATION_IDLE_BEHAVIOR_DISABLE)
            hr = IUIAnimationTimerUpdateHandler_SetTimerClientEventHandler(handler,
                    &This->IUIAnimationTimerClientEventHandler_iface);
    }
    This->update_handler = handler;
    LeaveCriticalSection(&uia_cs);
    if (old) IUIAnimationTimerUpdateHandler_Release(old);
    return hr;
}

static HRESULT WINAPI timer_SetTimerEventHandler(IUIAnimationTimer *iface, IUIAnimationTimerEventHandler *handler)
{
    struct timer *This = impl_from_IUIAnimationTimer(iface);

    EnterCriticalSection(&uia_cs);
    if (handler) IUIAnimationTimerEventHandler_AddRef(handler);
    if (This->event_handler) IUIAnimationTimerEventHandler_Release(This->event_handler);
    This->event_handler = handler;
    LeaveCriticalSection(&uia_cs);
    return S_OK;
}

static HRESULT WINAPI timer_Enable(IUIAnimationTimer *iface)
{
    struct timer *This = impl_from_IUIAnimationTimer(iface);
    HRESULT hr = S_OK;

    TRACE("(%p)\n", iface);

    EnterCriticalSection(&uia_cs);
    if (This->timer_id)
        hr = S_FALSE;
    else if (!(This->timer_id = SetTimer(NULL, 0, This->fps ? max(1, 1000 / This->fps) : 16, timer_proc)))
        hr = HRESULT_FROM_WIN32(GetLastError());
    else
        list_add_tail(&timer_list, &This->entry);
    LeaveCriticalSection(&uia_cs);
    return hr;
}

static HRESULT WINAPI timer_Disable(IUIAnimationTimer *iface)
{
    struct timer *This = impl_from_IUIAnimationTimer(iface);
    HRESULT hr = S_OK;

    TRACE("(%p)\n", iface);

    EnterCriticalSection(&uia_cs);
    if (!This->timer_id)
        hr = S_FALSE;
    else
    {
        KillTimer(NULL, This->timer_id);
        This->timer_id = 0;
        list_remove(&This->entry);
    }
    LeaveCriticalSection(&uia_cs);
    return hr;
}

static HRESULT WINAPI timer_IsEnabled(IUIAnimationTimer *iface)
{
    struct timer *This = impl_from_IUIAnimationTimer(iface);
    return This->timer_id ? S_OK : S_FALSE;
}

static HRESULT WINAPI timer_GetTime(IUIAnimationTimer *iface, UI_ANIMATION_SECONDS *seconds)
{
    if (!seconds)
        return E_POINTER;
    *seconds = timer_now();
    return S_OK;
}

static HRESULT WINAPI timer_SetFrameRateThreshold(IUIAnimationTimer *iface, UINT32 frames_per_sec)
{
    struct timer *This = impl_from_IUIAnimationTimer(iface);

    This->fps = frames_per_sec;
    return S_OK;
}

static const IUIAnimationTimerVtbl timer_vtbl =
{
    timer_QueryInterface,
    timer_AddRef,
    timer_Release,
    timer_SetTimerUpdateHandler,
    timer_SetTimerEventHandler,
    timer_Enable,
    timer_Disable,
    timer_IsEnabled,
    timer_GetTime,
    timer_SetFrameRateThreshold,
};

/* The timer as the update handler's client: runs only while it is busy */

static HRESULT WINAPI timer_client_QueryInterface(IUIAnimationTimerClientEventHandler *iface, REFIID iid, void **obj)
{
    struct timer *This = impl_from_IUIAnimationTimerClientEventHandler(iface);
    return timer_QueryInterface(&This->IUIAnimationTimer_iface, iid, obj);
}

/* Not counted: the update handler would otherwise keep the timer alive */
static ULONG WINAPI timer_client_AddRef(IUIAnimationTimerClientEventHandler *iface)
{
    return 2;
}

static ULONG WINAPI timer_client_Release(IUIAnimationTimerClientEventHandler *iface)
{
    return 1;
}

static HRESULT WINAPI timer_client_OnTimerClientStatusChanged(IUIAnimationTimerClientEventHandler *iface,
        UI_ANIMATION_TIMER_CLIENT_STATUS new_status, UI_ANIMATION_TIMER_CLIENT_STATUS previous_status)
{
    struct timer *This = impl_from_IUIAnimationTimerClientEventHandler(iface);

    if (new_status == UI_ANIMATION_TIMER_CLIENT_BUSY)
        timer_Enable(&This->IUIAnimationTimer_iface);
    else
        timer_Disable(&This->IUIAnimationTimer_iface);
    return S_OK;
}

static const IUIAnimationTimerClientEventHandlerVtbl timer_client_vtbl =
{
    timer_client_QueryInterface,
    timer_client_AddRef,
    timer_client_Release,
    timer_client_OnTimerClientStatusChanged,
};

static HRESULT timer_create(IUnknown *outer, REFIID iid, void **obj)
{
    struct timer *This;
    HRESULT hr;

    if (outer) return CLASS_E_NOAGGREGATION;
    if (!(This = calloc(1, sizeof(*This))))
        return E_OUTOFMEMORY;
    This->IUIAnimationTimer_iface.lpVtbl = &timer_vtbl;
    This->IUIAnimationTimerClientEventHandler_iface.lpVtbl = &timer_client_vtbl;
    This->ref = 1;
    InterlockedIncrement(&object_count);

    hr = IUIAnimationTimer_QueryInterface(&This->IUIAnimationTimer_iface, iid, obj);
    IUIAnimationTimer_Release(&This->IUIAnimationTimer_iface);
    return hr;
}

/***********************************************************************
 *          IUIAnimationTransitionFactory
 */
struct tr_factory
{
    IUIAnimationTransitionFactory IUIAnimationTransitionFactory_iface;
    LONG ref;
};

static inline struct tr_factory *impl_from_IUIAnimationTransitionFactory(IUIAnimationTransitionFactory *iface)
{
    return CONTAINING_RECORD(iface, struct tr_factory, IUIAnimationTransitionFactory_iface);
}

static HRESULT WINAPI tr_factory_QueryInterface(IUIAnimationTransitionFactory *iface, REFIID iid, void **obj)
{
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IUIAnimationTransitionFactory))
    {
        IUIAnimationTransitionFactory_AddRef(iface);
        *obj = iface;
        return S_OK;
    }
    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI tr_factory_AddRef(IUIAnimationTransitionFactory *iface)
{
    struct tr_factory *This = impl_from_IUIAnimationTransitionFactory(iface);
    return InterlockedIncrement(&This->ref);
}

static ULONG WINAPI tr_factory_Release(IUIAnimationTransitionFactory *iface)
{
    struct tr_factory *This = impl_from_IUIAnimationTransitionFactory(iface);
    ULONG ref = InterlockedDecrement(&This->ref);

    if (!ref)
    {
        free(This);
        InterlockedDecrement(&object_count);
    }
    return ref;
}

static HRESULT WINAPI tr_factory_CreateTransition(IUIAnimationTransitionFactory *iface,
        IUIAnimationInterpolator *interpolator, IUIAnimationTransition **transition)
{
    HRESULT hr;

    TRACE("(%p)->(%p %p)\n", iface, interpolator, transition);

    if (!interpolator)
        return E_INVALIDARG;
    if (SUCCEEDED(hr = transition_create(TRANSITION_INTERPOLATOR, 0.0, 0.0, TRUE, 0.0, 0.0, transition)))
    {
        IUIAnimationInterpolator_AddRef(interpolator);
        impl_from_IUIAnimationTransition(*transition)->interpolator = interpolator;
    }
    return hr;
}

static const IUIAnimationTransitionFactoryVtbl tr_factory_vtbl =
{
    tr_factory_QueryInterface,
    tr_factory_AddRef,
    tr_factory_Release,
    tr_factory_CreateTransition,
};

static HRESULT tr_factory_create(IUnknown *outer, REFIID iid, void **obj)
{
    struct tr_factory *This;
    HRESULT hr;

    if (outer) return CLASS_E_NOAGGREGATION;
    if (!(This = calloc(1, sizeof(*This))))
        return E_OUTOFMEMORY;
    This->IUIAnimationTransitionFactory_iface.lpVtbl = &tr_factory_vtbl;
    This->ref = 1;
    InterlockedIncrement(&object_count);

    hr = IUIAnimationTransitionFactory_QueryInterface(&This->IUIAnimationTransitionFactory_iface, iid, obj);
    IUIAnimationTransitionFactory_Release(&This->IUIAnimationTransitionFactory_iface);
    return hr;
}

/***********************************************************************
 *          IUIAnimationTransitionLibrary
 */
struct tr_library
{
    IUIAnimationTransitionLibrary IUIAnimationTransitionLibrary_iface;
    LONG ref;
};

static inline struct tr_library *impl_from_IUIAnimationTransitionLibrary(IUIAnimationTransitionLibrary *iface)
{
    return CONTAINING_RECORD(iface, struct tr_library, IUIAnimationTransitionLibrary_iface);
}

static HRESULT WINAPI tr_library_QueryInterface(IUIAnimationTransitionLibrary *iface, REFIID iid, void **obj)
{
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IUIAnimationTransitionLibrary))
    {
        IUIAnimationTransitionLibrary_AddRef(iface);
        *obj = iface;
        return S_OK;
    }
    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI tr_library_AddRef(IUIAnimationTransitionLibrary *iface)
{
    struct tr_library *This = impl_from_IUIAnimationTransitionLibrary(iface);
    return InterlockedIncrement(&This->ref);
}

static ULONG WINAPI tr_library_Release(IUIAnimationTransitionLibrary *iface)
{
    struct tr_library *This = impl_from_IUIAnimationTransitionLibrary(iface);
    ULONG ref = InterlockedDecrement(&This->ref);

    if (!ref)
    {
        free(This);
        InterlockedDecrement(&object_count);
    }
    return ref;
}

static HRESULT WINAPI tr_library_CreateInstantaneousTransition(IUIAnimationTransitionLibrary *iface,
        double finalValue, IUIAnimationTransition **transition)
{
    return transition_create(TRANSITION_INSTANTANEOUS, 0.0, finalValue, TRUE, 0.0, 0.0, transition);
}

static HRESULT WINAPI tr_library_CreateConstantTransition(IUIAnimationTransitionLibrary *iface,
        double duration, IUIAnimationTransition **transition)
{
    if (duration < 0.0) return E_INVALIDARG;
    return transition_create(TRANSITION_CONSTANT, duration, 0.0, FALSE, 0.0, 0.0, transition);
}

static HRESULT WINAPI tr_library_CreateDiscreteTransition(IUIAnimationTransitionLibrary *iface,
        double delay, double finalValue, double hold, IUIAnimationTransition **transition)
{
    if (delay < 0.0 || hold < 0.0) return E_INVALIDARG;
    return transition_create(TRANSITION_DISCRETE, delay, finalValue, TRUE, hold, 0.0, transition);
}

static HRESULT WINAPI tr_library_CreateLinearTransition(IUIAnimationTransitionLibrary *iface,
        double duration, double finalValue, IUIAnimationTransition **transition)
{
    if (duration < 0.0) return E_INVALIDARG;
    return transition_create(TRANSITION_LINEAR, duration, finalValue, TRUE, 0.0, 0.0, transition);
}

static HRESULT WINAPI tr_library_CreateLinearTransitionFromSpeed(IUIAnimationTransitionLibrary *iface,
        double speed, double finalValue, IUIAnimationTransition **transition)
{
    if (speed <= 0.0) return E_INVALIDARG;
    return transition_create(TRANSITION_LINEAR_FROM_SPEED, 0.0, finalValue, TRUE, speed, 0.0, transition);
}

static HRESULT WINAPI tr_library_CreateSinusoidalTransitionFromVelocity(IUIAnimationTransitionLibrary *iface,
        double duration, double period, IUIAnimationTransition **transition)
{
    if (duration < 0.0 || period <= 0.0) return E_INVALIDARG;
    return transition_create(TRANSITION_SINUSOIDAL, duration, 0.0, FALSE, period, 0.0, transition);
}

static HRESULT WINAPI tr_library_CreateSinusoidalTransitionFromRange(IUIAnimationTransitionLibrary *iface,
        double duration, double minValue, double maxValue, double period, UI_ANIMATION_SLOPE slope,
        IUIAnimationTransition **transition)
{
    if (duration < 0.0 || period <= 0.0) return E_INVALIDARG;
    return transition_create(TRANSITION_SINUSOIDAL, duration, 0.0, FALSE, period, 0.0, transition);
}

static HRESULT WINAPI tr_library_CreateAccelerateDecelerateTransition(IUIAnimationTransitionLibrary *iface,
        double duration, double finalValue, double accelerationRatio, double decelerationRatio,
        IUIAnimationTransition **transition)
{
    if (duration < 0.0 || accelerationRatio < 0.0 || decelerationRatio < 0.0 ||
        accelerationRatio + decelerationRatio > 1.0)
        return E_INVALIDARG;
    return transition_create(TRANSITION_ACCEL_DECEL, duration, finalValue, TRUE,
                             accelerationRatio, decelerationRatio, transition);
}

static HRESULT WINAPI tr_library_CreateReversalTransition(IUIAnimationTransitionLibrary *iface,
        double duration, IUIAnimationTransition **transition)
{
    if (duration < 0.0) return E_INVALIDARG;
    return transition_create(TRANSITION_REVERSAL, duration, 0.0, FALSE, 0.0, 0.0, transition);
}

static HRESULT WINAPI tr_library_CreateCubicTransition(IUIAnimationTransitionLibrary *iface,
        double duration, double finalValue, double finalVelocity, IUIAnimationTransition **transition)
{
    if (duration < 0.0) return E_INVALIDARG;
    return transition_create(TRANSITION_CUBIC, duration, finalValue, TRUE, 0.0, finalVelocity, transition);
}

static HRESULT WINAPI tr_library_CreateSmoothStopTransition(IUIAnimationTransitionLibrary *iface,
        double maximumDuration, double finalValue, IUIAnimationTransition **transition)
{
    if (maximumDuration < 0.0) return E_INVALIDARG;
    return transition_create(TRANSITION_SMOOTH_STOP, maximumDuration, finalValue, TRUE, 0.0, 0.0, transition);
}

static HRESULT WINAPI tr_library_CreateParabolicTransitionFromAcceleration(IUIAnimationTransitionLibrary *iface,
        double finalValue, double finalVelocity, double acceleration, IUIAnimationTransition **transition)
{
    return transition_create(TRANSITION_PARABOLIC, 0.0, finalValue, TRUE, acceleration, finalVelocity, transition);
}

static const IUIAnimationTransitionLibraryVtbl tr_library_vtbl =
{
    tr_library_QueryInterface,
    tr_library_AddRef,
    tr_library_Release,
    tr_library_CreateInstantaneousTransition,
    tr_library_CreateConstantTransition,
    tr_library_CreateDiscreteTransition,
    tr_library_CreateLinearTransition,
    tr_library_CreateLinearTransitionFromSpeed,
    tr_library_CreateSinusoidalTransitionFromVelocity,
    tr_library_CreateSinusoidalTransitionFromRange,
    tr_library_CreateAccelerateDecelerateTransition,
    tr_library_CreateReversalTransition,
    tr_library_CreateCubicTransition,
    tr_library_CreateSmoothStopTransition,
    tr_library_CreateParabolicTransitionFromAcceleration,
};

static HRESULT tr_library_create(IUnknown *outer, REFIID iid, void **obj)
{
    struct tr_library *This;
    HRESULT hr;

    if (outer) return CLASS_E_NOAGGREGATION;
    if (!(This = calloc(1, sizeof(*This))))
        return E_OUTOFMEMORY;
    This->IUIAnimationTransitionLibrary_iface.lpVtbl = &tr_library_vtbl;
    This->ref = 1;
    InterlockedIncrement(&object_count);

    hr = IUIAnimationTransitionLibrary_QueryInterface(&This->IUIAnimationTransitionLibrary_iface, iid, obj);
    IUIAnimationTransitionLibrary_Release(&This->IUIAnimationTransitionLibrary_iface);
    return hr;
}

/***********************************************************************
 *          Class factories and registration
 */
struct class_factory
{
    IClassFactory IClassFactory_iface;
    const CLSID *clsid;
    const WCHAR *name;
    HRESULT (*create)(IUnknown *outer, REFIID iid, void **obj);
};

static HRESULT WINAPI class_factory_QueryInterface(IClassFactory *iface, REFIID iid, void **obj)
{
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IClassFactory))
    {
        IClassFactory_AddRef(iface);
        *obj = iface;
        return S_OK;
    }
    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI class_factory_AddRef(IClassFactory *iface)
{
    return 2;
}

static ULONG WINAPI class_factory_Release(IClassFactory *iface)
{
    return 1;
}

static HRESULT WINAPI class_factory_CreateInstance(IClassFactory *iface, IUnknown *outer, REFIID iid, void **obj)
{
    struct class_factory *This = CONTAINING_RECORD(iface, struct class_factory, IClassFactory_iface);

    TRACE("(%p)->(%p %s %p)\n", iface, outer, debugstr_guid(iid), obj);

    if (!obj)
        return E_POINTER;
    *obj = NULL;
    return This->create(outer, iid, obj);
}

static HRESULT WINAPI class_factory_LockServer(IClassFactory *iface, BOOL lock)
{
    if (lock) InterlockedIncrement(&object_count);
    else InterlockedDecrement(&object_count);
    return S_OK;
}

static const IClassFactoryVtbl class_factory_vtbl =
{
    class_factory_QueryInterface,
    class_factory_AddRef,
    class_factory_Release,
    class_factory_CreateInstance,
    class_factory_LockServer,
};

static struct class_factory class_factories[] =
{
    { { &class_factory_vtbl }, &CLSID_UIAnimationManager, L"UIAnimationManager", manager_create },
    { { &class_factory_vtbl }, &CLSID_UIAnimationTimer, L"UIAnimationTimer", timer_create },
    { { &class_factory_vtbl }, &CLSID_UIAnimationTransitionFactory, L"UIAnimationTransitionFactory", tr_factory_create },
    { { &class_factory_vtbl }, &CLSID_UIAnimationTransitionLibrary, L"UIAnimationTransitionLibrary", tr_library_create },
};

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        uianimation_instance = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void **obj)
{
    unsigned int i;

    TRACE("(%s %s %p)\n", debugstr_guid(clsid), debugstr_guid(iid), obj);

    for (i = 0; i < ARRAY_SIZE(class_factories); i++)
    {
        if (IsEqualCLSID(clsid, class_factories[i].clsid))
            return IClassFactory_QueryInterface(&class_factories[i].IClassFactory_iface, iid, obj);
    }
    *obj = NULL;
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllCanUnloadNow(void)
{
    return object_count ? S_FALSE : S_OK;
}

static void clsid_key_name(const CLSID *clsid, WCHAR *name, DWORD size)
{
    WCHAR guid[40];

    StringFromGUID2(clsid, guid, ARRAY_SIZE(guid));
    lstrcpynW(name, L"CLSID\\", size);
    lstrcatW(name, guid);
}

HRESULT WINAPI DllRegisterServer(void)
{
    static const WCHAR path[] = L"%SystemRoot%\\system32\\uianimation.dll";
    WCHAR name[MAX_PATH];
    unsigned int i;
    HKEY key, server;

    for (i = 0; i < ARRAY_SIZE(class_factories); i++)
    {
        clsid_key_name(class_factories[i].clsid, name, ARRAY_SIZE(name));
        if (RegCreateKeyExW(HKEY_CLASSES_ROOT, name, 0, NULL, 0, KEY_WRITE, NULL, &key, NULL))
            return SELFREG_E_CLASS;
        RegSetValueExW(key, NULL, 0, REG_SZ, (const BYTE *)class_factories[i].name,
                       (lstrlenW(class_factories[i].name) + 1) * sizeof(WCHAR));
        if (RegCreateKeyExW(key, L"InprocServer32", 0, NULL, 0, KEY_WRITE, NULL, &server, NULL))
        {
            RegCloseKey(key);
            return SELFREG_E_CLASS;
        }
        RegSetValueExW(server, NULL, 0, REG_EXPAND_SZ, (const BYTE *)path, sizeof(path));
        RegSetValueExW(server, L"ThreadingModel", 0, REG_SZ, (const BYTE *)L"Both", sizeof(L"Both"));
        RegCloseKey(server);
        RegCloseKey(key);
    }
    return S_OK;
}

HRESULT WINAPI DllUnregisterServer(void)
{
    WCHAR name[MAX_PATH];
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(class_factories); i++)
    {
        clsid_key_name(class_factories[i].clsid, name, ARRAY_SIZE(name));
        lstrcatW(name, L"\\InprocServer32");
        RegDeleteKeyW(HKEY_CLASSES_ROOT, name);
        clsid_key_name(class_factories[i].clsid, name, ARRAY_SIZE(name));
        RegDeleteKeyW(HKEY_CLASSES_ROOT, name);
    }
    return S_OK;
}
