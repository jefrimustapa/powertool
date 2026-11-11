/*
 * svcstate.h - pure logic for remembering and restoring service state (no Win32 calls,
 * so it can be unit-tested with test_svcstate.c).
 *
 * Record format in settings.ini [DisabledServices]:  name=<startType>,<delayedAuto>,<wasRunning>
 *   startType  : 0..4 (SERVICE_BOOT_START .. SERVICE_DISABLED) before governance
 *   delayedAuto: 0/1
 *   wasRunning : 0/1, or absent in old 2-field records (unknown -> never start)
 */
#ifndef SVCSTATE_H
#define SVCSTATE_H
#include <wchar.h>

#define SVC_T_DISABLED 4UL
#define SVC_T_AUTO     2UL
#define SVC_T_MANUAL   3UL

/* SCM dwCurrentState codes (same values as winsvc.h, kept here so this header stays Win32-free) */
#define SVC_S_STOPPED          1UL
#define SVC_S_START_PENDING    2UL
#define SVC_S_STOP_PENDING     3UL
#define SVC_S_RUNNING          4UL
#define SVC_S_CONTINUE_PENDING 5UL
#define SVC_S_PAUSE_PENDING    6UL
#define SVC_S_PAUSED           7UL

typedef struct { unsigned long type; int delayed; int running; /* -1 unknown */ } SvcState;
typedef struct { int set_type; int set_delayed; int start; } SvcPlan;

static void svcstate_format(wchar_t *out, size_t n, const SvcState *s)
{
    swprintf(out, n, L"%lu,%d,%d", s->type, s->delayed ? 1 : 0, s->running > 0 ? 1 : 0);
}

/* Returns 1 if the record is valid, 0 if empty/corrupt. */
static int svcstate_parse(const wchar_t *v, SvcState *s)
{
    unsigned long t; int d = 0, r = -1, n;
    if (!v || !*v) return 0;
    n = swscanf(v, L"%lu,%d,%d", &t, &d, &r);
    if (n < 1 || t > SVC_T_DISABLED || d < 0 || d > 1 || (n >= 3 && (r < 0 || r > 1))) return 0;
    s->type = t; s->delayed = d; s->running = (n >= 3) ? r : -1;
    return 1;
}

/* What to do on disable, given the service's current start type / running state. */
static SvcPlan svcstate_plan(const SvcState *orig, unsigned long curType, int curRunning)
{
    SvcPlan p = {0, 0, 0};
    p.set_type = orig->type != SVC_T_DISABLED && orig->type != curType;
    p.set_delayed = orig->type == SVC_T_AUTO && orig->delayed;
    /* start only if it was running before, is stopped now, and is not meant to stay disabled */
    p.start = orig->running == 1 && !curRunning && orig->type != SVC_T_DISABLED;
    return p;
}

/*
 * Is the service "running" for restore purposes? RUNNING, START_PENDING (it is coming up; a
 * StartService on it only returns ERROR_SERVICE_ALREADY_RUNNING), CONTINUE_PENDING,
 * PAUSE_PENDING and PAUSED count as running. STOPPED and STOP_PENDING (going down) do not.
 * Unknown codes count as not running.
 */
static int svcstate_is_running(unsigned long state)
{
    return state == SVC_S_RUNNING || state == SVC_S_START_PENDING ||
           state == SVC_S_CONTINUE_PENDING || state == SVC_S_PAUSE_PENDING ||
           state == SVC_S_PAUSED;
}

/*
 * Start type to use when the saved record is unreadable. A service left Disabled would lock the
 * user out, so Disabled becomes Manual (restorable, not auto-started, not started now).
 * Any other start type is returned unchanged.
 */
static unsigned long svcstate_fallback_type(unsigned long curType)
{
    return curType == SVC_T_DISABLED ? SVC_T_MANUAL : curType;
}
#endif
