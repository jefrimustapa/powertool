/* Property-based test for svcstate.h: fixed-seed random generator (no C PBT framework available).
 * Build: gcc -Wall -I.. test_svcstate_prop.c -o test_svcstate_prop.exe
 * Seed and iteration count are printed so a failure can be replayed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../svcstate.h"

static unsigned long long rs = 20261112ULL;     /* fixed seed */
static unsigned rnd(void) { rs = rs * 6364136223846793005ULL + 1442695040888963407ULL; return (unsigned)(rs >> 33); }
static int fails;
#define PROP(c, ...) do { if (!(c)) { if (fails < 10) { printf("PROP FAIL line %d: %s | ", __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } fails++; } } while (0)
#define N 100000

int main(void)
{
    static const wchar_t alpha[] = L"0123456789,- +abc\t.";
    int i;
    printf("seed=20261112 iterations=%d\n", N);
    for (i = 0; i < N; i++) {
        SvcState a, b, c; SvcPlan p; wchar_t buf[64], junk[40]; unsigned long ct; int cr, n, k;

        /* P1: parse(format(s)) == s for every valid s */
        a.type = rnd() % 5; a.delayed = rnd() % 2; a.running = rnd() % 2;
        svcstate_format(buf, 64, &a);
        PROP(svcstate_parse(buf, &b) && b.type == a.type && b.delayed == a.delayed && b.running == a.running,
             "s=%lu,%d,%d buf=%ls", a.type, a.delayed, a.running, buf);

        /* P2: plan invariants for any valid record and any current state */
        a.running = (int)(rnd() % 3) - 1;        /* -1 unknown, 0, 1 */
        ct = rnd() % 5; cr = (int)(rnd() % 2);
        p = svcstate_plan(&a, ct, cr);
        PROP(!(p.start && a.type == SVC_T_DISABLED), "start on Disabled orig");
        PROP(!(p.start && cr), "start while already running");
        PROP(!(p.start && a.running != 1), "start although not known running before");
        PROP(!(p.set_type && a.type == ct), "set_type although equal");
        PROP(!(p.set_type && a.type == SVC_T_DISABLED), "set_type to Disabled");
        PROP(!(p.set_delayed && a.type != SVC_T_AUTO), "delayed on non-auto");
        PROP(p.start == (a.running == 1 && !cr && a.type != SVC_T_DISABLED), "start formula");

        /* P3: parse never crashes on random junk, and accepts only fully valid records */
        n = (int)(rnd() % 39);
        for (k = 0; k < n; k++) junk[k] = alpha[rnd() % (sizeof alpha / sizeof alpha[0] - 1)];
        junk[n] = 0;
        if (svcstate_parse(junk, &c))
            PROP(c.type <= 4 && (c.delayed == 0 || c.delayed == 1) && c.running >= -1 && c.running <= 1,
                 "accepted junk '%ls' -> %lu,%d,%d", junk, c.type, c.delayed, c.running);

        /* P4: is_running / fallback_type totality on arbitrary 32-bit input */
        ct = ((unsigned long)rnd() << 16) ^ rnd();
        PROP(svcstate_is_running(ct) == (ct >= 4 && ct <= 7 ? 1 : (ct == 2)), "is_running(%lu)", ct);
        PROP((ct == 4 ? svcstate_fallback_type(ct) == 3 : svcstate_fallback_type(ct) == ct), "fallback(%lu)", ct);
    }
    printf(fails ? "%d property violations\n" : "all properties held\n", fails);
    return fails != 0;
}
