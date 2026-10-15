// Plain-LED receiver: ESP32 + Seeed LoRa-E5 (STM32WLE5JC, AT-command firmware,
// https://wiki.seeedstudio.com/LoRa-E5_STM32WLE5JC_Module/) over UART2, wired
// as a drop-in stand-in for receiver.ino's CubeCell+HM-TRLR+NeoPixel setup.
// Mirrors receiver.ino's constants, transmitter-id/channel scheme, packet
// validation, RSSI display and log strings as closely as the hardware allows:
//   - Radio: SX126x on CubeCell -> LoRa-E5 in AT+TEST P2P mode. Same
//     frequency scheme (BASE_RF_FREQUENCY + (id-1)*FREQUENCY_STEP), SF7,
//     125 kHz BW, preamble 8. The LoRa-E5's AT set has no sync-word register,
//     so the sync_word[CAFE<id>x4BABE] filtering receiver.ino does at the
//     radio is replicated here at the application layer instead: payload[0]
//     and payload[1] must both equal the configured transmitter id.
//   - Display: 7 CubeCell NeoPixels -> 7 plain LEDs on LED_PINS[], driven
//     with the same to_first_led_bits/to_last_led_bits/display_bits_on_leds
//     bit math as receiver.ino, just digitalWrite instead of NeoPixel colors.
//   - Id cycling: receiver.ino changes id on a SW1 press; here it's a button
//     wired to GPIO32 (to GND, internal pull-up), short-press only - no
//     long-press power-off, since this board has no equivalent sleep mode. A digit
//     1-N over USB serial does the same thing (N = channel setting, default 7).
//
// Wiring: ESP32 TX2 (GPIO17) -> LoRa-E5 RX, ESP32 RX2 (GPIO16) <- LoRa-E5 TX,
// GND-GND. LEDs on LED_PINS[] (D33,D25,D26,D27,D14,D12,D13, reversed from the
// original D13,D12,D14,D27,D26,D25,D33 wiring), each with a series resistor
// to GND (active-HIGH).
#include <Arduino.h>
#include <Preferences.h>
#include <LisekOta.h>  // updates and version readout over Bluetooth, software/libraries

#define MODULE_RX 16  // ESP32 RX2 <- LoRa-E5 TX
#define MODULE_TX 17  // ESP32 TX2 -> LoRa-E5 RX
#define MODULE_BAUD 115200  // set on the LoRa-E5 by fast_link(); its factory default is 9600
#define MODULE_BAUD_DEFAULT 9600
// At 9600 baud one received packet ("+TEST: LEN:..." plus "+TEST: RX "<64 hex>"",
// ~111 chars) takes ~116 ms on the wire, longer than the transmitter's 100 ms
// interval: packets queued up and the LEDs lagged further and further behind.
// At 115200 the same lines take ~10 ms.

// Same constants as receiver.ino.
#define BASE_RF_FREQUENCY_HZ 863000000UL
#define FREQUENCY_STEP_HZ 500000UL
#define RF_SF "SF7"        // LORA_SPREADING_FACTOR 7
#define RF_BW "125"        // LORA_BANDWIDTH 0 -> 125 kHz
#define RF_PREAMBLE 8       // LORA_PREAMBLE_LENGTH
#define RF_POWER 14         // TX_OUTPUT_POWER (RX side power is unused but AT+TEST=RFCFG sets both)

const int LED_PINS[] = { 33, 25, 26, 27, 14, 12, 13 };  // reversed order
const int NUM_LEDS = sizeof(LED_PINS) / sizeof(LED_PINS[0]);

// No packet from the selected transmitter for this long = no signal, LEDs off
// (it sends every 100 ms).
#define NO_SIGNAL_MS 1000

#define BUTTON_PIN 32  // external button to GND, matches receiver.ino's SW1 (GPIO0 on CubeCell)

uint8_t transmitter_id = 1;  // matches receiver.ino's initial set_transmitter_id(1)

