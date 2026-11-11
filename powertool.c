/*
 * powertool.c - Tray/GUI app that periodically ends processes whose executable
 * path is on a blacklist.
 *
 * Build: see build.bat
 */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commdlg.h>
#include <powrprof.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include "svcstate.h"

#define APP_NAME          L"PowerTool"
#define MAX_ITEMS         64
#define DEFAULT_INTERVAL  10
#define MAX_INTERVAL      86400
#define DEFAULT_AWAKE_SEC 60

#define WM_TRAY (WM_APP + 2)

enum {
    ID_AUTOSTART = 100, ID_ENABLE, ID_INTERVAL, ID_LIST, ID_ADD, ID_REMOVE, ID_SAVE, ID_STATUS,
    ID_TRAY_SHOW = 200, ID_TRAY_EXIT,
    ID_TIMER = 300, ID_TIMER_AWAKE,
    ID_PAGE0 = 400, ID_PAGE1, ID_AWAKE, ID_AWAKE_SIM, ID_AWAKE_INT, ID_AWAKE_LID
};

#define PANEL_CLASS APP_NAME L"Panel"

static HWND  g_hwnd, g_chkAuto, g_chkEnable, g_edInterval, g_list, g_status;
static HWND  g_btnPage[2], g_pg[2];     /* page selector buttons and the page panels */
static int   g_page;
static HWND  g_chkAwake, g_chkSim, g_lblAwakeInt, g_edAwakeInt, g_lblAwakeHelp;
static HWND  g_lblInterval, g_lblItems, g_btnAdd, g_btnRemove, g_btnSave;
static HWND  g_chkLid;
static HPOWERNOTIFY g_lidNotify;
static BOOL  g_awakeOn, g_awakeSim, g_lidSleep;
static int   g_awakeSec = DEFAULT_AWAKE_SEC;
static wchar_t g_ini[MAX_PATH];
static wchar_t g_exe[MAX_PATH];

static wchar_t g_items[MAX_ITEMS][MAX_PATH];    /* blacklist; UI thread only */
static int     g_itemCount;
static int     g_interval = DEFAULT_INTERVAL;
static BOOL    g_enabled;
static unsigned g_killed;

/* ---------- config ---------- */

static void config_init_path(void)
{
    wchar_t dir[MAX_PATH];
    SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, dir);
    wcscat(dir, L"\\" APP_NAME);
    CreateDirectoryW(dir, NULL);
    swprintf(g_ini, MAX_PATH, L"%ls\\settings.ini", dir);
}

static void config_load(void)
{
    wchar_t key[32];
    int v = GetPrivateProfileIntW(L"Settings", L"IntervalSec", DEFAULT_INTERVAL, g_ini);
    g_interval = (v >= 1 && v <= MAX_INTERVAL) ? v : DEFAULT_INTERVAL;
    g_enabled = GetPrivateProfileIntW(L"Settings", L"Enabled", 0, g_ini) != 0;

    v = GetPrivateProfileIntW(L"KeepAwake", L"IntervalSec", DEFAULT_AWAKE_SEC, g_ini);
    g_awakeSec = (v >= 1 && v <= MAX_INTERVAL) ? v : DEFAULT_AWAKE_SEC;
    g_awakeOn = GetPrivateProfileIntW(L"KeepAwake", L"Enabled", 0, g_ini) != 0;
    g_awakeSim = GetPrivateProfileIntW(L"KeepAwake", L"SimulateInput", 0, g_ini) != 0;
    g_lidSleep = GetPrivateProfileIntW(L"KeepAwake", L"SleepOnLidClose", 0, g_ini) != 0;

    g_itemCount = 0;
    for (int i = 0; i < MAX_ITEMS; i++) {
        swprintf(key, 32, L"Path%d", i);
        GetPrivateProfileStringW(L"Blacklist", key, L"", g_items[g_itemCount], MAX_PATH, g_ini);
        if (g_items[g_itemCount][0]) g_itemCount++;
    }
}

static void config_save(void)
{
    wchar_t key[32], buf[16];
    swprintf(buf, 16, L"%d", g_interval);
    WritePrivateProfileStringW(L"Settings", L"IntervalSec", buf, g_ini);
    WritePrivateProfileStringW(L"Settings", L"Enabled", g_enabled ? L"1" : L"0", g_ini);
    swprintf(buf, 16, L"%d", g_awakeSec);
    WritePrivateProfileStringW(L"KeepAwake", L"IntervalSec", buf, g_ini);
    WritePrivateProfileStringW(L"KeepAwake", L"Enabled", g_awakeOn ? L"1" : L"0", g_ini);
    WritePrivateProfileStringW(L"KeepAwake", L"SimulateInput", g_awakeSim ? L"1" : L"0", g_ini);
    WritePrivateProfileStringW(L"KeepAwake", L"SleepOnLidClose", g_lidSleep ? L"1" : L"0", g_ini);
    WritePrivateProfileStringW(L"Blacklist", NULL, NULL, g_ini);   /* clear section */
    for (int i = 0; i < g_itemCount; i++) {
        swprintf(key, 32, L"Path%d", i);
        WritePrivateProfileStringW(L"Blacklist", key, g_items[i], g_ini);
    }
}

/* ---------- start with Windows ---------- */

/*
 * The app requires elevation, and Windows does not launch elevated programs from the
 * HKCU Run key, so autostart is a logon task that runs with highest privileges.
 */

#define RUN_KEY   L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define TASK_NAME APP_NAME

