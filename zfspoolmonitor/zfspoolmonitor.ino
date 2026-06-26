/*
 * ZFS Pool Health Monitor - Arduino Micro (ATmega32U4)
 *   ONLINE   -> GREEN solid
 *   DEGRADED -> YELLOW solid
 *   FAULTED  -> RED solid   (also OFFLINE/REMOVED/UNAVAIL/SUSPENDED)
 *   host down -> cycle GREEN/YELLOW/RED every 400 ms
 *
 * Health is pushed by the host: instantly via a ZED hook, and periodically
 * via a heartbeat timer (~every 5 s). No host-down heartbeat -> cycle.
 * Protocol: newline-terminated health word, case-insensitive.
 * Wiring: LED anode -> ~220R resistor -> digital pin, cathode -> GND.
 */

const uint8_t PIN_GREEN  = 5;
const uint8_t PIN_RED    = 9;
const uint8_t PIN_YELLOW = 7;

const unsigned long CYCLE_INTERVAL_MS = 600UL;
const unsigned long HOST_TIMEOUT_MS   = 15000UL;  // ~3x the 5s heartbeat

enum PoolState { POOL_ONLINE, POOL_DEGRADED, POOL_FAULTED };
PoolState poolState = POOL_FAULTED;     // default until first message
unsigned long lastMessageMs = 0;
bool everReceived = false;
String lineBuf = "";

unsigned long lastCycleMs = 0;
uint8_t cycleStep = 0;

// order: green, yellow, red
void setLeds(bool green, bool yellow, bool red) {
  digitalWrite(PIN_GREEN,  green  ? HIGH : LOW);
  digitalWrite(PIN_YELLOW, yellow ? HIGH : LOW);
  digitalWrite(PIN_RED,    red    ? HIGH : LOW);
}

void setup() {
  pinMode(PIN_GREEN,  OUTPUT);
  pinMode(PIN_YELLOW, OUTPUT);
  pinMode(PIN_RED,    OUTPUT);
  Serial.begin(9600);   // USB CDC on the Micro
  // starts in cycling/host-down state since nothing received yet
}

void handleLine(String s) {
  s.trim();
  s.toUpperCase();
  if (s == "ONLINE") {
    poolState = POOL_ONLINE;
  } else if (s == "DEGRADED") {
    poolState = POOL_DEGRADED;
  } else if (s == "FAULTED" || s == "OFFLINE" || s == "REMOVED" ||
             s == "UNAVAIL" || s == "SUSPENDED") {
    poolState = POOL_FAULTED;
  } else {
    return;   // unrecognized: ignore, do NOT count as a heartbeat
  }
  lastMessageMs = millis();
  everReceived = true;
}

void loop() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (lineBuf.length() > 0) { handleLine(lineBuf); lineBuf = ""; }
    } else {
      lineBuf += c;
      if (lineBuf.length() > 32) lineBuf = "";  // guard runaway input
    }
  }

  bool connected = everReceived &&
                   (millis() - lastMessageMs <= HOST_TIMEOUT_MS);

  if (!connected) {
    if (millis() - lastCycleMs >= CYCLE_INTERVAL_MS) {
      lastCycleMs = millis();
      cycleStep = (cycleStep + 1) % 3;
    }
    setLeds(cycleStep == 2, cycleStep == 1, cycleStep == 0);
  } else {
    switch (poolState) {
      case POOL_ONLINE:   setLeds(true,  false, false); break;  // green
      case POOL_DEGRADED: setLeds(false, true,  false); break;  // yellow
      case POOL_FAULTED:  setLeds(false, false, true);  break;  // red
    }
  }
}