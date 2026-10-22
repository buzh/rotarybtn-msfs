/*
 * connector_gui.c — Win32 GUI for rotary encoder -> MSFS SimConnect connector
 *
 * Build: run build.bat
 * Usage: connector.exe  (GUI lets you pick COM port and event mappings)
 * Config saved automatically to connector.ini next to the exe.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "SimConnect.h"

/* ------------------------------------------------------------------ */
/* Serial inputs (must match what the ESP sketch sends)                */
/* ------------------------------------------------------------------ */
static const char *INPUTS[] = { "E1+", "E1-", "E2+", "E2-", "B1" };
#define INPUT_COUNT 5

/* ------------------------------------------------------------------ */
/* SimConnect event list. Index 0 = "(none)". Index is used as the    */
/* SimConnect client event ID, so they must stay in this fixed order. */
/* ------------------------------------------------------------------ */
static const char *SIM_EVENTS[] = {
    "(none)",
    "HEADING_BUG_INC",          "HEADING_BUG_DEC",
    "AP_ALT_VAR_INC",           "AP_ALT_VAR_DEC",
    "AP_VS_VAR_INC",            "AP_VS_VAR_DEC",
    "AP_SPD_VAR_INC",           "AP_SPD_VAR_DEC",
    "AP_MASTER",                "AUTOPILOT_DISENGAGE_TOGGLE",
    "AUTOPILOT_HEADING_LOCK",   "AUTOPILOT_ALTITUDE_LOCK",
    "AUTOPILOT_VERTICAL_HOLD",  "AUTOPILOT_AIRSPEED_HOLD",
    "AUTOPILOT_NAV1_LOCK",      "AUTOPILOT_APPROACH_HOLD",
    "AUTOPILOT_BACKCOURSE_HOLD","AP_ATT_HOLD",
    "NAV_RADIO_WHOLE_INC",      "NAV_RADIO_WHOLE_DEC",
    "NAV_RADIO_FRACT_INC",      "NAV_RADIO_FRACT_DEC",
    "COM_RADIO_WHOLE_INC",      "COM_RADIO_WHOLE_DEC",
    "COM_RADIO_FRACT_INC",      "COM_RADIO_FRACT_DEC",
    "XPNDR_1000_INC",           "XPNDR_1000_DEC",
    "XPNDR_100_INC",            "XPNDR_100_DEC",
    "FLAPS_INCR",               "FLAPS_DECR",
    "THROTTLE_INCR",            "THROTTLE_DECR",
    NULL
};
static int g_event_count; /* set in WinMain */

/* ------------------------------------------------------------------ */
/* Control IDs                                                         */
/* ------------------------------------------------------------------ */
#define IDC_PORT_COMBO  100
#define IDC_REFRESH     101
#define IDC_CONNECT     102
#define IDC_MAP_BASE    200   /* IDC_MAP_BASE+i = combo for INPUTS[i] */
#define IDC_LOG         300
#define IDC_STATUS      301

/* Worker -> main thread messages */
#define WM_WORKER_LOG  (WM_USER + 1)  /* wParam = malloc'd char* (caller frees) */
#define WM_WORKER_DONE (WM_USER + 2)  /* serial port disconnected               */

/* ------------------------------------------------------------------ */
/* Global state                                                        */
/* ------------------------------------------------------------------ */
static HWND   g_hwnd    = NULL;
static HANDLE g_hSim    = NULL;
static HANDLE g_hSerial = NULL;
static HANDLE g_hThread = NULL;
static HANDLE g_hStop   = NULL;   /* manual-reset event to stop worker */
static BOOL   g_running = FALSE;

static int g_map[INPUT_COUNT];    /* index into SIM_EVENTS per input  */

static char g_ini[MAX_PATH];      /* full path to connector.ini       */

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */
static void log_append(const char *text) {
    HWND h = GetDlgItem(g_hwnd, IDC_LOG);
    int  n = GetWindowTextLength(h);
    SendMessage(h, EM_SETSEL, n, n);
    SendMessage(h, EM_REPLACESEL, FALSE, (LPARAM)text);
}

static void set_status(const char *text) {
    SetDlgItemText(g_hwnd, IDC_STATUS, text);
}

/* ------------------------------------------------------------------ */
/* Config (connector.ini)                                              */
/* ------------------------------------------------------------------ */
static void save_config(void) {
    char port[64], key[16], val[8];
    GetDlgItemText(g_hwnd, IDC_PORT_COMBO, port, sizeof(port));
    WritePrivateProfileStringA("connector", "port", port, g_ini);
    for (int i = 0; i < INPUT_COUNT; i++) {
        snprintf(key, sizeof(key), "map%d", i);
        snprintf(val, sizeof(val), "%d", g_map[i]);
        WritePrivateProfileStringA("connector", key, val, g_ini);
    }
}

