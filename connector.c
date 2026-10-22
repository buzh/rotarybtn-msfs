/*
 * connector.c — reads rotary encoder serial messages and fires MSFS SimConnect events.
 * Compiled as C++ (/TP) because SimConnect.h requires it; code itself is plain C.
 *
 * Usage: connector.exe [COM_PORT]   (defaults to COM3)
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "SimConnect.h"

#define BAUD 115200

static HANDLE g_sim = NULL;

enum {
    EVT_HEADING_BUG_INC = 1,
    EVT_HEADING_BUG_DEC,
    EVT_AP_ALT_VAR_INC,
    EVT_AP_ALT_VAR_DEC,
    EVT_AP_MASTER,
};

static const struct {
    const char *msg;
    int         id;
    const char *sim_event;
} MAP[] = {
    { "E1+", EVT_HEADING_BUG_INC, "HEADING_BUG_INC" },
    { "E1-", EVT_HEADING_BUG_DEC, "HEADING_BUG_DEC" },
    { "E2+", EVT_AP_ALT_VAR_INC,  "AP_ALT_VAR_INC"  },
    { "E2-", EVT_AP_ALT_VAR_DEC,  "AP_ALT_VAR_DEC"  },
    { "B1",  EVT_AP_MASTER,       "AP_MASTER"        },
};
#define MAP_LEN (int)(sizeof(MAP) / sizeof(MAP[0]))

static void dispatch(const char *msg) {
    for (int i = 0; i < MAP_LEN; i++) {
        if (strcmp(msg, MAP[i].msg) == 0) {
            SimConnect_TransmitClientEvent(
                g_sim, SIMCONNECT_OBJECT_ID_USER, MAP[i].id, 0,
                SIMCONNECT_GROUP_PRIORITY_HIGHEST,
                SIMCONNECT_EVENT_FLAG_GROUPID_IS_PRIORITY
            );
            printf("-> %s (%s)\n", MAP[i].msg, MAP[i].sim_event);
            return;
        }
    }
}

int main(int argc, char *argv[]) {
    const char *port = (argc > 1) ? argv[1] : "COM3";

    char port_path[32];
    if (strncmp(port, "\\\\.\\", 4) == 0)
        snprintf(port_path, sizeof(port_path), "%s", port);
    else
        snprintf(port_path, sizeof(port_path), "\\\\.\\%s", port);

    HRESULT hr = SimConnect_Open(&g_sim, "RotaryConnector", NULL, 0, NULL, 0);
    if (FAILED(hr)) {
        fprintf(stderr, "SimConnect_Open failed (0x%08X) — is MSFS running?\n", hr);
        return 1;
    }
    printf("SimConnect connected.\n");

    for (int i = 0; i < MAP_LEN; i++)
        SimConnect_MapClientEventToSimEvent(g_sim, MAP[i].id, MAP[i].sim_event);

    HANDLE hSerial = CreateFileA(port_path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hSerial == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Cannot open %s (error %lu)\n", port_path, GetLastError());
        SimConnect_Close(g_sim);
        return 1;
    }

    DCB dcb = {0};
    dcb.DCBlength = sizeof(dcb);
    GetCommState(hSerial, &dcb);
    dcb.BaudRate = BAUD;
    dcb.ByteSize = 8;
    dcb.StopBits = ONESTOPBIT;
    dcb.Parity   = NOPARITY;
    SetCommState(hSerial, &dcb);

    COMMTIMEOUTS to = {1000, 0, 1000, 0, 0};
    SetCommTimeouts(hSerial, &to);

    printf("Listening on %s at %d baud...\n", port_path, BAUD);

    char  buf[32];
    int   pos = 0;
    char  ch;
    DWORD n;

    while (ReadFile(hSerial, &ch, 1, &n, NULL) && n == 1) {
        if (ch == '\n' || ch == '\r') {
            if (pos > 0) {
                buf[pos] = '\0';
                dispatch(buf);
                pos = 0;
            }
        } else if (pos < (int)sizeof(buf) - 1) {
            buf[pos++] = ch;
        }
    }

    fprintf(stderr, "Serial port disconnected.\n");
    CloseHandle(hSerial);
    SimConnect_Close(g_sim);
    return 0;
}
