/*
 * ZFS Pool Health Indicator
 * -------------------------
 * Receives pool status from a host over USB serial and lights LEDs:
 *
 *   'G'  -> GREEN   (all pools ONLINE, no errors)
 *   'Y'  -> YELLOW  (DEGRADED, errors present, or resilvering)
 *   'R'  -> RED     (FAULTED / UNAVAIL / SUSPENDED / zpool failure)
 *
 * If no update arrives within TIMEOUT_MS (host script died, cable
 * unplugged, etc.) all three LEDs cycle green -> yellow -> red to show
 * the status is stale. They also cycle after power-up until the first
 * status arrives.
 *
 * Wiring (each LED):
 *   Arduino pin -> 220 ohm resistor -> LED anode (long leg)
 *   LED cathode (short leg) -> GND
 */

const uint8_t PIN_GREEN  = 9;
const uint8_t PIN_YELLOW = 10;
const uint8_t PIN_RED    = 11;

const unsigned long BAUD_RATE  = 9600;
const unsigned long TIMEOUT_MS = 30000;  // must be longer than the host's check interval
const unsigned long CYCLE_MS   = 300;    // time each LED stays lit during the cycle

enum PoolState { STATE_UNKNOWN, STATE_HEALTHY, STATE_WARNING, STATE_CRITICAL };

PoolState state = STATE_UNKNOWN;
unsigned long lastUpdate = 0;
unsigned long lastStep = 0;
uint8_t cycleIndex = 0;

void setLeds(bool g, bool y, bool r) {
  digitalWrite(PIN_GREEN,  g ? HIGH : LOW);
  digitalWrite(PIN_YELLOW, y ? HIGH : LOW);
  digitalWrite(PIN_RED,    r ? HIGH : LOW);
}

void startupTest() {
  // Quick lamp test so you can confirm all three LEDs work
  setLeds(true, false, false);  delay(300);
  setLeds(false, true, false);  delay(300);
  setLeds(false, false, true);  delay(300);
  setLeds(false, false, false);
}

void handleCommand(char c) {
  switch (c) {
    case 'G': case 'g': state = STATE_HEALTHY;  break;
    case 'Y': case 'y': state = STATE_WARNING;  break;
    case 'R': case 'r': state = STATE_CRITICAL; break;
    default: return;  // ignore newlines and anything unexpected
  }
  lastUpdate = millis();
  Serial.print("ACK ");
  Serial.println(c);
}

void setup() {
  pinMode(PIN_GREEN, OUTPUT);
  pinMode(PIN_YELLOW, OUTPUT);
  pinMode(PIN_RED, OUTPUT);
  Serial.begin(BAUD_RATE);
  startupTest();
  Serial.println("ZFS monitor ready");
}

void loop() {
  while (Serial.available() > 0) {
    handleCommand((char)Serial.read());
  }

  unsigned long now = millis();
  bool stale = (state == STATE_UNKNOWN) || (now - lastUpdate > TIMEOUT_MS);

  if (stale) {
    // Cycle green -> yellow -> red: "host is not reporting"
    if (now - lastStep >= CYCLE_MS) {
      lastStep = now;
      cycleIndex = (cycleIndex + 1) % 3;
    }
    setLeds(cycleIndex == 0, cycleIndex == 1, cycleIndex == 2);
    return;
  }

  switch (state) {
    case STATE_HEALTHY:  setLeds(true,  false, false); break;
    case STATE_WARNING:  setLeds(false, true,  false); break;
    case STATE_CRITICAL: setLeds(false, false, true);  break;
    default:             setLeds(false, false, false); break;
  }
}
