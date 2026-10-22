import serial
from SimConnect import SimConnect, AircraftEvents

PORT = "COM3"   # check Device Manager

sm = SimConnect()
ae = AircraftEvents(sm)

# Map each serial message to an MSFS event
MAP = {
    "E1+": "HEADING_BUG_INC",
    "E1-": "HEADING_BUG_DEC",
    "E2+": "AP_ALT_VAR_INC",
    "E2-": "AP_ALT_VAR_DEC",
    "B1":  "AP_MASTER",
}
events = {k: ae.find(v) for k, v in MAP.items()}

with serial.Serial(PORT, 115200, timeout=1) as ser:
    while True:
        line = ser.readline().decode(errors="ignore").strip()
        if line in events:
            events[line]()