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
#define MODULE_BAUD 9600  // LoRa-E5 default AT baud

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
//     binary: LED7 = 1, LED6 = 2, LED5 = 4, LED4 = 8, up to 8
//   channels: how many transmitter ids the button cycles through (default 7)
volatile bool binary_leds = false;
volatile uint8_t channels = 7;
volatile bool settings_changed = false;

uint8_t max_id() {
  uint8_t shown = binary_leds ? 8 : 7;
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
bool on_setting(const String &key, const String &value) {
  Preferences prefs;
  if (key == "leds" && (value == "binary" || value == "unary")) {
    binary_leds = value == "binary";
    prefs.begin("receiver", false);
    prefs.putBool("binary_leds", binary_leds);
  } else if (key == "channels" && value.toInt() >= 1 && value.toInt() <= (binary_leds ? 8 : 7)) {
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
    Serial.printf("radio: no \"+AT: OK\" from LoRa-E5 after 15 tries, last reply \"%s\" (%u chars)\r\n", r.c_str(),
                  r.length());
    return false;
  }

  r = sendAT("AT+MODE=TEST");
  if (r.indexOf("+MODE: TEST") < 0) {
    Serial.printf("radio: AT+MODE=TEST answered \"%s\"\r\n", r.c_str());
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
  Serial.println("Hej liski!");
  LisekOta::begin("receiver");
  Preferences prefs;
  prefs.begin("receiver", true);
  binary_leds = prefs.getBool("binary_leds", false);
  channels = constrain(prefs.getUChar("channels", 7), 1, 8);
  prefs.end();
  advertise_settings();
  LisekOta::onSetting(on_setting);
  Serial.println("firmware " LISEK_VERSION);

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

  Serial1.begin(MODULE_BAUD, SERIAL_8N1, MODULE_RX, MODULE_TX);
  delay(200);

  while (!set_transmitter_id(transmitter_id)) {
    Serial.println("radio init failed, retrying in 2s...");
    delay(2000);
  }
  LisekOta::markHealthy();
  Serial.println("into RX mode");
}

// While non-zero, the LEDs show the newly selected id until this millis() time
// (receiver.ino's 500 ms id flash, without blocking presses or the radio).
uint32_t id_flash_until = 0;
// millis() of the last packet from the selected transmitter, 0 = none shown.
uint32_t last_packet_ms = 0;

void change_transmitter_id_to(uint8_t id) {
  last_packet_ms = 0;  // the old transmitter's strength no longer applies
  // Feedback first: the id shows on the LEDs the moment the button goes down.
  display_bits_on_leds(id_bits(id));
  id_flash_until = millis() + 500;
  if (id_flash_until == 0) id_flash_until = 1;

  if (retune(id) || set_transmitter_id(id)) {
    Serial.printf("\r\nChanged transmitter id to %d\r\n", transmitter_id);
  } else {
    Serial.println("failed to change transmitter id");
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
  // longer allowed (fewer channels, 8 in unary) goes back to 1.
  if (settings_changed) {
    settings_changed = false;
    if (transmitter_id > max_id()) {
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
    display_strength_on_leds(total ? 1 + done * 6 / total : 0);
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
            Serial.println("WARN: packet of size 0 or 1 recieved");
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
              if (!id_flash_until && !LisekOta::updating()) display_strength_on_leds((110 + pendingRssi) / (110 / 7));
              Serial.printf("received packet \"%s\" with rssi=%d snr=%d length=%d packet_tx_id=%d\r\n", rxpacket,
                            pendingRssi, pendingSnr, size, rxpacket[1]);
            }
          }
        } else if (line.length()) {
          Serial.printf("module: %s\r\n", line.c_str());
        }
      }
      line = "";
    } else if (c != '\r') {
      line += c;
    }
  }
}