/* Default mapping: E1+/- -> heading, E2+/- -> altitude, B1 -> AP master */
static const int DEFAULT_MAP[INPUT_COUNT] = { 1, 2, 3, 4, 9 };

static void load_config(void) {
    char port[64], key[16], val[8], def[8];
    GetPrivateProfileStringA("connector", "port", "COM3", port, sizeof(port), g_ini);
    HWND hCombo = GetDlgItem(g_hwnd, IDC_PORT_COMBO);
    if (SendMessageA(hCombo, CB_SELECTSTRING, (WPARAM)-1, (LPARAM)port) == CB_ERR)
        SendMessage(hCombo, CB_SETCURSEL, 0, 0);

    for (int i = 0; i < INPUT_COUNT; i++) {
        snprintf(key, sizeof(key), "map%d", i);
        snprintf(def, sizeof(def), "%d", DEFAULT_MAP[i]);
        GetPrivateProfileStringA("connector", key, def, val, sizeof(val), g_ini);
        g_map[i] = atoi(val);
        if (g_map[i] < 0 || g_map[i] >= g_event_count) g_map[i] = 0;
        SendDlgItemMessage(g_hwnd, IDC_MAP_BASE + i, CB_SETCURSEL, g_map[i], 0);
    }
}

/* ------------------------------------------------------------------ */
/* COM port enumeration                                                */
/* ------------------------------------------------------------------ */
static void populate_ports(void) {
    HWND hCombo = GetDlgItem(g_hwnd, IDC_PORT_COMBO);
    char cur[32] = {0};
    GetWindowTextA(hCombo, cur, sizeof(cur));
    SendMessage(hCombo, CB_RESETCONTENT, 0, 0);

    for (int i = 1; i <= 32; i++) {
        char path[32], name[16];
        snprintf(path, sizeof(path), "\\\\.\\COM%d", i);
        snprintf(name, sizeof(name), "COM%d", i);
        HANDLE h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
            SendMessageA(hCombo, CB_ADDSTRING, 0, (LPARAM)name);
        }
    }

    if (cur[0] && SendMessageA(hCombo, CB_SELECTSTRING, (WPARAM)-1, (LPARAM)cur) == CB_ERR)
        SendMessage(hCombo, CB_SETCURSEL, 0, 0);
    else if (!cur[0])
        SendMessage(hCombo, CB_SETCURSEL, 0, 0);
}

/* ------------------------------------------------------------------ */
/* Worker thread                                                       */
/* ------------------------------------------------------------------ */
typedef struct {
    HANDLE hSim;
    HANDLE hSerial;
    HANDLE hStop;
    HWND   hWnd;
    int    map[INPUT_COUNT];
} WorkerParams;

