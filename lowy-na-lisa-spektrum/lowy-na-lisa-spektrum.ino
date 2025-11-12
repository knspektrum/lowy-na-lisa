//#define SERVER

#include <SPI.h>
#ifdef ESP32
#define TMP_ESP32
#undef ESP32
#endif
//#define SPIClass SPIClassRP2040
#include "./nRF905.h"
#include "./nRF905_defs.h"
#ifdef TMP_ESP32
#define ESP32
#undef TMP_ESP32
#endif

#ifdef SERVER
#define RXADDR 0xDEADBEEF  // Address of this device
#define TXADDR 0xDEADBEEF  // Address of device to send to
#else
#define RXADDR 0xDEADBEEF  // Address of this device
#define TXADDR 0xDEADBEEF  // Address of device to send to
#endif

#define PACKET_NONE 0
#define PACKET_OK 1
#define PACKET_INVALID 2

#define PAYLOAD_SIZE NRF905_MAX_PAYLOAD

nRF905 transceiver = nRF905();

static volatile uint8_t packetStatus;

// Don't modify these 2 functions. They just pass the DR/AM interrupt to the correct nRF905 instance.
void nRF905_int_dr() {
  transceiver.interrupt_dr();
}
void nRF905_int_am() {
  transceiver.interrupt_am();
}

// Event function for RX complete
void nRF905_onRxComplete(nRF905* device) {
  packetStatus = PACKET_OK;
  transceiver.standby();
}

// Event function for RX invalid
void nRF905_onRxInvalid(nRF905* device) {
  packetStatus = PACKET_INVALID;
  transceiver.standby();
}



// ESP32 D1 R32 devkit configuration
// #define SS SS            // SPI SS (chip select)
#define CE GPIO_NUM_16   // CE (standby)
#define TRX GPIO_NUM_27  // TRX (RX/TX mode)
#define PWR GPIO_NUM_14  // PWR (power down)
#define CD GPIO_NUM_17   // CD (carrier detect / collision avoid)
#define DR GPIO_NUM_26   // DR (data ready)
#define AM GPIO_NUM_25   // AM (address match)


#define LED GPIO_NUM_12


void setup() {
  Serial.begin(115200);

  Serial.println("spi begin...");


  SPI.begin();

  Serial.println(F("ok"));

  Serial.println(F("transceiver begin..."));
  transceiver.begin(
    &SPI,           // SPI bus to use (SPI, SPI1, SPI2 etc)
    10000000,       // SPI Clock speed (10MHz)
    SS,             // SPI SS
    CE,             // CE (standby)
    TRX,            // TRX (RX/TX mode)
    PWR,            // PWR (power down)
    CD,             // CD (collision avoid)
    DR,             // DR (data ready)
    AM,             // AM (address match)
    nRF905_int_dr,  // Interrupt function for DR
    nRF905_int_am   // Interrupt function for AM
  );

  Serial.println(F("ok"));


  Serial.println(F("register events..."));
  // Register event functions
  transceiver.events(
    nRF905_onRxComplete,
    nRF905_onRxInvalid,
    NULL,
    NULL);
  Serial.println(F("ok"));


  transceiver.setTransmitPower(NRF905_PWR_10);

  uint8_t regs[NRF905_REGISTER_COUNT];
  Serial.println(F("REGS"));
  transceiver.getConfigRegisters(regs);
  for (uint8_t i = 0; i < NRF905_REGISTER_COUNT; i++)
    Serial.print(regs[i], HEX);
  Serial.println(F("\nREGS END"));

  Serial.println(F("set listen addr..."));
  // Set address of this device
  transceiver.setListenAddress(RXADDR);


  Serial.println(F("set band..."));
  transceiver.setBand(NRF905_BAND_868);

  Serial.println(F("REGS"));

  for (uint8_t i = 0; i < NRF905_REGISTER_COUNT; i++)
    Serial.print(regs[i], HEX);
  Serial.println(F("\nREGS END"));


  Serial.println(F("REGS"));
  transceiver.getConfigRegisters(regs);
  for (uint8_t i = 0; i < NRF905_REGISTER_COUNT; i++)
    Serial.print(regs[i], HEX);
  Serial.println(F("\nREGS END"));

#ifdef SERVER
  Serial.println(F("enable rx mode..."));
  transceiver.RX();
  Serial.println(F("ok"));
#endif

#ifdef SERVER
  Serial.println(F("server started"));
#else
  Serial.println(F("client started"));
#endif
}