// Panel settings (web page over Bluetooth only, stored in NVS so they survive
// power-off):
//   leds: how the selected id is shown
//     unary (default): that many LEDs from LED7 down, so at most 7
//     binary: LED7 = 1, LED6 = 2, LED5 = 4, LED4 = 8, up to 9
//   channels: how many transmitter ids the button cycles through (default 7)
volatile bool binary_leds = false;
volatile uint8_t channels = 7;
volatile bool settings_changed = false;
volatile uint8_t requested_id = 0;  // applied by loop(), which owns the radio UART
volatile bool requested_id_ok = false;
SemaphoreHandle_t id_change_done;

uint8_t max_id() {
  uint8_t shown = binary_leds ? 9 : 7;
  return channels < shown ? channels : shown;
}

void advertise_settings() {
  LisekOta::setUserBits((binary_leds ? 1 : 0) | ((channels - 1) << 1));
}

String lastLine;

// Reads one line from the module, trims it, times out after ms.
String readLine(uint32_t timeoutMs) {
  uint32_t start = millis();
  String line;
  while (millis() - start < timeoutMs) {
    while (Serial1.available()) {
      char c = Serial1.read();
      if (c == '\n') {
        line.trim();
        if (line.length()) return line;
      } else if (c != '\r') {
        line += c;
      }
    }
  }
  line.trim();
  return line;
}

// Sends an AT command, returns the first non-empty response line.
String sendAT(const String &cmd, uint32_t timeoutMs = 1000) {
  while (Serial1.available()) Serial1.read();
  Serial1.print(cmd);
  Serial1.print("\r\n");
  String r = readLine(timeoutMs);
  lastLine = r;
  return r;
}

// --- LED bit helpers, ported 1:1 from receiver.ino ---

uint8_t to_last_led_bits(uint8_t x) {
  if (x >= 8) return 0xFF;
  return ((1u << x) - 1u);
}

uint8_t to_first_led_bits(uint8_t x) {
  if (x == 0) return 0;
  if (x >= 8) return 0xFF;
  x += 1;
  return ((1u << x) - 1u) << (8 - x);
}

void display_bits_on_leds(int8_t bits) {
  for (int i = 0; i < NUM_LEDS; i++) {
    digitalWrite(LED_PINS[i], (bits & (1 << i)) > 0 ? HIGH : LOW);
  }
}

void display_strength_on_leds(int8_t strength) {
  display_bits_on_leds(to_last_led_bits(strength));
}

uint8_t id_bits(uint8_t id) {
  if (!binary_leds) return to_first_led_bits(id);
  uint8_t bits = 0;
  for (int b = 0; b < 4; b++)
    if (id & (1 << b)) bits |= 1 << (NUM_LEDS - 1 - b);
  return bits;
}

// "set leds=binary|unary" / "set channels=N" from the web panel (runs on the
// Bluetooth task).
// --- Signal display: scale (curve), sensitivity and smoothing, all set from
// the web panel ("set scale=..|sens=1-5|smooth=0-2") and stored in NVS. ---
enum Scale : uint8_t { SCALE_AUTO, SCALE_LINEAR, SCALE_NEAR, SCALE_FAR, SCALE_RELATIVE };
const char *const SCALE_NAMES[] = { "auto", "linear", "near", "far", "relative" };
volatile uint8_t scale = SCALE_AUTO;
volatile uint8_t sens = 3;    // 1..5: bar starts at -130/-120/-110/-100/-90 dBm (3 = the original scale)
volatile uint8_t smooth = 1;  // 0 off, 1 = 3 packets, 2 = 5 packets
volatile bool signal_reset = true;

int sens_floor() {
  return -110 + (sens - 3) * 10;
}

// dB above the floor at which LED 1..7 lights, per fixed curve (7 entries).
const uint8_t CURVE_LINEAR[7] = { 15, 30, 45, 60, 75, 90, 105 };
const uint8_t CURVE_NEAR[7] = { 40, 60, 74, 84, 92, 99, 105 };  // big steps far away, fine steps near the fox
const uint8_t CURVE_FAR[7] = { 6, 13, 21, 31, 45, 65, 105 };    // fine steps far away, catches the first faint signal