/* Run schtasks.exe hidden and return its exit code (-1 if it could not start). */
static int run_schtasks(const wchar_t *args)
{
    wchar_t cmd[2 * MAX_PATH + 128];
    STARTUPINFOW si = {0};
    PROCESS_INFORMATION pi;
    DWORD code = (DWORD)-1;

    swprintf(cmd, 2 * MAX_PATH + 128, L"schtasks.exe %ls", args);
    si.cb = sizeof si;
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
        return -1;
    WaitForSingleObject(pi.hProcess, 15000);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)code;
}

static BOOL autostart_get(void)
{
    return run_schtasks(L"/Query /TN \"" TASK_NAME L"\"") == 0;
}

static void autostart_set(BOOL on)
{
    HKEY k;

    /* drop the old Run-key entry from earlier versions */
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_WRITE, &k) == ERROR_SUCCESS) {
        RegDeleteValueW(k, APP_NAME);
        RegCloseKey(k);
    }

    if (!on) {
        run_schtasks(L"/Delete /TN \"" TASK_NAME L"\" /F");
        return;
    }

    wchar_t tmp[MAX_PATH], xmlPath[MAX_PATH], user[256] = L"", dom[256] = L"", args[2 * MAX_PATH];
    GetEnvironmentVariableW(L"USERNAME", user, 256);
    GetEnvironmentVariableW(L"USERDOMAIN", dom, 256);
    GetTempPathW(MAX_PATH, tmp);
    swprintf(xmlPath, MAX_PATH, L"%ls%ls-task.xml", tmp, APP_NAME);

    FILE *f = _wfopen(xmlPath, L"wb");
    if (!f) return;
    fputwc(0xFEFF, f);                              /* UTF-16 LE BOM */
    fwprintf(f,
        L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\n"
        L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\n"
        L"<Triggers><LogonTrigger><Enabled>true</Enabled><UserId>%ls\\%ls</UserId></LogonTrigger></Triggers>\n"
        L"<Principals><Principal id=\"Author\"><UserId>%ls\\%ls</UserId>"
        L"<LogonType>InteractiveToken</LogonType><RunLevel>HighestAvailable</RunLevel></Principal></Principals>\n"
        L"<Settings><MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>"
        L"<DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>"
        L"<StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>"
        L"<ExecutionTimeLimit>PT0S</ExecutionTimeLimit><Enabled>true</Enabled></Settings>\n"
        L"<Actions Context=\"Author\"><Exec><Command>%ls</Command></Exec></Actions>\n"
        L"</Task>\n", dom, user, dom, user, g_exe);
    fclose(f);

    swprintf(args, 2 * MAX_PATH, L"/Create /TN \"" TASK_NAME L"\" /XML \"%ls\" /F", xmlPath);
    run_schtasks(args);
    DeleteFileW(xmlPath);
}

/* ---------- scanning ---------- */

static void set_status(const wchar_t *text) { SetWindowTextW(g_status, text); }

static void normalize(const wchar_t *in, wchar_t *out)
{
    if (!GetFullPathNameW(in, MAX_PATH, out, NULL)) wcscpy(out, in);
}

static BOOL is_blacklisted(const wchar_t *path)
{
    for (int i = 0; i < g_itemCount; i++) {
        wchar_t full[MAX_PATH];
        normalize(g_items[i], full);
        if (_wcsicmp(full, path) == 0) return TRUE;
    }
    return FALSE;
}

/* Extract the executable path from a service ImagePath (quoted, or unquoted with args). */
static void service_exe_path(const wchar_t *image, wchar_t *out)
{
    wchar_t raw[MAX_PATH * 2], exp[MAX_PATH * 2];
    const wchar_t *p = image;

    while (*p == L' ') p++;
    if (wcsncmp(p, L"\\??\\", 4) == 0) p += 4;
    if (*p == L'"') {
        const wchar_t *q = wcschr(++p, L'"');
        size_t n = q ? (size_t)(q - p) : wcslen(p);
        if (n >= MAX_PATH * 2) n = MAX_PATH * 2 - 1;
        wcsncpy(raw, p, n);
        raw[n] = 0;
    } else {
        wchar_t *e;
        wcsncpy(raw, p, MAX_PATH * 2 - 1);
        raw[MAX_PATH * 2 - 1] = 0;
        for (e = raw; *e; e++)
            if (_wcsnicmp(e, L".exe", 4) == 0) { e[4] = 0; break; }
    }
    if (!ExpandEnvironmentStringsW(raw, exp, MAX_PATH * 2)) wcscpy(exp, raw);
    exp[MAX_PATH - 1] = 0;
    normalize(exp, out);
}

/*
 * The state a service had BEFORE governance first touched it is remembered in settings.ini
 * ([DisabledServices], name=<startType>,<delayedAuto>,<wasRunning>; see svcstate.h) so it can
 * be put back on disable. A record is written once and never overwritten while it exists, so
 * repeated enable/disable cannot replace the original state with the governed one.
 */
#define DISABLED_SECTION L"DisabledServices"

static wchar_t g_restoreNote[160];      /* last restore problem, shown in the status line */

/* Save the original state unless a record already exists. Returns TRUE if a record exists. */
static BOOL service_record(SC_HANDLE s, const wchar_t *name, DWORD startType, BOOL running)
{
    SERVICE_DELAYED_AUTO_START_INFO d = {0};
    DWORD n = 0;
    wchar_t old[32], val[32];
    SvcState st;

    GetPrivateProfileStringW(DISABLED_SECTION, name, L"", old, 32, g_ini);
    if (old[0] && svcstate_parse(old, &st)) return TRUE;    /* keep the original */
    QueryServiceConfig2W(s, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, (LPBYTE)&d, sizeof d, &n);
    st.type = startType; st.delayed = d.fDelayedAutostart ? 1 : 0; st.running = running ? 1 : 0;
    svcstate_format(val, 32, &st);
    return WritePrivateProfileStringW(DISABLED_SECTION, name, val, g_ini) != 0;
}