#ifdef SERVER
void loop() {

  static uint32_t pings;
  static uint32_t invalids;
  static uint32_t badData;

  Serial.println(F("Waiting for ping..."));

  // Wait for data
  while (packetStatus == PACKET_NONE) {
    // #ifdef ESP32
    //     transceiver.poll();
    // #endif
    // pinMode(LED, OUTPUT);
    // digitalWrite(LED, digitalRead(LED) ? LOW : HIGH);
    // delay(100);
  }

  if (packetStatus != PACKET_OK) {
    invalids++;
    packetStatus = PACKET_NONE;
    Serial.println(F("Invalid packet!"));
    transceiver.RX();
  } else {
    pings++;
    packetStatus = PACKET_NONE;

    Serial.println(F("Got ping"));

    // Toggle LED
    digitalWrite(LED, digitalRead(LED) ? LOW : HIGH);

    // Make buffer for data
    uint8_t buffer[PAYLOAD_SIZE];

    // Read payload
    transceiver.read(buffer, sizeof(buffer));

    // Copy data into new buffer for modifying
    uint8_t replyBuffer[PAYLOAD_SIZE];
    memcpy(replyBuffer, buffer, PAYLOAD_SIZE);

    // Validate data and modify
    // Each byte ofthe payload should be the same value, increment this value and send back to the client
    bool dataIsBad = false;
    uint8_t value = replyBuffer[0];
    for (uint8_t i = 0; i < PAYLOAD_SIZE; i++) {
      if (replyBuffer[i] == value)
        replyBuffer[i]++;
      else {
        badData++;
        dataIsBad = true;
        break;
      }
    }

    // Write reply data and destination address to radio
    transceiver.write(TXADDR, replyBuffer, sizeof(replyBuffer));

    // Send the reply data, once the transmission has completed go into receive mode
    while (!transceiver.TX(NRF905_NEXTMODE_RX, true));

    Serial.println(F("Reply sent"));

    if (dataIsBad)
      Serial.println(F("Received data was bad!"));

    // Show received data
    Serial.print(F("Data from client:"));
    for (uint8_t i = 0; i < PAYLOAD_SIZE; i++) {
      Serial.print(F(" "));
      Serial.print(buffer[i], DEC);
    }
    Serial.println();

    // Show sent data
    Serial.print(F("Reply data:"));
    for (uint8_t i = 0; i < PAYLOAD_SIZE; i++) {
      Serial.print(F(" "));
      Serial.print(replyBuffer[i], DEC);
    }

    Serial.println(F("Totals:"));
    Serial.print(F(" Pings   "));
    Serial.println(pings);
    Serial.print(F(" Invalid "));
    Serial.println(invalids);
    Serial.print(F(" Bad     "));
    Serial.println(badData);
    Serial.println(F("------"));
  }
}
#else

#define TIMEOUT 500 * 1000  // 500ms ping timeout

void loop() {
  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);
  delay(500);
  pinMode(LED, OUTPUT);
  digitalWrite(LED, HIGH);
  delay(500);
  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);
  delay(500);

  static uint8_t counter;
  static uint32_t sent;
  static uint32_t replies;
  static uint32_t timeouts;
  static uint32_t invalids;
  static uint32_t badData;

  // Make data
  uint8_t buffer[PAYLOAD_SIZE];
  memset(buffer, counter, PAYLOAD_SIZE);
  counter++;

  packetStatus = PACKET_NONE;

  // Show data
  Serial.print(F("Sending data: "));
  for (uint8_t i = 0; i < PAYLOAD_SIZE; i++) {
    Serial.print(F(" "));
    Serial.print(buffer[i], DEC);
  }
  Serial.println();

  // Write reply data and destination address to radio IC
  transceiver.write(TXADDR, buffer, sizeof(buffer));

  uint32_t startTime = micros();

  // Send the data (send fails if other transmissions are going on, keep trying until success) and enter RX mode on completion
  while (!transceiver.TX(NRF905_NEXTMODE_RX, true)) {
    pinMode(LED, OUTPUT);
    digitalWrite(LED, digitalRead(LED) ? LOW : HIGH);
    delay(100);
  }
  sent++;

  Serial.println(F("Waiting for reply..."));

  uint8_t success;

  // Wait for reply with timeout
  unsigned long sendStartTime = micros();
  while (1) {
    // Uncomment this line if the library is running in polling mode
    //		transceiver.poll();

    success = packetStatus;
    if (success != PACKET_NONE)
      break;
    else if (micros() - sendStartTime > TIMEOUT)
      break;
  }

  if (success == PACKET_NONE) {
    Serial.println(F("Ping timed out"));
    timeouts++;
  } else if (success == PACKET_INVALID) {
    Serial.println(F("Invalid packet!"));
    invalids++;
  } else {
    unsigned long totalTime = micros() - startTime;
    replies++;

    // If success toggle LED and send ping time over serial
    digitalWrite(LED, digitalRead(LED) ? LOW : HIGH);

    Serial.print(F("Ping time: "));
    Serial.print(totalTime);
    Serial.println(F("micros"));

    // Get the reply data
    uint8_t replyBuffer[PAYLOAD_SIZE];
    transceiver.read(replyBuffer, sizeof(replyBuffer));

    // Validate data
    for (uint8_t i = 0; i < PAYLOAD_SIZE; i++) {
      if (replyBuffer[i] != counter) {
        badData++;
        Serial.println(F("Bad data!"));
        break;
      }
    }

    // Print out ping contents
    Serial.print(F("Data from server:"));
    for (uint8_t i = 0; i < PAYLOAD_SIZE; i++) {
      Serial.print(F(" "));
      Serial.print(replyBuffer[i], DEC);
    }
    Serial.println();
  }

  Serial.println(F("Totals:"));
  Serial.print(F(" Sent     "));
  Serial.println(sent);
  Serial.print(F(" Replies  "));
  Serial.println(replies);
  Serial.print(F(" Timeouts "));
  Serial.println(timeouts);
  Serial.print(F(" Invalid  "));
  Serial.println(invalids);
  Serial.print(F(" Bad      "));
  Serial.println(badData);
  Serial.println(F("------"));
}
#endif