uint8_t leds_on_curve(int rssi, int floor_dbm, const uint8_t *curve) {
  uint8_t n = 0;
  while (n < 7 && rssi >= floor_dbm + curve[n]) n++;
  return n;
}

// Auto-zoom: when the bar stays at 6-7 LEDs for ZOOM_HOLD_MS it zooms in
// (finer dB per LED, current signal back to the middle); at 0-1 LEDs it zooms
// out again. A quick sweep on the LEDs shows each zoom.
const uint8_t ZOOM_STEP[] = { 10, 5, 3, 2 };  // dB per LED per zoom level; the widest is full at about -40 dBm (next to the fox)
const uint32_t ZOOM_HOLD_MS = 2000;
uint8_t zoom = 0;
int zoom_floor = -110;
uint32_t zoom_since = 0;
int8_t zoom_dir = 0;  // +1 waiting to zoom in, -1 waiting to zoom out

void zoom_sweep(bool in) {
  for (int k = 0; k < 7; k++) {
    display_bits_on_leds(to_last_led_bits(in ? k + 1 : 7 - k));
    delay(35);
  }
}

uint8_t leds_auto(int rssi) {
  int step = ZOOM_STEP[zoom];
  int n = constrain((rssi - zoom_floor) / step, 0, 7);
  int8_t want = n >= 6 && zoom < sizeof ZOOM_STEP - 1 ? 1 : n <= 1 && zoom > 0 ? -1 : 0;
  if (want != zoom_dir) zoom_dir = want, zoom_since = millis();
  if (want && millis() - zoom_since >= ZOOM_HOLD_MS) {
    zoom += want;
    // put the current signal in the middle of the new range (the widest zoom keeps the sensitivity floor)
    zoom_floor = zoom ? rssi - 7 * ZOOM_STEP[zoom] / 2 : sens_floor();
    zoom_dir = 0;
    zoom_sweep(want > 0);
    n = constrain((rssi - zoom_floor) / ZOOM_STEP[zoom], 0, 7);
  }
  return n;
}

// Relative: the weakest signal of the last RELATIVE_MS maps to 1 LED, the
// strongest to 7, for sweeping a directional antenna.
const uint32_t RELATIVE_MS = 10000;
const uint8_t REL_SLOTS = 20;
int rel_min[REL_SLOTS], rel_max[REL_SLOTS];
uint32_t rel_slot_start = 0;
uint8_t rel_slot = 0;

uint8_t leds_relative(int rssi) {
  uint32_t now = millis();
  if (now - rel_slot_start >= RELATIVE_MS / REL_SLOTS) {
    rel_slot = (rel_slot + 1) % REL_SLOTS;
    rel_min[rel_slot] = 999, rel_max[rel_slot] = -999;
    rel_slot_start = now;
  }
  rel_min[rel_slot] = min(rel_min[rel_slot], rssi);
  rel_max[rel_slot] = max(rel_max[rel_slot], rssi);
  int lo = 999, hi = -999;
  for (int i = 0; i < REL_SLOTS; i++) lo = min(lo, rel_min[i]), hi = max(hi, rel_max[i]);
  if (hi - lo < 10) {  // too little variation yet: a 10 dB window around it
    int mid = (hi + lo) / 2;
    lo = mid - 5, hi = mid + 5;
  }
  return 1 + (rssi - lo) * 6 / (hi - lo);
}

// Smoothing ring buffer
int smooth_buf[5];
uint8_t smooth_n = 0, smooth_i = 0;

void reset_signal_display() {
  smooth_n = smooth_i = 0;
  zoom = 0, zoom_floor = sens_floor(), zoom_dir = 0;
  for (int i = 0; i < REL_SLOTS; i++) rel_min[i] = 999, rel_max[i] = -999;
  rel_slot_start = millis();
}