/*
 * Put back the state of services PowerTool governed: start type (incl. delayed-auto) and, if
 * the service was running before, start it again. A service that was stopped stays stopped.
 * With all=FALSE only those whose executable is no longer blacklisted are restored.
 * Missing service or corrupt record: record dropped (a corrupt record on a Disabled service sets
 * it to Manual), noted in g_restoreNote, which scan_and_kill() / timer_apply() display.
 */
static void services_restore(BOOL all)
{
    DWORD cap = 32768;
    wchar_t *names = malloc(cap * sizeof(wchar_t));
    SC_HANDLE scm;

    if (!names) return;
    GetPrivateProfileStringW(DISABLED_SECTION, NULL, L"", names, cap, g_ini);
    if (!names[0] || !(scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT))) {
        free(names);
        return;
    }
    for (const wchar_t *nm = names; *nm; nm += wcslen(nm) + 1) {
        wchar_t val[32];
        SvcState st;
        SC_HANDLE s = OpenServiceW(scm, nm, SERVICE_QUERY_CONFIG | SERVICE_CHANGE_CONFIG |
                                            SERVICE_QUERY_STATUS | SERVICE_START);
        DWORD sz = 0;
        QUERY_SERVICE_CONFIGW *cfg = NULL;
        SERVICE_STATUS ss;
        SvcPlan plan;
        BOOL ok = TRUE;

        if (!s) {
            if (GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST) {
                WritePrivateProfileStringW(DISABLED_SECTION, nm, NULL, g_ini);
                swprintf(g_restoreNote, 160, L"%ls: service missing, record dropped", nm);
            }
            continue;
        }
        QueryServiceConfigW(s, NULL, 0, &sz);
        cfg = sz ? malloc(sz) : NULL;
        if (!cfg || !QueryServiceConfigW(s, cfg, sz, &sz)) { free(cfg); CloseServiceHandle(s); continue; }
        if (!all) {
            wchar_t path[MAX_PATH];
            service_exe_path(cfg->lpBinaryPathName, path);
            if (is_blacklisted(path)) { free(cfg); CloseServiceHandle(s); continue; }
        }
        GetPrivateProfileStringW(DISABLED_SECTION, nm, L"", val, 32, g_ini);
        if (!svcstate_parse(val, &st)) {
            /* corrupt record: drop it; a Disabled service becomes Manual so it is not locked out */
            unsigned long ft = svcstate_fallback_type(cfg->dwStartType);
            WritePrivateProfileStringW(DISABLED_SECTION, nm, NULL, g_ini);
            if (ft != cfg->dwStartType &&
                ChangeServiceConfigW(s, SERVICE_NO_CHANGE, (DWORD)ft, SERVICE_NO_CHANGE,
                                     NULL, NULL, NULL, NULL, NULL, NULL, NULL))
                swprintf(g_restoreNote, 160, L"%ls: saved state unreadable, set to Manual", nm);
            else if (ft != cfg->dwStartType)
                swprintf(g_restoreNote, 160, L"%ls: saved state unreadable, could not set Manual (error %lu)",
                         nm, GetLastError());
            else
                swprintf(g_restoreNote, 160, L"%ls: saved state unreadable, left unchanged", nm);
            free(cfg); CloseServiceHandle(s);
            continue;
        }
        plan = svcstate_plan(&st, cfg->dwStartType,
                             QueryServiceStatus(s, &ss) && svcstate_is_running(ss.dwCurrentState));
        if (plan.set_type)
            ok = ChangeServiceConfigW(s, SERVICE_NO_CHANGE, (DWORD)st.type, SERVICE_NO_CHANGE,
                                      NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        if (ok && plan.set_delayed) {
            SERVICE_DELAYED_AUTO_START_INFO d = { TRUE };
            ChangeServiceConfig2W(s, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, &d);
        }
        if (ok) {
            if (plan.start && !StartServiceW(s, 0, NULL))
                swprintf(g_restoreNote, 160, L"%ls: restored but could not start (error %lu)",
                         nm, GetLastError());
            WritePrivateProfileStringW(DISABLED_SECTION, nm, NULL, g_ini);
        }
        free(cfg);
        CloseServiceHandle(s);
    }
    CloseServiceHandle(scm);
    free(names);
}


