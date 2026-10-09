// Fox (beacon) transmitter: ESP32 + HM-TRLR-D-TTL-868 LoRa module over UART.
// Reconstructed from the module's persisted settings, the original firmware's
// captured serial output and an old stash of the sketch - not the original
// source. Sync word CAFE<id>x4BABE matches sync_word[] in receiver.ino.
//   USB serial '0'-'9': set transmitter id      'w': save config to module flash
#include <Arduino.h>

#define CONFIG_PIN 14  // LOW = AT/config mode, HIGH = transparent mode
#define MODULE_RX 16
#define MODULE_TX 17
#define TX_POWER_LEVEL 0  // module scale: 0 = 20 dBm ... 7 = 2 dBm
#define LORA_SF 7
#define SYNC_WORD_LEN 8
#define PACKET_LEN 32
#define PACKET_INTERVAL_MS 100
#define STATUS_INTERVAL_MS 2000

int transmitter_id = 1;
String lastResponse;

// 0 on OK, -(n+1) on "ERROR:n", -4 on anything else
int sendAT(const char *cmd) {
  while (Serial1.available()) Serial1.read();
  Serial1.print(cmd);
  Serial1.print("\r\n");
  lastResponse = Serial1.readStringUntil('\n');
  lastResponse.trim();
  if (lastResponse == "OK") return 0;
  if (lastResponse.startsWith("ERROR:")) return -(lastResponse.substring(6).toInt() + 1);
  return -4;
}

String queryParam(const char *name) {
  char cmd[24];
  snprintf(cmd, sizeof cmd, "AT+%s=?", name);
  while (Serial1.available()) Serial1.read();
  Serial1.print(cmd);
  Serial1.print("\r\n");
  String r = Serial1.readStringUntil('\n');
  r.trim();
  int colon = r.indexOf(':');
  return (r.startsWith("ERROR") || colon < 0) ? String() : r.substring(colon + 1);
}

// Only writes to the module when it doesn't already report the wanted value.
void ensureParam(const char *name, long value) {
  String cur = queryParam(name);
  if (cur.length() && cur.toInt() == value) {
    Serial.printf("cfg %s=%ld ok\r\n", name, value);
    return;
  }
  char cmd[32];
  snprintf(cmd, sizeof cmd, "AT+%s=%ld", name, value);
  int res = sendAT(cmd);
  Serial.printf("cfg %s: was \"%s\", set to %ld -> %d (\"%s\")\r\n", name, cur.c_str(), value, res, lastResponse.c_str());
}

void enterConfig() {
  digitalWrite(CONFIG_PIN, LOW);
  delay(20);
}

void exitConfig() {
  digitalWrite(CONFIG_PIN, HIGH);
  delay(20);
}

bool moduleAnswers(unsigned long baud) {
  Serial1.end();
  Serial1.begin(baud, SERIAL_8N1, MODULE_RX, MODULE_TX);
  delay(50);
  return sendAT("AT") == 0;
}

int idFromSyncWord(const String &sw) {
  if (sw.length() >= 8 && sw.startsWith("CAFE")) return strtol(sw.substring(4, 6).c_str(), nullptr, 16);
  return -1;
}

void update_transmitter_id(int id) {
  char buf[20];
  snprintf(buf, sizeof buf, "CAFE%02X%02X%02X%02XBABE", id, id, id, id);
  enterConfig();
  int res = sendAT((String("AT+SYNW=") + buf).c_str());
  exitConfig();
  if (res != 0) {
    Serial.printf("set sync word failed: %d (module said \"%s\")\r\n", res, lastResponse.c_str());
    return;
  }
  transmitter_id = id;
  Serial.printf("updated transmitter id id=%d sync_word=%s\r\n", id, buf);
  if (id < 1 || id > 6) Serial.println("note: receiver.ino only tunes ids 1-6");
}

void save_to_flash() {
  enterConfig();
  int res = sendAT("AT&W");
  exitConfig();
  if (res != 0) {
    Serial.printf("save to flash failed: %d (module said \"%s\")\r\n", res, lastResponse.c_str());
    return;
  }
  Serial.printf("saved to flash\r\n");
}

void setup() {
  Serial.begin(115200);
  pinMode(CONFIG_PIN, OUTPUT);
  Serial.println("\r\n=== fox transmitter (reconstructed) ===");

  enterConfig();
  unsigned long baud = 0;
  const unsigned long candidates[] = { 115200, 9600 };
  while (!baud) {
    for (unsigned long b : candidates) {
      if (moduleAnswers(b)) {
        baud = b;
        break;
      }
    }
    if (!baud) {
      Serial.println("connection failed: no AT response at 115200 or 9600, retrying");
      delay(2000);
    }
  }
  Serial.printf("connection successful :) module answers at %lu baud\r\n", baud);

  if (baud != 115200) {
    int res = sendAT("AT+SPR=9");
    Serial1.updateBaudRate(115200);
    Serial.printf("cfg SPR: module was at %lu baud, set SPR=9 -> %d, switched to 115200\r\n", baud, res);
  } else {
    ensureParam("SPR", 9);
  }
  ensureParam("POWER", TX_POWER_LEVEL);
  ensureParam("SYNL", SYNC_WORD_LEN);
  ensureParam("LRSF", LORA_SF);
  ensureParam("LRPL", PACKET_LEN);

  String sw = queryParam("SYNW");
  int id = idFromSyncWord(sw);
  Serial.printf("stored sync word: \"%s\"\r\n", sw.c_str());
  Serial.printf("module ALL: %s\r\n", queryParam("ALL").c_str());
  exitConfig();

  if (id >= 0) {
    transmitter_id = id;
  } else {
    Serial.println("stored sync word is not CAFE<id>x4BABE, defaulting to id=1 and rewriting it");
    update_transmitter_id(1);
  }
  Serial.printf("transmitter_id=%d\r\n", transmitter_id);
}

void loop() {
  static uint32_t lastPacketMs = 0, lastStatusMs = 0, packets = 0;
  uint32_t now = millis();

  while (Serial.available()) {
    char c = Serial.read();
    if (c >= '0' && c <= '9') {
      update_transmitter_id(c - '0');
    } else if (c == 'w') {
      save_to_flash();
    }
  }

  if (now - lastPacketMs >= PACKET_INTERVAL_MS && Serial1.availableForWrite() >= PACKET_LEN) {
    uint8_t packet[PACKET_LEN];
    memset(packet, transmitter_id, sizeof packet);
    Serial1.write(packet, sizeof packet);
    packets++;
    lastPacketMs = now;
  }

  if (now - lastStatusMs >= STATUS_INTERVAL_MS) {
    Serial.printf("[%lu ms] id=%d sync=CAFE%02X%02X%02X%02XBABE packets_queued=%lu\r\n", (unsigned long)now,
                  transmitter_id, transmitter_id, transmitter_id, transmitter_id, transmitter_id, (unsigned long)packets);
    lastStatusMs = now;
  }
}