// Number of LEDs (0..7) for a received packet's RSSI.
uint8_t signal_leds(int rssi) {
  if (signal_reset) signal_reset = false, reset_signal_display();
  uint8_t window = smooth == 0 ? 1 : smooth == 1 ? 3 : 5;
  smooth_buf[smooth_i] = rssi;
  smooth_i = (smooth_i + 1) % window;
  if (smooth_n < window) smooth_n++;
  long sum = 0;
  for (int i = 0; i < smooth_n; i++) sum += smooth_buf[i];
  int r = sum / smooth_n;
  switch (scale) {
    case SCALE_LINEAR: return leds_on_curve(r, sens_floor(), CURVE_LINEAR);
    case SCALE_NEAR: return leds_on_curve(r, sens_floor(), CURVE_NEAR);
    case SCALE_FAR: return leds_on_curve(r, sens_floor(), CURVE_FAR);
    case SCALE_RELATIVE: return leds_relative(r);
    default: return leds_auto(r);
  }
}

String display_settings_json() {
  return String("{\"scale\":\"") + SCALE_NAMES[scale] + "\",\"sens\":" + sens + ",\"smooth\":" + smooth + "}";
}

bool on_setting(const String &key, const String &value) {
  Preferences prefs;
  if (key == "id") {
    int id = value.toInt();
    if (value != String(id) || id < 1 || id > max_id()) return false;
    while (xSemaphoreTake(id_change_done, 0) == pdTRUE) {}
    requested_id = id;
    requested_id_ok = false;
    settings_changed = true;
    if (xSemaphoreTake(id_change_done, pdMS_TO_TICKS(4000)) != pdTRUE || !requested_id_ok) return false;
    prefs.begin("receiver", false);
    prefs.putUChar("id", id);
    prefs.end();
    return true;
  } else if (key == "leds" && (value == "binary" || value == "unary")) {
    binary_leds = value == "binary";
    prefs.begin("receiver", false);
    prefs.putBool("binary_leds", binary_leds);
  } else if (key == "scale" || key == "sens" || key == "smooth") {
    int v = value.toInt();
    if (key == "scale") {
      v = -1;
      for (int i = 0; i < 5; i++)
        if (value == SCALE_NAMES[i]) v = i;
      if (v < 0) return false;
      scale = v;
    } else if (key == "sens") {
      if (value != String(v) || v < 1 || v > 5) return false;
      sens = v;
    } else {
      if (value != String(v) || v < 0 || v > 2) return false;
      smooth = v;
    }
    prefs.begin("receiver", false);
    prefs.putUChar(key.c_str(), v);
    prefs.end();
    signal_reset = true;
    LisekOta::setInfoExtra(display_settings_json());
    return true;
  } else if (key == "channels" && value.toInt() >= 1 && value.toInt() <= (binary_leds ? 9 : 7)) {
    channels = value.toInt();
    prefs.begin("receiver", false);
    prefs.putUChar("channels", channels);
  } else {
    return false;
  }
  prefs.end();
  advertise_settings();
  settings_changed = true;
  return true;
}

// --- Radio setup, ported from receiver.ino's set_transmitter_id/SetChannel ---

// Sends a command line to the module, dropping anything still buffered.
void command(const char *cmd) {
  while (Serial1.available()) Serial1.read();
  Serial1.print(cmd);
  Serial1.print("\r\n");
}

// Reads module lines until one starts with `prefix` (other lines, e.g. a
// packet that was still in flight, are dropped). Returns as soon as it arrives.
bool waitFor(const char *prefix, uint32_t timeoutMs) {
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    String l = readLine(timeoutMs - (millis() - start));
    if (l.startsWith(prefix)) return true;
  }
  return false;
}

// Sets the frequency for `id` and starts continuous receive. Every step moves
// on as soon as the module confirms it.
bool tune_and_receive(uint8_t id) {
  unsigned long freqHz = BASE_RF_FREQUENCY_HZ + (unsigned long)(id - 1) * FREQUENCY_STEP_HZ;
  char cfg[64];
  snprintf(cfg, sizeof cfg, "AT+TEST=RFCFG,%lu.%lu,%s,%s,%d,%d,%d,ON,OFF,OFF", freqHz / 1000000UL,
           (freqHz % 1000000UL) / 100000UL, RF_SF, RF_BW, RF_PREAMBLE, RF_POWER, RF_POWER);
  command(cfg);
  if (!waitFor("+TEST: RFCFG", 500)) return false;
  command("AT+TEST=RXLRPKT");
  if (!waitFor("+TEST: RXLRPKT", 300)) return false;
  transmitter_id = id;
  LisekOta::setId(id);
  return true;
}