/* Disable and stop services whose executable is blacklisted. Needs admin rights to succeed. */
static unsigned stop_blacklisted_services(void)
{
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT | SC_MANAGER_ENUMERATE_SERVICE);
    BYTE *buf = NULL;
    DWORD need = 0, count = 0, resume = 0;
    unsigned stopped = 0;

    if (!scm) return 0;
    EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL,
                          NULL, 0, &need, &count, &resume, NULL);
    if (need && (buf = malloc(need)) &&
        EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL,
                              buf, need, &need, &count, &resume, NULL)) {
        ENUM_SERVICE_STATUS_PROCESSW *svc = (ENUM_SERVICE_STATUS_PROCESSW *)buf;
        for (DWORD i = 0; i < count; i++) {
            SC_HANDLE s = OpenServiceW(scm, svc[i].lpServiceName,
                                       SERVICE_QUERY_CONFIG | SERVICE_CHANGE_CONFIG | SERVICE_STOP);
            DWORD sz = 0;
            if (!s) continue;
            QueryServiceConfigW(s, NULL, 0, &sz);
            QUERY_SERVICE_CONFIGW *cfg = sz ? malloc(sz) : NULL;
            if (cfg && QueryServiceConfigW(s, cfg, sz, &sz) && cfg->lpBinaryPathName) {
                wchar_t path[MAX_PATH];
                SERVICE_STATUS ss;
                service_exe_path(cfg->lpBinaryPathName, path);
                if (is_blacklisted(path)) {
                    /* disable first so it cannot restart (recovery action or next boot) */
                    wchar_t had[32];
                    BOOL fresh;
                    GetPrivateProfileStringW(DISABLED_SECTION, svc[i].lpServiceName, L"", had, 32, g_ini);
                    fresh = !had[0];
                    /* remember the pre-governance state once (never overwritten), then disable */
                    service_record(s, svc[i].lpServiceName, cfg->dwStartType,
                                   svcstate_is_running(svc[i].ServiceStatusProcess.dwCurrentState));
                    if (cfg->dwStartType != SERVICE_DISABLED &&
                        !ChangeServiceConfigW(s, SERVICE_NO_CHANGE, SERVICE_DISABLED, SERVICE_NO_CHANGE,
                                              NULL, NULL, NULL, NULL, NULL, NULL, NULL) && fresh)
                        WritePrivateProfileStringW(DISABLED_SECTION, svc[i].lpServiceName, NULL, g_ini);
                    if (svc[i].ServiceStatusProcess.dwCurrentState != SERVICE_STOPPED &&
                        ControlService(s, SERVICE_CONTROL_STOP, &ss))
                        stopped++;
                }
            }
            free(cfg);
            CloseServiceHandle(s);
        }
    }
    free(buf);
    CloseServiceHandle(scm);
    return stopped;
}

static void scan_and_kill(void)
{
    HANDLE snap;
    PROCESSENTRY32W pe = {0};
    DWORD self = GetCurrentProcessId();
    unsigned killed = stop_blacklisted_services();
    SYSTEMTIME st;
    wchar_t t[320];

    snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    pe.dwSize = sizeof pe;
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        if (pe.th32ProcessID == self || pe.th32ProcessID == 0) continue;
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE,
                               FALSE, pe.th32ProcessID);
        if (!h) continue;                       /* system or elevated: skip */
        wchar_t path[MAX_PATH];
        DWORD n = MAX_PATH;
        if (QueryFullProcessImageNameW(h, 0, path, &n) && is_blacklisted(path) &&
            TerminateProcess(h, 1))
            killed++;
        CloseHandle(h);
    }
    CloseHandle(snap);

    g_killed += killed;
    GetLocalTime(&st);
    swprintf(t, 320, L"Last scan %02d:%02d:%02d - killed %u total",
             st.wHour, st.wMinute, st.wSecond, g_killed);
    if (g_restoreNote[0]) {                     /* show a restore problem in the same tick */
        size_t l = wcslen(t);
        swprintf(t + l, 320 - l, L" | %ls", g_restoreNote);
        g_restoreNote[0] = 0;
    }
    set_status(t);
}

/* (Re)arm the timer from the current settings and scan once. */
static void timer_apply(void)
{
    KillTimer(g_hwnd, ID_TIMER);
    if (g_enabled && g_itemCount > 0) {
        SetTimer(g_hwnd, ID_TIMER, (UINT)g_interval * 1000, NULL);
        services_restore(FALSE);                /* entries removed from the blacklist */
        scan_and_kill();
    } else {
        services_restore(TRUE);                 /* ending is off: re-enable what we disabled */
        if (g_restoreNote[0]) {
            set_status(g_restoreNote);          /* report restore problems clearly */
            g_restoreNote[0] = 0;
        } else
            set_status(g_enabled ? L"Enabled, but the blacklist is empty" : L"Disabled");
    }
}

/* ---------- keep awake ---------- */

/* A harmless F15 tap; resets the real input-idle counter (Teams status, lock timers). */
static void send_fake_input(void)
{
    INPUT in[2] = {0};
    in[0].type = in[1].type = INPUT_KEYBOARD;
    in[0].ki.wVk = in[1].ki.wVk = VK_F15;
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
}

/* Execution state is per-thread; it is always set from the UI thread. */
static void awake_apply(void)
{
    KillTimer(g_hwnd, ID_TIMER_AWAKE);
    if (g_awakeOn) {
        SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
        if (g_awakeSim) {
            SetTimer(g_hwnd, ID_TIMER_AWAKE, (UINT)g_awakeSec * 1000, NULL);
            send_fake_input();
        }
    } else {
        SetThreadExecutionState(ES_CONTINUOUS);
    }
}

/* ---------- sleep on lid close ---------- */

/* GUID_LIDSWITCH_STATE_CHANGE; Data[0] is 0 when the lid is closed, 1 when open. */
static const GUID k_lidGuid =
    {0xBA3E0F4D, 0xB817, 0x4094, {0xA2, 0xD1, 0xD5, 0x63, 0x79, 0xE6, 0xA0, 0xF3}};

static volatile LONG g_sleeping;

/* SetSuspendState blocks until resume, so it runs off the UI thread. */
static DWORD WINAPI sleep_thread(LPVOID arg)
{
    HANDLE tok;
    (void)arg;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok)) {
        TOKEN_PRIVILEGES tp = {0};
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        if (LookupPrivilegeValueW(NULL, SE_SHUTDOWN_NAME, &tp.Privileges[0].Luid))
            AdjustTokenPrivileges(tok, FALSE, &tp, 0, NULL, NULL);
        CloseHandle(tok);
    }
    SetSuspendState(FALSE, FALSE, FALSE);           /* sleep, not hibernate */
    InterlockedExchange(&g_sleeping, 0);
    return 0;
}

