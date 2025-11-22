#define CONFIG_PIN 14

#pragma once
#include <Arduino.h>

class HopeRF {
public:
  HopeRF(HardwareSerial &serial, int configPin)
    : serial(serial), configPin(configPin) {}

  void begin(unsigned long baud = 9600) {
    pinMode(configPin, OUTPUT);
    digitalWrite(configPin, HIGH);  // normal mode
    serial.begin(baud, SERIAL_8N1, 16, 17);
  }

  void enterConfig() {
    digitalWrite(configPin, LOW);
    delay(20);
  }

  void exitConfig() {
    digitalWrite(configPin, HIGH);
    delay(20);
  }

  int sendCommand(const char *cmd) {
    serial.print(cmd);
    serial.print("\r\n");
    return readResponse();
  }

  int readResponse() {
    String res = serial.readStringUntil('\n');
    res.trim();

    if (res == "OK") return 0;
    if (res == "ERROR:0") return -1;
    if (res == "ERROR:1") return -2;
    if (res == "ERROR:2") return -3;

    return -4;  // unknown response
  }

  int testConnection() {
    return sendCommand("AT");
  }

  int setPower(int level) {
    // level 0–7 (0=20dBm ... 7=2dBm)
    char buf[20];
    sprintf(buf, "AT+POWER=%d", level);
    return sendCommand(buf);
  }

  int setBaudRate(int n) {
    // 0–9 according to datasheet
    char buf[20];
    sprintf(buf, "AT+SPR=%d", n);
    return sendCommand(buf);
  }

  int setSyncWord(const char *hexString) {
    // hex string, up to 16 chars
    char buf[40];
    sprintf(buf, "AT+SYNW=%s", hexString);
    return sendCommand(buf);
  }

  int setSyncWordLength(int n) {
    char buf[20];
    sprintf(buf, "AT+SYNL=%d", n);
    return sendCommand(buf);
  }

  int setMode(int mode) {
    // 0=LoRa, 1=OOK, 2=FSK, 3=GFSK
    char buf[20];
    sprintf(buf, "AT+MODE=%d", mode);
    return sendCommand(buf);
  }

  int setBand(int band) {
    // 0=433, 1=470, 2=868, 3=915
    char buf[20];
    sprintf(buf, "AT+BAND=%d", band);
    return sendCommand(buf);
  }

  int saveToFlash() {
    return sendCommand("AT&W");
  }

  int readValue() {
    String res = serial.readStringUntil('\n');
    res.trim();

    if (res.startsWith("ERROR")) return -1000;

    int colon = res.indexOf(':');
    if (colon < 0) return -1001;

    String val = res.substring(colon + 1);
    val.trim();
    return val.toInt();
  }

  int query(const char *cmd) {
    char buf[30];
    sprintf(buf, "AT+%s=?", cmd);

    serial.print(buf);
    serial.print("\r\n");

    return readValue();
  }


  int readPower() {
    return query("POWER");
  }

  int readBaudRate() {
    return query("SPR");
  }

  int readSyncWordLength() {
    return query("SYNL");
  }

  int readMode() {
    return query("MODE");
  }

  int readBand() {
    return query("BAND");
  }


  HardwareSerial &serial;
private:
  int configPin;
};


HopeRF radio(Serial1, CONFIG_PIN);

void setup() {
  Serial.begin(115200);
  radio.begin(9600);

  delay(50);  // 12ms init

  radio.enterConfig();
  int res;

  res = radio.testConnection();
  if (res != 0) {
    Serial.printf("connection failed: %d\n", res);
    while (1);
  }

  Serial.println("connection successful :)");


  res = radio.setPower(0);
  if (res != 0) {
    Serial.printf("set power failed: %d\n", res);
    while (1);
  }

  Serial.println("set power successful :)");

  Serial.printf("readPower: %d\r\nreadBaudRate: %d\r\nreadSyncWordLength: %d\r\nreadMode: %d\r\nreadBand: %d\r\n", radio.readPower(), radio.readBaudRate(), radio.readSyncWordLength(), radio.readMode(), radio.readBand());

  // radio.exitConfig();
}