// True when the module answers "AT" at the current Serial1 baud.
bool module_answers(int tries) {
  for (int i = 0; i < tries; i++) {
    if (sendAT("AT", 300) == "+AT: OK") return true;
  }
  return false;
}

// Puts the UART link to the LoRa-E5 at MODULE_BAUD. The module keeps its baud
// rate across power-off, so this only switches it once (from the 9600 default).
void fast_link() {
  Serial1.updateBaudRate(MODULE_BAUD);
  sendAT("AT+TEST=STOP", 200);  // a still-streaming RXLRPKT session ignores plain AT
  if (module_answers(3)) return;
  Serial1.updateBaudRate(MODULE_BAUD_DEFAULT);
  sendAT("AT+TEST=STOP", 200);
  if (!module_answers(5)) {
    Serial1.updateBaudRate(MODULE_BAUD);  // neither rate answers: leave it to the normal retries
    return;
  }
  String r = sendAT("AT+UART=BR, " + String(MODULE_BAUD));
  LisekOta::logf("radio: LoRa-E5 at %d baud, switching to %d: \"%s\"\r\n", MODULE_BAUD_DEFAULT, MODULE_BAUD, r.c_str());
  sendAT("AT+RESET");  // the new rate takes effect after a reset
  delay(1500);
  Serial1.updateBaudRate(MODULE_BAUD);
  while (Serial1.available()) Serial1.read();
  LisekOta::logf("radio: LoRa-E5 answers at %d: %s\r\n", MODULE_BAUD, module_answers(5) ? "yes" : "no");
}

// Full radio init: enters AT+TEST P2P mode at the frequency for `id` and
// starts continuous packet receive. Equivalent to receiver.ino's
// set_transmitter_id() (channel part) + Radio.Rx(0). Used at boot and as a
// fallback when a quick retune fails.
bool set_transmitter_id(uint8_t id) {
  // If a previous RXLRPKT session is still streaming (e.g. right after an
  // ESP32-only reset that didn't touch the module), plain AT won't interrupt
  // it - the module needs an explicit stop first. Best-effort: ignore the
  // reply, drain whatever comes back, then retry AT until a clean "+AT: OK".
  sendAT("AT+TEST=STOP");
  delay(200);
  while (Serial1.available()) Serial1.read();

  String r;
  bool atOk = false;
  for (int attempt = 0; attempt < 15 && !atOk; attempt++) {
    r = sendAT("AT", 500);
    atOk = (r == "+AT: OK");
  }
  if (!atOk) {
    // Silence means no power or a broken TX/RX wire; garbage means a wrong baud rate.
    LisekOta::logf("radio: no \"+AT: OK\" from LoRa-E5 after 15 tries, last reply \"%s\" (%u chars)\r\n", r.c_str(),
                  r.length());
    return false;
  }

  r = sendAT("AT+MODE=TEST");
  if (r.indexOf("+MODE: TEST") < 0) {
    LisekOta::logf("radio: AT+MODE=TEST answered \"%s\"\r\n", r.c_str());
    return false;
  }

  return tune_and_receive(id);
}

// Quick channel change for a module that is already receiving in TEST mode:
// stop, set the new frequency, receive again.
bool retune(uint8_t id) {
  command("AT+TEST=STOP");
  if (!waitFor("+TEST: STOP", 300)) return false;
  return tune_and_receive(id);
}

// Parses "+TEST: LEN:2, RSSI:-42, SNR:9".
void parseLenLine(const String &line, int &len, int &rssi, int &snr) {
  int li = line.indexOf("LEN:");
  len = li >= 0 ? line.substring(li + 4).toInt() : 0;
  int ri = line.indexOf("RSSI:");
  rssi = ri >= 0 ? line.substring(ri + 5).toInt() : 0;
  int si = line.indexOf("SNR:");
  snr = si >= 0 ? line.substring(si + 4).toInt() : 0;
}

