/* Unit test for svcstate.h (mocked service layer). Build: gcc -Wall test_svcstate.c -o test_svcstate.exe */
#include <stdio.h>
#include "svcstate.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

int main(void)
{
    SvcState s; SvcPlan p; wchar_t buf[32];

    /* running + Automatic(delayed) before; governed = Disabled + stopped -> set type, delayed, start */
    s.type = 2; s.delayed = 1; s.running = 1;
    svcstate_format(buf, 32, &s); CHECK(wcscmp(buf, L"2,1,1") == 0);
    CHECK(svcstate_parse(buf, &s) && s.type == 2 && s.delayed == 1 && s.running == 1);
    p = svcstate_plan(&s, 4, 0); CHECK(p.set_type && p.set_delayed && p.start);

    /* stopped before (Manual): restore type, do NOT start */
    s.type = 3; s.delayed = 0; s.running = 0;
    p = svcstate_plan(&s, 4, 0); CHECK(p.set_type && !p.set_delayed && !p.start);

    /* was already Disabled before governance: nothing to do, never start */
    s.type = 4; s.running = 1;
    p = svcstate_plan(&s, 4, 0); CHECK(!p.set_type && !p.start);

    /* already running now: do not start again */
    s.type = 2; s.delayed = 0; s.running = 1;
    p = svcstate_plan(&s, 2, 1); CHECK(!p.set_type && !p.start);

    /* old 2-field record: running unknown -> never start (old behaviour) */
    CHECK(svcstate_parse(L"2,0", &s) && s.running == -1);
    p = svcstate_plan(&s, 4, 0); CHECK(p.set_type && !p.start);

    /* corrupt / missing records rejected */
    CHECK(!svcstate_parse(L"", &s));
    CHECK(!svcstate_parse(L"garbage", &s));
    CHECK(!svcstate_parse(L"9,0,1", &s));
    CHECK(!svcstate_parse(L"2,5,1", &s));
    CHECK(!svcstate_parse(L"2,0,7", &s));
    CHECK(!svcstate_parse(L"2,0,2", &s));   /* BVA: just above max */
    CHECK(!svcstate_parse(L"2,0,-1", &s));  /* BVA: just below min */
    CHECK(!svcstate_parse(L"2,-1,1", &s));  /* delayed just below min */
    CHECK(!svcstate_parse(NULL, &s));

    /* svcstate_is_running: every SCM code 0..8 (BVA on both sides of the valid range) */
    {
        static const int exp[9] = {0, 0, 1, 0, 1, 1, 1, 1, 0}; /* 0 unknown,1 STOPPED,2 START_PEND,
            3 STOP_PEND,4 RUNNING,5 CONT_PEND,6 PAUSE_PEND,7 PAUSED,8 unknown */
        unsigned long c;
        for (c = 0; c <= 8; c++) CHECK(svcstate_is_running(c) == exp[c]);
        CHECK(svcstate_is_running(0xFFFFFFFFUL) == 0);
    }

    /* svcstate_fallback_type: Disabled -> Manual, all others unchanged */
    {
        unsigned long t;
        CHECK(svcstate_fallback_type(4) == 3);
        for (t = 0; t <= 3; t++) CHECK(svcstate_fallback_type(t) == t);
        CHECK(svcstate_fallback_type(5) == 5);
    }

    printf(fails ? "%d FAILED\n" : "all tests passed\n", fails);
    return fails != 0;
}