static void lid_closed(void)
{
    HANDLE h;
    if (!g_lidSleep || InterlockedCompareExchange(&g_sleeping, 1, 0) != 0) return;
    h = CreateThread(NULL, 0, sleep_thread, NULL, 0, NULL);
    if (h) CloseHandle(h);
    else InterlockedExchange(&g_sleeping, 0);
}

/* ---------- GUI ---------- */

/* Show the selected page panel, hide the other, and press its selector button. */
static void page_select(int page)
{
    g_page = page;
    for (int i = 0; i < 2; i++) {
        ShowWindow(g_pg[i], i == page ? SW_SHOW : SW_HIDE);
        SendMessageW(g_btnPage[i], BM_SETCHECK, i == page ? BST_CHECKED : BST_UNCHECKED, 0);
    }
}

/* Page panels are plain child windows: they own their controls, forward commands
 * to the main window and paint the dialog background themselves. */
static LRESULT CALLBACK panel_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_COMMAND:
        return SendMessageW(GetParent(hwnd), msg, wp, lp);
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        SetBkColor((HDC)wp, GetSysColor(COLOR_BTNFACE));
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* Let long paths scroll horizontally instead of being cut off. */
static void list_extent(void)
{
    HDC dc = GetDC(g_list);
    HGDIOBJ old = SelectObject(dc, (HFONT)SendMessageW(g_list, WM_GETFONT, 0, 0));
    int w = 0, n = (int)SendMessageW(g_list, LB_GETCOUNT, 0, 0);
    for (int i = 0; i < n; i++) {
        wchar_t s[MAX_PATH];
        SIZE sz;
        if (SendMessageW(g_list, LB_GETTEXTLEN, i, 0) >= MAX_PATH) continue;
        SendMessageW(g_list, LB_GETTEXT, i, (LPARAM)s);
        if (GetTextExtentPoint32W(dc, s, (int)wcslen(s), &sz) && sz.cx > w) w = sz.cx;
    }
    SelectObject(dc, old);
    ReleaseDC(g_list, dc);
    SendMessageW(g_list, LB_SETHORIZONTALEXTENT, w + 16, 0);
}

static void list_fill(void)
{
    SendMessageW(g_list, LB_RESETCONTENT, 0, 0);
    for (int i = 0; i < g_itemCount; i++)
        SendMessageW(g_list, LB_ADDSTRING, 0, (LPARAM)g_items[i]);
    list_extent();
}

static NOTIFYICONDATAW g_nid;

/* Tray tooltip summarises what is currently active. */
static void tray_tip(void)
{
    wchar_t k[48], a[24];
    if (g_enabled && g_itemCount > 0) swprintf(k, 48, L"on, every %d s", g_interval);
    else                              wcscpy(k, L"off");
    if (g_awakeOn) wcscpy(a, g_awakeSim ? L"on + input" : L"on");
    else           wcscpy(a, L"off");
    swprintf(g_nid.szTip, 128, L"PowerTool\nApp Govern: %ls\nKeep awake: %ls", k, a);
    g_nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static BOOL is_protected(const wchar_t *file)
{
    wchar_t sys[MAX_PATH], full[MAX_PATH];
    size_t n;
    normalize(file, full);
    if (_wcsicmp(full, g_exe) == 0) return TRUE;
    GetSystemDirectoryW(sys, MAX_PATH);
    n = wcslen(sys);
    return _wcsnicmp(full, sys, n) == 0 && full[n] == L'\\';
}

static void on_add(HWND hwnd)
{
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"Applications (*.exe)\0*.exe\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Add blacklisted application";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn)) return;

    if (is_protected(file)) {
        MessageBoxW(hwnd, L"PowerTool itself and files in the Windows System32 folder cannot be blacklisted.",
                    APP_NAME, MB_OK | MB_ICONWARNING);
        return;
    }
    if (SendMessageW(g_list, LB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)file) != LB_ERR) return;
    if (SendMessageW(g_list, LB_GETCOUNT, 0, 0) >= MAX_ITEMS) {
        MessageBoxW(hwnd, L"Blacklist is full.", APP_NAME, MB_OK | MB_ICONWARNING);
        return;
    }
    SendMessageW(g_list, LB_ADDSTRING, 0, (LPARAM)file);
    list_extent();
}

static void on_remove(void)
{
    LRESULT sel = SendMessageW(g_list, LB_GETCURSEL, 0, 0);
    if (sel != LB_ERR) SendMessageW(g_list, LB_DELETESTRING, sel, 0);
}

static void on_save(HWND hwnd)
{
    wchar_t buf[16];
    GetWindowTextW(g_edInterval, buf, 16);
    int sec = _wtoi(buf);
    if (sec < 1 || sec > MAX_INTERVAL) {
        MessageBoxW(hwnd, L"Interval must be between 1 and 86400 seconds.", APP_NAME,
                    MB_OK | MB_ICONWARNING);
        return;
    }

    LRESULT cnt = SendMessageW(g_list, LB_GETCOUNT, 0, 0);
    g_itemCount = 0;
    for (int i = 0; i < cnt && i < MAX_ITEMS; i++) {
        if (SendMessageW(g_list, LB_GETTEXTLEN, i, 0) < MAX_PATH)
            SendMessageW(g_list, LB_GETTEXT, i, (LPARAM)g_items[g_itemCount++]);
    }

    GetWindowTextW(g_edAwakeInt, buf, 16);
    int asec = _wtoi(buf);
    if (asec < 1 || asec > MAX_INTERVAL) {
        MessageBoxW(hwnd, L"Keep awake interval must be between 1 and 86400 seconds.", APP_NAME,
                    MB_OK | MB_ICONWARNING);
        return;
    }

    g_interval = sec;
    g_enabled = SendMessageW(g_chkEnable, BM_GETCHECK, 0, 0) == BST_CHECKED;
    g_awakeSec = asec;
    g_awakeOn = SendMessageW(g_chkAwake, BM_GETCHECK, 0, 0) == BST_CHECKED;
    g_awakeSim = SendMessageW(g_chkSim, BM_GETCHECK, 0, 0) == BST_CHECKED;
    g_lidSleep = SendMessageW(g_chkLid, BM_GETCHECK, 0, 0) == BST_CHECKED;
    config_save();
    autostart_set(SendMessageW(g_chkAuto, BM_GETCHECK, 0, 0) == BST_CHECKED);
    timer_apply();
    awake_apply();
    tray_tip();
}