static DWORD WINAPI worker(LPVOID param) {
    WorkerParams *p  = (WorkerParams *)param;
    char  buf[32];
    int   pos = 0;
    char  ch;
    DWORD n;

    while (WaitForSingleObject(p->hStop, 0) == WAIT_TIMEOUT) {
        if (!ReadFile(p->hSerial, &ch, 1, &n, NULL)) break;
        if (n == 0) continue;

        if (ch == '\n' || ch == '\r') {
            if (pos > 0) {
                buf[pos] = '\0';
                pos = 0;
                for (int i = 0; i < INPUT_COUNT; i++) {
                    if (strcmp(buf, INPUTS[i]) != 0) continue;
                    int evt = p->map[i];
                    if (evt > 0) {
                        SimConnect_TransmitClientEvent(
                            p->hSim, SIMCONNECT_OBJECT_ID_USER, evt, 0,
                            SIMCONNECT_GROUP_PRIORITY_HIGHEST,
                            SIMCONNECT_EVENT_FLAG_GROUPID_IS_PRIORITY
                        );
                        char *msg = (char *)malloc(128);
                        if (msg) {
                            snprintf(msg, 128, "-> %-4s  =>  %s\r\n", buf, SIM_EVENTS[evt]);
                            PostMessage(p->hWnd, WM_WORKER_LOG, (WPARAM)msg, 0);
                        }
                    }
                    break;
                }
            }
        } else if (pos < (int)sizeof(buf) - 1) {
            buf[pos++] = ch;
        }
    }

    free(p);
    PostMessage(p->hWnd, WM_WORKER_DONE, 0, 0);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Connect / Disconnect                                                */
/* ------------------------------------------------------------------ */
static void ui_set_connected(BOOL connected) {
    SetDlgItemText(g_hwnd, IDC_CONNECT, connected ? "Disconnect" : "Connect");
    EnableWindow(GetDlgItem(g_hwnd, IDC_PORT_COMBO), !connected);
    EnableWindow(GetDlgItem(g_hwnd, IDC_REFRESH),    !connected);
    for (int i = 0; i < INPUT_COUNT; i++)
        EnableWindow(GetDlgItem(g_hwnd, IDC_MAP_BASE + i), !connected);
}

static void do_connect(void) {
    char port_name[64];
    GetDlgItemText(g_hwnd, IDC_PORT_COMBO, port_name, sizeof(port_name));
    if (!port_name[0]) {
        MessageBoxA(g_hwnd, "Select a COM port first.", "Error", MB_OK | MB_ICONWARNING);
        return;
    }

    /* Snapshot current mapping */
    for (int i = 0; i < INPUT_COUNT; i++)
        g_map[i] = (int)SendDlgItemMessage(g_hwnd, IDC_MAP_BASE + i, CB_GETCURSEL, 0, 0);
    save_config();

    /* SimConnect */
    HRESULT hr = SimConnect_Open(&g_hSim, "RotaryConnector", NULL, 0, NULL, 0);
    if (FAILED(hr)) {
        char msg[160];
        snprintf(msg, sizeof(msg), "SimConnect_Open failed (0x%08X)\nIs MSFS running?", hr);
        MessageBoxA(g_hwnd, msg, "Error", MB_OK | MB_ICONERROR);
        return;
    }
    for (int i = 1; SIM_EVENTS[i]; i++)
        SimConnect_MapClientEventToSimEvent(g_hSim, i, SIM_EVENTS[i]);

    /* Serial port */
    char path[32];
    snprintf(path, sizeof(path), "\\\\.\\%s", port_name);
    g_hSerial = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (g_hSerial == INVALID_HANDLE_VALUE) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Cannot open %s (error %lu)", port_name, GetLastError());
        MessageBoxA(g_hwnd, msg, "Error", MB_OK | MB_ICONERROR);
        SimConnect_Close(g_hSim);
        g_hSim = NULL;
        return;
    }

    DCB dcb = {0};
    dcb.DCBlength = sizeof(dcb);
    GetCommState(g_hSerial, &dcb);
    dcb.BaudRate = 115200;
    dcb.ByteSize = 8;
    dcb.StopBits = ONESTOPBIT;
    dcb.Parity   = NOPARITY;
    SetCommState(g_hSerial, &dcb);

    COMMTIMEOUTS to = {1000, 0, 1000, 0, 0};
    SetCommTimeouts(g_hSerial, &to);

    /* Worker thread */
    g_hStop = CreateEvent(NULL, TRUE, FALSE, NULL);
    WorkerParams *wp = (WorkerParams *)malloc(sizeof(WorkerParams));
    wp->hSim    = g_hSim;
    wp->hSerial = g_hSerial;
    wp->hStop   = g_hStop;
    wp->hWnd    = g_hwnd;
    memcpy(wp->map, g_map, sizeof(g_map));
    g_hThread = CreateThread(NULL, 0, worker, wp, 0, NULL);

    g_running = TRUE;
    ui_set_connected(TRUE);

    char logmsg[80];
    snprintf(logmsg, sizeof(logmsg), "Connected: %s\r\n", port_name);
    log_append(logmsg);
    set_status("Connected");
}

static void do_disconnect(void) {
    if (g_hStop)   SetEvent(g_hStop);
    if (g_hThread) { WaitForSingleObject(g_hThread, 3000); CloseHandle(g_hThread); g_hThread = NULL; }
    if (g_hStop)   { CloseHandle(g_hStop);   g_hStop   = NULL; }
    if (g_hSerial) { CloseHandle(g_hSerial); g_hSerial = NULL; }
    if (g_hSim)    { SimConnect_Close(g_hSim); g_hSim  = NULL; }

    g_running = FALSE;
    ui_set_connected(FALSE);
    log_append("Disconnected.\r\n");
    set_status("Disconnected");
}

/* ------------------------------------------------------------------ */
/* Window procedure                                                    */
/* ------------------------------------------------------------------ */
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_WORKER_LOG:
        log_append((char *)wp);
        free((void *)wp);
        return 0;

    case WM_WORKER_DONE:
        if (g_running) {
            log_append("Serial port disconnected.\r\n");
            do_disconnect();
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_REFRESH: populate_ports();                              return 0;
        case IDC_CONNECT: g_running ? do_disconnect() : do_connect();   return 0;
        }
        return 0;

    case WM_CLOSE:
        if (g_running) do_disconnect();
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

