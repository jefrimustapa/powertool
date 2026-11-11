/*
 * test_svc_win32.c - REAL Win32 SCM test of PowerTool's service restore logic, using only a
 * throw-away dummy service (name PTGovDummy_<pid>). It #includes powertool.c so the REAL
 * stop_blacklisted_services() / services_restore() run. Needs an ELEVATED shell.
 * Isolation: g_ini = temp ini, blacklist = the dummy exe only (match is by exe path), so no other
 * service and not %APPDATA%\PowerTool\settings.ini is touched. Cato is never touched.
 * Build + run: see run_win32_test.ps1.
 */
#define wWinMain pt_wWinMain_unused     /* keep powertool.c's entry point out of the way */
#include "../powertool.c"
#undef wWinMain
#include <stdio.h>

#define PREFIX L"PTGovDummy_"
static int fails, checks;
static wchar_t g_name[64], g_exe[MAX_PATH];
static SC_HANDLE g_scm;
#define CHECK(c) do { checks++; if (!(c)) { printf("  FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static SC_HANDLE open_svc(DWORD acc) { return OpenServiceW(g_scm, g_name, acc); }

static DWORD get_state(void)
{
    SERVICE_STATUS ss; DWORD r = 0; SC_HANDLE s = open_svc(SERVICE_QUERY_STATUS);
    if (s) { if (QueryServiceStatus(s, &ss)) r = ss.dwCurrentState; CloseServiceHandle(s); }
    return r;                                   /* 0 = service does not exist */
}
static DWORD get_type(int *delayed)
{
    DWORD sz = 0, r = 99; QUERY_SERVICE_CONFIGW *c; SC_HANDLE s = open_svc(SERVICE_QUERY_CONFIG);
    if (!s) return r;
    QueryServiceConfigW(s, NULL, 0, &sz); c = malloc(sz);
    if (c && QueryServiceConfigW(s, c, sz, &sz)) r = c->dwStartType;
    free(c);
    if (delayed) {
        SERVICE_DELAYED_AUTO_START_INFO d = {0}; DWORD n;
        *delayed = QueryServiceConfig2W(s, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, (LPBYTE)&d, sizeof d, &n) && d.fDelayedAutostart;
    }
    CloseServiceHandle(s);
    return r;
}
static int wait_state(DWORD want, int secs)
{
    for (int i = 0; i < secs * 10; i++) { if (get_state() == want) return 1; Sleep(100); }
    return get_state() == want;
}
static void kill_svc(const wchar_t *name)
{
    SC_HANDLE s = OpenServiceW(g_scm, name, SERVICE_STOP | DELETE | SERVICE_CHANGE_CONFIG | SERVICE_QUERY_STATUS);
    SERVICE_STATUS ss;
    if (!s) return;
    ChangeServiceConfigW(s, SERVICE_NO_CHANGE, SERVICE_DEMAND_START, SERVICE_NO_CHANGE, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    ControlService(s, SERVICE_CONTROL_STOP, &ss);
    for (int i = 0; i < 100 && QueryServiceStatus(s, &ss) && ss.dwCurrentState != SERVICE_STOPPED; i++) Sleep(100);
    DeleteService(s); CloseServiceHandle(s);
}
static void sweep_old(void)                     /* leftovers of crashed earlier runs */
{
    DWORD need = 0, cnt = 0, res = 0; BYTE *b;
    EnumServicesStatusExW(g_scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL, NULL, 0, &need, &cnt, &res, NULL);
    if (!need || !(b = malloc(need))) return;
    if (EnumServicesStatusExW(g_scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL, b, need, &need, &cnt, &res, NULL))
        for (DWORD i = 0; i < cnt; i++) {
            ENUM_SERVICE_STATUS_PROCESSW *e = (ENUM_SERVICE_STATUS_PROCESSW *)b + i;
            if (_wcsnicmp(e->lpServiceName, PREFIX, wcslen(PREFIX)) == 0) kill_svc(e->lpServiceName);
        }
    free(b);
}

/* Create the dummy with a start type (+delayed), optionally start it. */
static void setup(DWORD type, int delayed, int run, int slow)
{
    wchar_t cmd[MAX_PATH + 16]; SC_HANDLE s;
    swprintf(cmd, MAX_PATH + 16, L"\"%ls\"%ls", g_exe, slow ? L" slow" : L"");
    kill_svc(g_name);
    DeleteFileW(g_ini);
    s = CreateServiceW(g_scm, g_name, g_name, SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
                       type == 4 ? SERVICE_DEMAND_START : type, SERVICE_ERROR_NORMAL, cmd,
                       NULL, NULL, NULL, NULL, NULL);
    if (!s) { printf("  cannot create dummy service (error %lu) - is this shell elevated?\n", GetLastError()); exit(2); }
    if (type == 4) ChangeServiceConfigW(s, SERVICE_NO_CHANGE, SERVICE_DISABLED, SERVICE_NO_CHANGE, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    if (delayed) { SERVICE_DELAYED_AUTO_START_INFO d = { TRUE }; ChangeServiceConfig2W(s, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, &d); }
    if (run) { StartServiceW(s, 0, NULL); if (!slow) wait_state(SERVICE_RUNNING, 10); }
    CloseServiceHandle(s);
    wcscpy(g_items[0], g_exe); g_itemCount = 1;
    g_restoreNote[0] = 0;
}
static void govern(void)
{
    stop_blacklisted_services();
    wait_state(SERVICE_STOPPED, 10);
}
static void rec(wchar_t *out) { GetPrivateProfileStringW(DISABLED_SECTION, g_name, L"", out, 32, g_ini); }

static void case_basic(const wchar_t *label, DWORD type, int delayed, int run)
{
    int d = -1; wchar_t r[32];
    printf("%ls\n", label);
    setup(type, delayed, run, 0);
    govern();
    if (type != 4) CHECK(get_type(NULL) == SERVICE_DISABLED);
    CHECK(get_state() == SERVICE_STOPPED);
    services_restore(TRUE);
    CHECK(get_type(&d) == type);
    if (type == 2) CHECK(d == delayed);
    if (run && type != 4) CHECK(wait_state(SERVICE_RUNNING, 10));
    else { Sleep(500); CHECK(get_state() == SERVICE_STOPPED); }
    rec(r); CHECK(r[0] == 0);
}

int wmain(void)
{
    wchar_t dir[MAX_PATH], tmp[MAX_PATH], r[32];
    GetTempPathW(MAX_PATH, tmp);
    swprintf(dir, MAX_PATH, L"%lsptgov-%lu", tmp, GetCurrentProcessId());
    CreateDirectoryW(dir, NULL);
    swprintf(g_exe, MAX_PATH, L"%ls\\dummysvc.exe", dir);
    swprintf(g_name, 64, PREFIX L"%lu", GetCurrentProcessId());
    swprintf(g_ini, MAX_PATH, L"%ls\\test-settings.ini", dir);
    if (_wcsnicmp(g_name, PREFIX, wcslen(PREFIX)) != 0) return 3;       /* safety guard */
    {
        wchar_t src[MAX_PATH], *sl;
        GetModuleFileNameW(NULL, src, MAX_PATH);
        sl = wcsrchr(src, L'\\'); if (sl) wcscpy(sl + 1, L"dummysvc.exe");
        if (!CopyFileW(src, g_exe, FALSE)) { printf("dummysvc.exe not found next to the test exe\n"); return 3; }
    }
    g_scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!g_scm) { printf("cannot open SCM (error %lu) - run elevated\n", GetLastError()); return 2; }
    sweep_old();
    printf("dummy=%ls exe=%ls\n", g_name, g_exe);

    case_basic(L"1 Automatic + running -> Automatic + running", 2, 0, 1);
    case_basic(L"2 Manual + stopped -> Manual, stays stopped", 3, 0, 0);
    case_basic(L"3 Automatic(delayed) + running -> delayed flag restored", 2, 1, 1);
    case_basic(L"4 Originally Disabled -> stays Disabled, not started", 4, 0, 0);

    printf("5 service deleted while governed -> record dropped, note, no crash\n");
    setup(2, 0, 1, 0); govern(); kill_svc(g_name); Sleep(500);
    services_restore(TRUE);
    rec(r); CHECK(r[0] == 0); CHECK(wcsstr(g_restoreNote, L"service missing") != NULL);

    printf("6 enable/disable x3 -> original record preserved, same end result as case 1\n");
    setup(2, 0, 1, 0);
    for (int i = 0; i < 3; i++) {
        govern(); rec(r); CHECK(wcscmp(r, L"2,0,1") == 0);
    }
    services_restore(TRUE);
    CHECK(get_type(NULL) == 2); CHECK(wait_state(SERVICE_RUNNING, 10));

    printf("7 record missing -> restore changes nothing, no crash\n");
    setup(2, 0, 1, 0); govern();
    WritePrivateProfileStringW(DISABLED_SECTION, g_name, NULL, g_ini);
    services_restore(TRUE);
    CHECK(get_type(NULL) == SERVICE_DISABLED); CHECK(get_state() == SERVICE_STOPPED);

    printf("8 corrupt record on a Disabled service -> Manual, not running, record dropped\n");
    {
        const wchar_t *bad[] = { L"abc", L"9,0,1", L"2,5,1", L"2,0,7" };
        for (int i = 0; i < 4; i++) {
            setup(2, 0, 1, 0); govern();
            WritePrivateProfileStringW(DISABLED_SECTION, g_name, bad[i], g_ini);
            services_restore(TRUE);
            CHECK(get_type(NULL) == SERVICE_DEMAND_START); CHECK(get_state() == SERVICE_STOPPED);
            rec(r); CHECK(r[0] == 0); CHECK(wcsstr(g_restoreNote, L"set to Manual") != NULL);
        }
    }
    printf("8b corrupt record on a non-Disabled service -> left unchanged\n");
    setup(3, 0, 0, 0);
    WritePrivateProfileStringW(DISABLED_SECTION, g_name, L"abc", g_ini);
    services_restore(TRUE);
    CHECK(get_type(NULL) == SERVICE_DEMAND_START); CHECK(wcsstr(g_restoreNote, L"left unchanged") != NULL);

    printf("9 START_PENDING at governance time counts as running -> restored running\n");
    setup(2, 0, 1, 1);
    printf(get_state() == SERVICE_START_PENDING ? "  (observed START_PENDING)\n"
                                                 : "  (START_PENDING window missed; covered by unit test)\n");
    stop_blacklisted_services(); rec(r);
    CHECK(wcscmp(r, L"2,0,1") == 0);
    wait_state(SERVICE_STOPPED, 15);
    services_restore(TRUE);
    CHECK(wait_state(SERVICE_RUNNING, 15) || get_state() == SERVICE_START_PENDING);

    kill_svc(g_name);
    CHECK(get_state() == 0);
    DeleteFileW(g_exe); DeleteFileW(g_ini); RemoveDirectoryW(dir);
    CloseServiceHandle(g_scm);
    if (fails) printf("\n%d of %d checks FAILED\n", fails, checks);
    else printf("\nall %d checks passed\n", checks);
    return fails != 0;
}
