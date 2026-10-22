const uint8_t PIN_A[2] = {D1, D5};
const uint8_t PIN_B[2] = {D2, D6};
const uint8_t PIN_BTN  = D7;
const int STEPS_PER_DETENT = 2;   // try 2 if one click = two events
int lastDir[2] = {0, 0};

uint8_t state[2] = {0, 0};
int acc[2] = {0, 0};
bool lastBtn = HIGH;
unsigned long lastBtnTime = 0;

const int8_t TBL[16] = {0,-1,1,0, 1,0,0,-1, -1,0,0,1, 0,1,-1,0};

void setup() {
  Serial.begin(115200);
  for (int i = 0; i < 2; i++) {
    pinMode(PIN_A[i], INPUT_PULLUP);
    pinMode(PIN_B[i], INPUT_PULLUP);
  }
  pinMode(PIN_BTN, INPUT_PULLUP);
}

void loop() {
  for (int i = 0; i < 2; i++) {
    uint8_t a = digitalRead(PIN_A[i]);
    uint8_t b = digitalRead(PIN_B[i]);
    state[i] = ((state[i] << 2) | (a << 1) | b) & 0x0F;
    int8_t step = TBL[state[i]];

    if (step != 0) {
      if (step != lastDir[i]) acc[i] = 0;   // direction changed: drop leftovers
      lastDir[i] = step;
      acc[i] += step;
    }

    if (acc[i] >= STEPS_PER_DETENT)  { Serial.printf("E%d+\n", i + 1); acc[i] = 0; }
    if (acc[i] <= -STEPS_PER_DETENT) { Serial.printf("E%d-\n", i + 1); acc[i] = 0; }
  }
  
  bool btn = digitalRead(PIN_BTN);
  if (btn != lastBtn && millis() - lastBtnTime > 20) {
    lastBtnTime = millis();
    lastBtn = btn;
    if (btn == LOW) Serial.println("B1");   // press only
  }
  yield();
}