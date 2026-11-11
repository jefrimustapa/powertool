/* dummysvc.c - minimal Win32 service used ONLY by test_svc_win32.c (never touches real services).
 * Build: gcc -O2 dummysvc.c -o dummysvc.exe -ladvapi32
 * Pass "slow" in the service binary path arguments to stay in START_PENDING for ~3 s. */
#include <windows.h>
#include <string.h>

static SERVICE_STATUS_HANDLE g_h;
static SERVICE_STATUS g_ss = { SERVICE_WIN32_OWN_PROCESS, SERVICE_START_PENDING, 0, 0, 0, 0, 3000 };
static HANDLE g_stop;
static int g_slow;

static DWORD WINAPI ctrl(DWORD c, DWORD t, LPVOID d, LPVOID x)
{
    (void)t; (void)d; (void)x;
    if (c == SERVICE_CONTROL_STOP) {
        g_ss.dwCurrentState = SERVICE_STOP_PENDING; g_ss.dwWaitHint = 3000;
        SetServiceStatus(g_h, &g_ss);
        SetEvent(g_stop);
    }
    return NO_ERROR;
}

static void WINAPI svcmain(DWORD argc, LPWSTR *argv)
{
    (void)argc;
    g_h = RegisterServiceCtrlHandlerExW(argv[0], ctrl, NULL);
    if (!g_h) return;
    g_stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    SetServiceStatus(g_h, &g_ss);
    if (g_slow) for (int i = 1; i <= 3; i++) { Sleep(1000); g_ss.dwCheckPoint = i; SetServiceStatus(g_h, &g_ss); }
    g_ss.dwCurrentState = SERVICE_RUNNING; g_ss.dwCheckPoint = 0; g_ss.dwWaitHint = 0;
    g_ss.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    SetServiceStatus(g_h, &g_ss);
    WaitForSingleObject(g_stop, INFINITE);
    g_ss.dwCurrentState = SERVICE_STOPPED; g_ss.dwControlsAccepted = 0;
    SetServiceStatus(g_h, &g_ss);
}

int main(int argc, char **argv)
{
    static wchar_t nm[] = L"";
    SERVICE_TABLE_ENTRYW t[] = { { nm, svcmain }, { NULL, NULL } };
    for (int i = 1; i < argc; i++) if (strcmp(argv[i], "slow") == 0) g_slow = 1;
    return StartServiceCtrlDispatcherW(t) ? 0 : 1;
}