// Parses "+TEST: RX "0102"" -> raw bytes.
bool parseRxData(const String &line, uint8_t *out, int &outLen) {
  int q1 = line.indexOf('"');
  int q2 = line.lastIndexOf('"');
  if (q1 < 0 || q2 <= q1) return false;
  String hex = line.substring(q1 + 1, q2);
  outLen = 0;
  for (int i = 0; i + 1 < (int)hex.length() && outLen < 32; i += 2) {
    out[outLen++] = strtol(hex.substring(i, i + 2).c_str(), nullptr, 16);
  }
  return outLen > 0;
}

int pendingRssi = 0;
int pendingSnr = 0;
bool havePending = false;

void setup() {
  // 80 MHz is plenty (UART, LEDs, Bluetooth) and draws far less than 240 MHz,
  // which matters on battery: Bluetooth on top of 240 MHz browned the boards out.
  setCpuFrequencyMhz(80);
  Serial.begin(115200);
  LisekOta::logln("Hej liski!");
  LisekOta::begin("receiver");
  id_change_done = xSemaphoreCreateBinary();
  Preferences prefs;
  prefs.begin("receiver", true);
  binary_leds = prefs.getBool("binary_leds", false);
  scale = min((int)prefs.getUChar("scale", SCALE_AUTO), (int)SCALE_RELATIVE);
  sens = constrain(prefs.getUChar("sens", 3), 1, 5);
  smooth = min((int)prefs.getUChar("smooth", 1), 2);
  LisekOta::setInfoExtra(display_settings_json());
  channels = constrain(prefs.getUChar("channels", 7), 1, binary_leds ? 9 : 7);
  transmitter_id = constrain(prefs.getUChar("id", 1), 1, min((int)channels, binary_leds ? 9 : 7));
  prefs.end();
  advertise_settings();
  LisekOta::logln("firmware " LISEK_VERSION);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  for (int i = 0; i < NUM_LEDS; i++) pinMode(LED_PINS[i], OUTPUT);

  // Startup LED chase, ported from receiver.ino's boot animation.
  for (int rep = 0; rep < 2; rep++) {
    for (int j = 0; j < 7; j++) {
      display_bits_on_leds(to_first_led_bits(j));
      delay(50);
    }
    for (int j = 0; j < 7; j++) {
      display_bits_on_leds(to_last_led_bits(8 - j));
      delay(50);
    }
  }
  display_bits_on_leds(0);

  Serial1.setRxBufferSize(1024);
  Serial1.begin(MODULE_BAUD, SERIAL_8N1, MODULE_RX, MODULE_TX);
  delay(200);
  fast_link();

  while (!set_transmitter_id(transmitter_id)) {
    LisekOta::logln("radio init failed, retrying in 2s...");
    delay(2000);
    fast_link();
  }
  LisekOta::markHealthy();
  LisekOta::onSetting(on_setting);
  LisekOta::logln("into RX mode");
}

// While non-zero, the LEDs show the newly selected id until this millis() time
// (receiver.ino's 500 ms id flash, without blocking presses or the radio).
uint32_t id_flash_until = 0;
// millis() of the last packet from the selected transmitter, 0 = none shown.
uint32_t last_packet_ms = 0;

bool change_transmitter_id_to(uint8_t id) {
  last_packet_ms = 0;  // the old transmitter's strength no longer applies
  signal_reset = true;
  // Feedback first: the id shows on the LEDs the moment the button goes down.
  display_bits_on_leds(id_bits(id));
  id_flash_until = millis() + 500;
  if (id_flash_until == 0) id_flash_until = 1;

  if (retune(id) || set_transmitter_id(id)) {
    LisekOta::logf("\r\nChanged transmitter id to %d\r\n", transmitter_id);
    return true;
  } else {
    LisekOta::logln("failed to change transmitter id");
    return false;
  }
}