/* ---------- DPI-aware layout ---------- */

static HFONT g_font;
static UINT  g_dpi = 96;

#define S(x) MulDiv((x), (int)g_dpi, 96)

static FARPROC user32_proc(const char *name)
{
    return GetProcAddress(GetModuleHandleW(L"user32.dll"), name);
}

static void dpi_enable(void)
{
    typedef BOOL (WINAPI *PFN)(HANDLE);
    PFN f = (PFN)user32_proc("SetProcessDpiAwarenessContext");
    if (f && f((HANDLE)-4))                 /* PER_MONITOR_AWARE_V2 */
        return;
    typedef BOOL (WINAPI *PFN_OLD)(void);
    PFN_OLD o = (PFN_OLD)user32_proc("SetProcessDPIAware");
    if (o) o();
}

static UINT dpi_of(HWND hwnd)
{
    typedef UINT (WINAPI *PFN)(HWND);
    PFN f = (PFN)user32_proc("GetDpiForWindow");
    UINT d = (f && hwnd) ? f(hwnd) : 0;
    if (!d) {
        HDC dc = GetDC(NULL);
        d = (UINT)GetDeviceCaps(dc, LOGPIXELSX);
        ReleaseDC(NULL, dc);
    }
    return d ? d : 96;
}

static void adjust_rect(RECT *rc, DWORD style)
{
    typedef BOOL (WINAPI *PFN)(LPRECT, DWORD, BOOL, DWORD, UINT);
    PFN f = (PFN)user32_proc("AdjustWindowRectExForDpi");
    if (f) f(rc, style, FALSE, 0, g_dpi);
    else   AdjustWindowRectEx(rc, style, FALSE, 0);
}

static BOOL CALLBACK set_font_cb(HWND child, LPARAM font)
{
    SendMessageW(child, WM_SETFONT, (WPARAM)font, TRUE);
    return TRUE;
}

/* (Re)build the UI font for the current DPI and apply it to all controls. */
static void font_rebuild(HWND hwnd)
{
    typedef BOOL (WINAPI *PFN)(UINT, UINT, PVOID, UINT, UINT);
    NONCLIENTMETRICSW ncm = {0};
    PFN f = (PFN)user32_proc("SystemParametersInfoForDpi");
    LOGFONTW lf = {0};
    ncm.cbSize = sizeof ncm;
    if (f && f(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0, g_dpi)) {
        lf = ncm.lfMessageFont;
    } else {
        lf.lfHeight = -MulDiv(9, (int)g_dpi, 72);
        wcscpy(lf.lfFaceName, L"Segoe UI");
    }
    HFONT nf = CreateFontIndirectW(&lf);
    if (!nf) return;
    EnumChildWindows(hwnd, set_font_cb, (LPARAM)nf);
    if (g_font) DeleteObject(g_font);
    g_font = nf;
}

/* Position every control from the current client size and DPI. */
static void layout(HWND hwnd)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    int cw = rc.right, ch = rc.bottom;
    int m = S(12), row = S(24), edH = S(26), btnW = S(88), btnH = S(28), gap = S(8);

    int y = m;
    MoveWindow(g_chkAuto, m, y, cw - 2 * m, row, TRUE);
    y += row + gap;

    /* page selector buttons */
    int selW = S(130);
    MoveWindow(g_btnPage[0], m, y, selW, btnH, TRUE);
    MoveWindow(g_btnPage[1], m + selW, y, selW, btnH, TRUE);
    y += btnH;

    /* bottom row: status + Save; the page panels take what is left */
    int by = ch - m - btnH;
    MoveWindow(g_btnSave, cw - m - btnW, by, btnW, btnH, TRUE);
    MoveWindow(g_status, m, by + S(6), cw - 2 * m - btnW - gap, btnH - S(6), TRUE);
    int ph = by - gap - y;
    for (int i = 0; i < 2; i++)
        MoveWindow(g_pg[i], m, y, cw - 2 * m, ph, TRUE);

    /* from here on, coordinates are relative to a page panel */
    int px = S(8), pw = cw - 2 * m - 2 * S(8);
    int py = S(12), pb = ph - S(8);

    /* App Govern page */
    int ky = py;
    MoveWindow(g_chkEnable, px, ky, pw, row, TRUE);
    ky += row + gap;
    MoveWindow(g_lblInterval, px, ky + S(4), S(200), row - S(4), TRUE);
    MoveWindow(g_edInterval, px + S(200), ky, S(80), edH, TRUE);
    ky += edH + gap;
    MoveWindow(g_lblItems, px, ky, pw, row - S(4), TRUE);
    ky += row - S(4) + S(2);
    int ay = pb - btnH;
    MoveWindow(g_btnAdd, px, ay, btnW, btnH, TRUE);
    MoveWindow(g_btnRemove, px + btnW + gap, ay, btnW, btnH, TRUE);
    int lh = ay - gap - ky;
    if (lh < S(40)) lh = S(40);
    MoveWindow(g_list, px, ky, pw, lh, TRUE);

    /* Keep awake page */
    int wy = py;
    MoveWindow(g_chkAwake, px, wy, pw, row, TRUE);
    wy += row;
    MoveWindow(g_chkSim, px, wy, pw, row, TRUE);
    wy += row;
    MoveWindow(g_chkLid, px, wy, pw, row, TRUE);
    wy += row + gap;
    MoveWindow(g_lblAwakeInt, px, wy + S(4), S(200), row - S(4), TRUE);
    MoveWindow(g_edAwakeInt, px + S(200), wy, S(80), edH, TRUE);
    wy += edH + gap;
    MoveWindow(g_lblAwakeHelp, px, wy, pw, S(80), TRUE);
}

