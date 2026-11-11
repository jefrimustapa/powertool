#!/bin/sh
# Manual mutation testing for svcstate.h (no C mutation tool available).
# Each mutant = one sed edit of a copy of svcstate.h; the unit test AND the property test
# are rebuilt against it and at least one must FAIL (mutant killed). Survivors are reported.
cd "$(dirname "$0")/.." || exit 1
W=./.mut-tmp; mkdir -p "$W"
mkdir -p "$W/tests"
killed=0; alive=0; n=0
run_mutant() {  # $1 = description, $2 = sed expression
    n=$((n+1))
    sed "$2" svcstate.h > "$W/svcstate.h"
    if cmp -s svcstate.h "$W/svcstate.h"; then echo "SKIP (no change): $1"; n=$((n-1)); return; fi
    cp test_svcstate.c "$W/"; cp tests/test_svcstate_prop.c "$W/tests/"
    cp "$W/svcstate.h" "$W/tests/svcstate.h"
    gcc -w -I"$W" "$W/test_svcstate.c" -o "$W/u.exe" 2>/dev/null || { echo "BUILD-FAIL (counts as killed): $1"; killed=$((killed+1)); return; }
    gcc -w -I"$W" "$W/tests/test_svcstate_prop.c" -o "$W/p.exe" 2>/dev/null
    if "$W/u.exe" >/dev/null 2>&1 && "$W/p.exe" >/dev/null 2>&1; then
        echo "ALIVE  : $1"; alive=$((alive+1))
    else echo "killed : $1"; killed=$((killed+1)); fi
}
# the property test includes ../svcstate.h, so place the mutant where "../svcstate.h" resolves
run_mutant "is_running: drop START_PENDING"        's/state == SVC_S_RUNNING || state == SVC_S_START_PENDING ||/state == SVC_S_RUNNING ||/'
run_mutant "is_running: drop CONTINUE_PENDING"     's/state == SVC_S_CONTINUE_PENDING || state == SVC_S_PAUSE_PENDING ||/state == SVC_S_PAUSE_PENDING ||/'
run_mutant "is_running: drop PAUSE_PENDING"        's/state == SVC_S_CONTINUE_PENDING || state == SVC_S_PAUSE_PENDING ||/state == SVC_S_CONTINUE_PENDING ||/'
run_mutant "is_running: drop PAUSED"               's/state == SVC_S_PAUSE_PENDING ||$/state == SVC_S_PAUSE_PENDING;/;/state == SVC_S_PAUSED;/d'
run_mutant "is_running: STOP_PENDING counted"      's/state == SVC_S_RUNNING ||/state == SVC_S_STOP_PENDING || state == SVC_S_RUNNING ||/'
run_mutant "is_running: STOPPED counted"           's/state == SVC_S_RUNNING ||/state == SVC_S_STOPPED || state == SVC_S_RUNNING ||/'
run_mutant "is_running: invert"                    's/return state == SVC_S_RUNNING/return state != SVC_S_RUNNING \&\& state == SVC_S_RUNNING/'
run_mutant "fallback: Disabled -> Auto"            's/? SVC_T_MANUAL : curType/? SVC_T_AUTO : curType/'
run_mutant "fallback: always Manual"               's/return curType == SVC_T_DISABLED ? SVC_T_MANUAL : curType;/return SVC_T_MANUAL;/'
run_mutant "fallback: never change"                's/return curType == SVC_T_DISABLED ? SVC_T_MANUAL : curType;/return curType;/'
run_mutant "fallback: == to !="                    's/curType == SVC_T_DISABLED ?/curType != SVC_T_DISABLED ?/'
run_mutant "plan: start ignores curRunning"        's/ \&\& !curRunning//'
run_mutant "plan: start ignores Disabled orig"     's/ \&\& orig->type != SVC_T_DISABLED;/;/'
run_mutant "plan: running == 1 -> >= 0"            's/orig->running == 1/orig->running >= 0/'
run_mutant "plan: set_type always on"              's/p.set_type = orig->type != SVC_T_DISABLED \&\& orig->type != curType;/p.set_type = orig->type != SVC_T_DISABLED;/'
run_mutant "plan: set_type to Disabled allowed"    's/orig->type != SVC_T_DISABLED \&\& orig->type != curType/orig->type != curType/'
run_mutant "plan: delayed ignores type"            's/orig->type == SVC_T_AUTO \&\& orig->delayed/orig->delayed/'
run_mutant "parse: t > 4 -> t > 5"                 's/t > SVC_T_DISABLED/t > 5UL/'
run_mutant "parse: t > 4 -> t >= 4"                's/t > SVC_T_DISABLED/t >= SVC_T_DISABLED/'
run_mutant "parse: d > 1 -> d > 2"                 's/d > 1/d > 2/'
run_mutant "parse: d < 0 removed"                  's/ || d < 0//'
run_mutant "parse: r > 1 -> r > 2"                 's/r > 1/r > 2/'
run_mutant "parse: r < 0 removed"                  's/(r < 0 || r > 1)/(r > 1)/'
run_mutant "parse: n < 1 -> n < 0"                 's/n < 1/n < 0/'
run_mutant "parse: n >= 3 -> n >= 2"               's/n >= 3 \&\&/n >= 2 \&\&/'
run_mutant "parse: unknown running -> 0"           's/(n >= 3) ? r : -1/(n >= 3) ? r : 0/'
run_mutant "format: running>0 -> running>=0"       's/s->running > 0/s->running >= 0/'
run_mutant "format: delayed dropped"               's/s->delayed ? 1 : 0/0/'
rm -rf "$W"
echo "mutants: $n  killed: $killed  alive: $alive"
[ "$alive" -eq 0 ]