void loop() {
  static String line;

  // Button: short press cycles the transmitter id, same as receiver.ino's
  // SW1 - but no long-press power-off, this board has no sleep mode to match.
  // Acts on the press edge itself; contact bounce within 30 ms of the last
  // accepted edge is ignored instead of waited out.
  static bool buttonHigh = true;
  static uint32_t lastEdgeMs = 0;
  bool high = digitalRead(BUTTON_PIN) == HIGH;
  if (high != buttonHigh && millis() - lastEdgeMs >= 30) {
    buttonHigh = high;
    lastEdgeMs = millis();
    if (!high) change_transmitter_id_to(1 + (transmitter_id % max_id()));
  }

  // Settings changed from the panel: show the id the new way; an id that is no
  // longer allowed (fewer channels, 8+ in unary) goes back to 1.
  if (settings_changed) {
    settings_changed = false;
    uint8_t new_id = requested_id;
    requested_id = 0;
    if (new_id) {
      requested_id_ok = change_transmitter_id_to(new_id);
      xSemaphoreGive(id_change_done);
    } else if (transmitter_id > max_id()) {
      change_transmitter_id_to(1);
    } else {
      display_bits_on_leds(id_bits(transmitter_id));
      id_flash_until = millis() + 1500;
    }
  }

  if (id_flash_until && (int32_t)(millis() - id_flash_until) >= 0) {
    id_flash_until = 0;
    display_bits_on_leds(0);
  }

  // Signal lost: don't keep showing the last strength.
  if (last_packet_ms && millis() - last_packet_ms >= NO_SIGNAL_MS) {
    last_packet_ms = 0;
    if (!id_flash_until) display_bits_on_leds(0);
  }

  // A firmware update over Bluetooth fills the LEDs as it arrives.
  if (LisekOta::updating()) {
    uint32_t total, done = LisekOta::progress(&total);
    // all 7 LEDs evenly (one per ~14%), full once installed until the reboot
    display_strength_on_leds(total ? max(1, (int)(((uint64_t)done * 7 + total - 1) / total)) : 0);
  }

  // A digit over USB serial does the same thing.
  while (Serial.available()) {
    char c = Serial.read();
    if (c >= '1' && c <= '0' + max_id()) {
      change_transmitter_id_to(c - '0');
    }
  }

  while (Serial1.available()) {
    char c = Serial1.read();
    if (c == '\n') {
      line.trim();
      if (line.length()) {
        if (line.startsWith("+TEST: LEN:")) {
          int size, rssi, snr;
          parseLenLine(line, size, rssi, snr);

          if (size < 2) {
            LisekOta::logln("WARN: packet of size 0 or 1 recieved");
          } else {
            // Actual payload arrives on the next "+TEST: RX ..." line; stash
            // rssi/snr for it.
            pendingRssi = rssi;
            pendingSnr = snr;
            havePending = true;
          }
        } else if (line.startsWith("+TEST: RX ") && havePending) {
          havePending = false;
          uint8_t payload[32];
          int size = 0;
          if (parseRxData(line, payload, size)) {
            if (size > 29) size = 29;

            // sync_word filtering, done in receiver.ino's radio by matching
            // sync_word[2..5]==id; done here at the application layer.
            if (payload[0] != transmitter_id || payload[1] != transmitter_id) {
              // not our transmitter - ignore, matching receiver.ino's silent drop.
            } else {
              char rxpacket[30];
              memcpy(rxpacket, payload, size);
              rxpacket[size] = '\0';

              last_packet_ms = millis();
              uint8_t leds = signal_leds(pendingRssi);
              if (!id_flash_until && !LisekOta::updating()) display_strength_on_leds(leds);
              Serial.printf("received packet \"%s\" with rssi=%d snr=%d length=%d packet_tx_id=%d\r\n", rxpacket,
                            pendingRssi, pendingSnr, size, rxpacket[1]);
            }
          }
        } else if (line.length()) {
          LisekOta::logf("module: %s\r\n", line.c_str());
        }
      }
      line = "";
    } else if (c != '\r') {
      line += c;
    }
  }
}