static HWND make(HWND parent, const wchar_t *cls, const wchar_t *text, DWORD style, int id)
{
    return CreateWindowExW(cls[0] == L'E' || cls[0] == L'L' ? WS_EX_CLIENTEDGE : 0,
                           cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0,
                           parent, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
}


/* Icon 1 is embedded from powertool.rc; falls back to the stock icon. */
static HICON app_icon(int cx, int cy)
{
    HICON i = (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(1),
                                IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);
    return i ? i : LoadIconW(NULL, IDI_APPLICATION);
}

static UINT g_msgTaskbarCreated;    /* broadcast when Explorer (re)creates the taskbar */

static void tray_add(HWND hwnd)
{
    g_nid.cbSize = sizeof g_nid;
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = app_icon(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
    wcscpy(g_nid.szTip, L"PowerTool");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

/* At logon the task can start before Explorer, so NIM_ADD fails; retry once the taskbar exists. */
static void tray_readd(void)
{
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    /* tray_tip() leaves uFlags == NIF_TIP; re-adding with that gives a blank icon, so restore all flags */
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    if (!g_nid.hIcon)
        g_nid.hIcon = app_icon(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    tray_tip();
}

static void show_settings(void)
{
    ShowWindow(g_hwnd, SW_SHOW);
    ShowWindow(g_hwnd, SW_RESTORE);
    SetForegroundWindow(g_hwnd);
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (g_msgTaskbarCreated && msg == g_msgTaskbarCreated) {
        tray_readd();
        return 0;
    }
    switch (msg) {
    case WM_CREATE: {
        wchar_t num[16];
        g_hwnd = hwnd;
        g_dpi = dpi_of(hwnd);
        g_chkAuto = make(hwnd, L"BUTTON", L"Start with Windows", BS_AUTOCHECKBOX | WS_TABSTOP, ID_AUTOSTART);
        g_btnPage[0] = make(hwnd, L"BUTTON", L"App Govern", BS_PUSHLIKE | BS_CHECKBOX | WS_TABSTOP, ID_PAGE0);
        g_btnPage[1] = make(hwnd, L"BUTTON", L"Keep awake", BS_PUSHLIKE | BS_CHECKBOX | WS_TABSTOP, ID_PAGE1);
        for (int i = 0; i < 2; i++)
            g_pg[i] = CreateWindowExW(WS_EX_CONTROLPARENT, PANEL_CLASS, L"",
                                      WS_CHILD | WS_CLIPCHILDREN, 0, 0, 0, 0, hwnd,
                                      NULL, GetModuleHandleW(NULL), NULL);
        g_chkEnable = make(g_pg[0], L"BUTTON", L"Enable ending blacklisted processes", BS_AUTOCHECKBOX | WS_TABSTOP, ID_ENABLE);
        g_chkAwake = make(g_pg[1], L"BUTTON", L"Keep screen and system awake (no screensaver / sleep)", BS_AUTOCHECKBOX | WS_TABSTOP, ID_AWAKE);
        g_chkSim = make(g_pg[1], L"BUTTON", L"Also simulate input (F15 key tap) to reset idle", BS_AUTOCHECKBOX | WS_TABSTOP, ID_AWAKE_SIM);
        g_chkLid = make(g_pg[1], L"BUTTON", L"Force sleep when the laptop lid is closed", BS_AUTOCHECKBOX | WS_TABSTOP, ID_AWAKE_LID);
        g_lblAwakeInt = make(g_pg[1], L"STATIC", L"Simulate input every (seconds):", 0, 0);
        swprintf(num, 16, L"%d", g_awakeSec);
        g_edAwakeInt = make(g_pg[1], L"EDIT", num, ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP, ID_AWAKE_INT);
        SendMessageW(g_edAwakeInt, EM_LIMITTEXT, 5, 0);
        g_lblAwakeHelp = make(g_pg[1], L"STATIC",
            L"The first option blocks the screensaver, display-off and sleep. "
            L"Apps that read the last-input time (Teams status, lock timers) "
            L"still see you as idle; enable the second option to reset that too.", 0, 0);
        g_lblInterval = make(g_pg[0], L"STATIC", L"Check interval (seconds):", 0, 0);
        swprintf(num, 16, L"%d", g_interval);
        g_edInterval = make(g_pg[0], L"EDIT", num, ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP, ID_INTERVAL);
        SendMessageW(g_edInterval, EM_LIMITTEXT, 5, 0);
        g_lblItems = make(g_pg[0], L"STATIC", L"Blacklisted executables (full path):", 0, 0);
        g_list = make(g_pg[0], L"LISTBOX", L"", LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP, ID_LIST);
        g_btnAdd = make(g_pg[0], L"BUTTON", L"Add...", BS_PUSHBUTTON | WS_TABSTOP, ID_ADD);
        g_btnRemove = make(g_pg[0], L"BUTTON", L"Remove", BS_PUSHBUTTON | WS_TABSTOP, ID_REMOVE);
        g_status = make(hwnd, L"STATIC", L"", 0, ID_STATUS);
        g_btnSave = make(hwnd, L"BUTTON", L"Save", BS_DEFPUSHBUTTON | WS_TABSTOP, ID_SAVE);

        font_rebuild(hwnd);
        layout(hwnd);
        SendMessageW(g_chkAuto, BM_SETCHECK, autostart_get() ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(g_chkEnable, BM_SETCHECK, g_enabled ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(g_chkAwake, BM_SETCHECK, g_awakeOn ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(g_chkSim, BM_SETCHECK, g_awakeSim ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(g_chkLid, BM_SETCHECK, g_lidSleep ? BST_CHECKED : BST_UNCHECKED, 0);
        g_lidNotify = RegisterPowerSettingNotification(hwnd, &k_lidGuid, DEVICE_NOTIFY_WINDOW_HANDLE);
        list_fill();
        page_select(0);
        tray_add(hwnd);
        timer_apply();
        awake_apply();
        tray_tip();
        return 0;
    }
    case WM_TIMER:
        if (wp == ID_TIMER) scan_and_kill();
        else if (wp == ID_TIMER_AWAKE) send_fake_input();
        return 0;
    case WM_POWERBROADCAST:
        if (wp == PBT_POWERSETTINGCHANGE) {
            const POWERBROADCAST_SETTING *ps = (const POWERBROADCAST_SETTING *)lp;
            if (IsEqualGUID(&ps->PowerSetting, &k_lidGuid) && ps->DataLength >= sizeof(DWORD) &&
                *(const DWORD *)ps->Data == 0)
                lid_closed();
        }
        return TRUE;
    case WM_SIZE:
        if (g_btnSave && wp != SIZE_MINIMIZED) layout(hwnd);
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)lp;
        RECT r = {0, 0, S(400), S(360)};
        adjust_rect(&r, WS_OVERLAPPEDWINDOW);
        mm->ptMinTrackSize.x = r.right - r.left;
        mm->ptMinTrackSize.y = r.bottom - r.top;
        return 0;
    }
    case WM_DPICHANGED: {
        const RECT *r = (const RECT *)lp;       /* suggested size/position at the new DPI */
        g_dpi = HIWORD(wp);
        SetWindowPos(hwnd, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        font_rebuild(hwnd);
        layout(hwnd);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_PAGE0:          page_select(0); break;
        case ID_PAGE1:          page_select(1); break;
        case ID_ADD:            on_add(hwnd); break;
        case ID_REMOVE:         on_remove(); break;
        case ID_SAVE:           on_save(hwnd); break;
        case ID_TRAY_SHOW:      show_settings(); break;
        case ID_TRAY_EXIT:      DestroyWindow(hwnd); break;
        }
        return 0;
    case WM_TRAY:
        if (lp == WM_LBUTTONDBLCLK) {
            show_settings();
        } else if (lp == WM_RBUTTONUP) {
            POINT pt;
            HMENU m = CreatePopupMenu();
            GetCursorPos(&pt);
            AppendMenuW(m, MF_STRING, ID_TRAY_SHOW, L"Settings");
            AppendMenuW(m, MF_STRING, ID_TRAY_EXIT, L"Exit");
            SetForegroundWindow(hwnd);
            TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(m);
        }
        return 0;
    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);      /* keep running in the tray */
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, ID_TIMER);
        KillTimer(hwnd, ID_TIMER_AWAKE);
        SetThreadExecutionState(ES_CONTINUOUS);
        if (g_lidNotify) UnregisterPowerSettingNotification(g_lidNotify);
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, PWSTR cmdLine, int nShow)
{
    (void)hPrev; (void)nShow; (void)cmdLine;

    dpi_enable();       /* must happen before any window is created */
    g_msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");


    HANDLE mutex = CreateMutexW(NULL, TRUE, L"Local\\" APP_NAME L"_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(NULL, L"PowerTool is already running (see the tray icon).",
                    APP_NAME, MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    GetModuleFileNameW(NULL, g_exe, MAX_PATH);
    config_init_path();
    config_load();

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hIcon = app_icon(GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
    wc.lpszClassName = APP_NAME L"Window";
    RegisterClassW(&wc);

    WNDCLASSW pc = {0};
    pc.lpfnWndProc = panel_proc;
    pc.hInstance = hInst;
    pc.hCursor = wc.hCursor;
    pc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    pc.lpszClassName = PANEL_CLASS;
    RegisterClassW(&pc);

    g_dpi = dpi_of(NULL);
    RECT rc = {0, 0, S(460), S(420)};
    DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
    adjust_rect(&rc, style);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"PowerTool - Settings", style,
                              CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
                              NULL, NULL, hInst, NULL);
    if (!hwnd) return 1;
    /* we run elevated; let Explorer's (non-elevated) TaskbarCreated broadcast through UIPI */
    ChangeWindowMessageFilterEx(hwnd, g_msgTaskbarCreated, MSGFLT_ALLOW, NULL);

    SendMessageW(hwnd, WM_SETICON, ICON_SMALL,
                 (LPARAM)app_icon(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON)));
    /* always start minimized to the tray; open Settings from the tray icon */

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }

    CloseHandle(mutex);
    return 0;
}