const char *commands_to_try[] = {
  "SPR",
  "HELP",
  "VER",
  "VERSION",
  "INFO",
  "STAT",
  "SYS",

  // ----- RSSI / SNR / Link Quality -----
  "RSSI",
  "CSQ",
  "LST_RSSI",
  "RX_RSSI",
  "PKT_RSSI",
  "SNR",
  "LQ",
  "NOISE",
  "SIG",
  "RX_QUALITY",
  "RX_LEVEL",

  // ----- Receive / TX state -----
  "RX",
  "RX_MODE",
  "RX_STATE",
  "RX_PARAM",
  "RX_TIMEOUT",
  "RXCOUNT",

  // ----- Frequency -----
  "FREQ",
  "FREQ",
  "FREQ=868000000",
  "FREQ=915000000",

  // ----- Power -----
  "POWER",
  "POW",
  "TXP",
  "TXPOWER",
  "PWR",

  // ----- Bandwidth -----
  "BW",
  "BANDWIDTH",

  // ----- Spreading Factor -----
  "SF",
  "SPREAD",

  // ----- Coding Rate -----
  "CR",
  "CODING",

  // ----- UART -----
  "UART",
  "BAUD",
  "PARITY",
  "STOPBIT",

  // ----- Modulation -----
  "MOD",
  "MODULATION",
  "LORA",
  "FSK",
  "OOK",

  // ----- Generic Radio -----
  "RADIO",
  "RADIO_STAT",
  "RADIO_MODE",
  "CHANNEL",
  "CHAN",
  "RF",
  "RF_PARAM",

  // ----- Packet Size / Payload -----
  "PKT",
  "PAYLOAD",
  "LEN",
  "SIZE",

  // ----- Addresses -----
  "ADDR",
  "ADDRESS",
  "SRC",
  "DST",
  "SRCADDR",
  "DSTADDR",
  "NETID",

  // ----- Device / Module Info -----
  "ID",
  "MODEL",
  "MANUF",
  "HWVER",
  "FWVER",
  "SERIAL",

  // ----- Temperature / Voltage -----
  "TEMP",
  "BAT",
  "VOLT",
  "ADC",

  // ----- Ping / Echo -----
  "PING",
  "PING",
  "ECHO",
  "ECHO=0",
  "ECHO=1",

  // ----- Sleep / Power saving -----
  "SLEEP",
  "SLEEP=0",
  "SLEEP=1",
  "LOWPOWER",

  // ----- Network / Mesh -----
  "JOIN",
  "JOIN",
  "ROLE",
  "ROUTE",

  // ----- LoRaWAN-like -----
  "DEVADDR",
  "DEVEUI",
  "APPEUI",
  "APPKEY",

  // ----- Hidden vendor commands -----
  "TEST",
  "TEST",
  "DEBUG",
  "DEBUG",
  "FACTORY",
  "FACTORY",
  "RESTORE",
  "RESET",

  // ----- Bootloader / diagnostics -----
  "BOOT",
  "DIAG",
  "MEM",
  "FLASH",
  "EEPROM",

  // ----- Encryption -----
  "AES",
  "KEY",

  // ----- Short unknown probes -----
  "R",
  "S",
  "G",
  "X",
  "Y",

  // End
  NULL,
};

const char **cmd_ptr = commands_to_try;

void loop() {
  if (*cmd_ptr != NULL) {
    char buf[30];
    sprintf(buf, "AT");

    radio.serial.print(buf);
    radio.serial.print("\r\n");
    Serial.print(buf);
    Serial.print("\r\n");

    cmd_ptr++;

    unsigned long start = millis();
    while (millis() - start < 1500) {
      while (radio.serial.available() > 0) {
        Serial.write(radio.serial.read());
      }

      while (Serial.available() > 0) {
        radio.serial.write(Serial.read());
      }
      delay(1);
    }
  }
}