/* ------------------------------------------------------------------ */
/* Control layout                                                      */
/* ------------------------------------------------------------------ */
#define W_CLIENT 575
#define ROW_H    26
#define MAP_Y0   82
#define MAP_STEP 30

static HWND ctrl(HWND parent, const char *cls, const char *text,
                 DWORD style, int x, int y, int w, int h, int id, HFONT font) {
    HWND c = CreateWindowExA(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                             x, y, w, h, parent, (HMENU)(intptr_t)id, NULL, NULL);
    if (font) SendMessage(c, WM_SETFONT, (WPARAM)font, TRUE);
    return c;
}

static void create_controls(HWND hwnd) {
    HFONT fnt = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    /* Port row */
    ctrl(hwnd, "STATIC",   "COM Port:", SS_LEFT,         10,  16, 68, 18,  0,            fnt);
    ctrl(hwnd, "COMBOBOX", NULL, CBS_DROPDOWNLIST|WS_VSCROLL,
                                                          82,  12, 110, 200, IDC_PORT_COMBO, fnt);
    ctrl(hwnd, "BUTTON",   "Refresh",  BS_PUSHBUTTON,   202,  12,  68, ROW_H, IDC_REFRESH, fnt);
    ctrl(hwnd, "BUTTON",   "Connect",  BS_PUSHBUTTON,   278,  12,  80, ROW_H, IDC_CONNECT, fnt);

    /* Separator */
    CreateWindowExA(0, "STATIC", NULL, WS_CHILD|WS_VISIBLE|SS_ETCHEDHORZ,
                    10, 48, W_CLIENT - 20, 2, hwnd, NULL, NULL, NULL);

    /* Column headers */
    ctrl(hwnd, "STATIC", "Input",            SS_LEFT, 10,  58, 55, 18, 0, fnt);
    ctrl(hwnd, "STATIC", "SimConnect Event", SS_LEFT, 75,  58, 300, 18, 0, fnt);

    /* Mapping rows */
    for (int i = 0; i < INPUT_COUNT; i++) {
        int y = MAP_Y0 + i * MAP_STEP;

        /* Input label with sunken border */
        CreateWindowExA(WS_EX_CLIENTEDGE, "STATIC", INPUTS[i],
                        WS_CHILD|WS_VISIBLE|SS_CENTER,
                        10, y, 52, 22, hwnd, NULL, NULL, NULL);
        SendMessage(GetWindow(hwnd, GW_CHILD), WM_SETFONT, (WPARAM)fnt, TRUE);

        /* Event combo */
        HWND hc = ctrl(hwnd, "COMBOBOX", NULL,
                       CBS_DROPDOWNLIST|WS_VSCROLL,
                       72, y, W_CLIENT - 82, 300,
                       IDC_MAP_BASE + i, fnt);
        for (int j = 0; SIM_EVENTS[j]; j++)
            SendMessageA(hc, CB_ADDSTRING, 0, (LPARAM)SIM_EVENTS[j]);
    }

    /* Log */
    int log_y = MAP_Y0 + INPUT_COUNT * MAP_STEP + 12;
    ctrl(hwnd, "EDIT", NULL,
         ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_EX_CLIENTEDGE,
         10, log_y, W_CLIENT - 20, 155, IDC_LOG, fnt);

    /* Status */
    ctrl(hwnd, "STATIC", "Ready", SS_LEFT,
         10, log_y + 162, W_CLIENT - 20, 18, IDC_STATUS, fnt);
}

/* ------------------------------------------------------------------ */
/* WinMain                                                             */
/* ------------------------------------------------------------------ */
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow) {
    for (g_event_count = 0; SIM_EVENTS[g_event_count]; g_event_count++);

    /* Resolve ini path to exe directory */
    GetModuleFileNameA(NULL, g_ini, MAX_PATH);
    char *sep = strrchr(g_ini, '\\');
    if (sep) { sep[1] = '\0'; strcat(g_ini, "connector.ini"); }
    else      { strcpy(g_ini, "connector.ini"); }

    WNDCLASSEXA wc = {0};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "RotaryConnector";
    wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassExA(&wc);

    /* Window height: port row + sep + headers + 5 map rows + gap + log + status + chrome */
    int client_h = MAP_Y0 + INPUT_COUNT * MAP_STEP + 12 + 155 + 26 + 10;

    RECT rc = {0, 0, W_CLIENT, client_h};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME, FALSE);

    HWND hwnd = CreateWindowExA(
        0, "RotaryConnector", "Rotary Button MSFS Connector",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        NULL, NULL, hInst, NULL
    );

    g_hwnd = hwnd;
    create_controls(hwnd);
    populate_ports();
    load_config();

    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}
