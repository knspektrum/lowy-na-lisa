#include "LoRaWan_APP.h"
#include "sx126x.h"
#include "Arduino.h"
#include "CubeCell_NeoPixel.h"
#include <Wire.h>
#include "HT_SSD1306Wire.h" 



#define BASE_RF_FREQUENCY 863000000  // Hz
#define FREQUENCY_STEP 500000

#define TX_OUTPUT_POWER 14  // dBm

#define LORA_BANDWIDTH 0         // [0: 125 kHz, \
                                 //  1: 250 kHz, \
                                 //  2: 500 kHz, \
                                 //  3: Reserved]
#define LORA_SPREADING_FACTOR 7  // [SF7..SF12]
#define LORA_CODINGRATE 1        // [1: 4/5, \
                                 //  2: 4/6, \
                                 //  3: 4/7, \
                                 //  4: 4/8]
#define LORA_PREAMBLE_LENGTH 8   // Same for Tx and Rx
#define LORA_SYMBOL_TIMEOUT 0    // Symbols
#define LORA_FIX_LENGTH_PAYLOAD_ON false
#define LORA_IQ_INVERSION_ON false


#define RX_TIMEOUT_VALUE 1000
#define BUFFER_SIZE 30  // Define the payload size here

#define LED_BOOST_ON GPIO1
#define LED_DATA GPIO2
#define SW1 GPIO0
#define SW2 GPIO6
#define SW3 GPIO7
#define BAT_LVL ADC

SSD1306Wire  display(0x3c, 500000, SDA, SCL, GEOMETRY_128_64, NULL); // addr , freq , SDA, SCL, resolution , rst
CubeCell_NeoPixel pixels(7, LED_DATA, NEO_GRB + NEO_KHZ800);



char txpacket[BUFFER_SIZE];
char rxpacket[BUFFER_SIZE];

static RadioEvents_t RadioEvents;
static TimerEvent_t sleep;

int16_t rssi, rxSize;

bool lora_idle = true;
bool powered_on = false;

uint8_t sync_word[8] = { 0xCA, 0xFE, 0x00, 0x00, 0x00, 0x00, 0xBA, 0xBE };


void setup() {
  Serial.begin(115200);
  Serial.println("Hej liski!");

  pinMode(SW1, INPUT_PULLUP);

  pinMode(LED_BOOST_ON,OUTPUT);
  digitalWrite(LED_BOOST_ON,HIGH);
  pixels.begin();
  pixels.clear();

  for (int i = 0; i < 2; i++) {
    for (int j = 0; j < 7; j++) {
      display_bits_on_leds(to_first_led_bits(j));
      delay(50);
    }
    for (int j = 0; j < 7; j++) {
      display_bits_on_leds(to_last_led_bits(8 - j));
      delay(50);
    }
  }
  // Show initial display buffer contents on the screen --
  // the library initializes this with an Adafruit splash screen.
  display.display();
  delay(2000); // Pause for 2 seconds

  // Clear the buffer
  display.clear();

  // Draw a single pixel in white
  display.setPixel(10, 10);

  // Show the display buffer on the screen. You MUST call display() after
  // drawing commands to make them visible on screen!
  display.display();

  display_bits_on_leds(0);

  rssi = 0;

  RadioEvents.RxDone = on_rx_done;
  Radio.Init(&RadioEvents);
  Radio.SetChannel(BASE_RF_FREQUENCY);



  Radio.SetRxConfig(MODEM_LORA, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                    LORA_CODINGRATE, 0, LORA_PREAMBLE_LENGTH,
                    LORA_SYMBOL_TIMEOUT, LORA_FIX_LENGTH_PAYLOAD_ON,
                    0, true, 0, 0, LORA_IQ_INVERSION_ON, true);

  set_transmitter_id(1);
}


void loop() {
  if (powered_on && digitalRead(SW1) == LOW) {
    detachInterrupt(SW1);
    handle_button_press();
  }

  if (!powered_on) {
    
    sx126xSleep();
    while (digitalRead(SW1) == LOW)
      ;
    delay(50); // debounce
    attachInterrupt(SW1, on_wake, FALLING);
    Serial.println();
    Serial.flush();
    lowPowerHandler();
  }

  if (powered_on && lora_idle) {
    lora_idle = false;
    Serial.println("into RX mode");
    Radio.Rx(0);
  }
}

void on_rx_done(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {
  if (size < 2) {
    Serial.println("WARN: packet of size 0 or 1 recieved");
    Radio.Sleep();
    lora_idle = true;
    return;
  }

  if ((payload[0] != get_transmitter_id()) || (payload[1] != get_transmitter_id())) {
    Radio.Sleep();
    lora_idle = true;
    return;
  }

  if (size > 29)
    size = 29;

  memcpy(rxpacket, payload, size);
  rxpacket[size] = '\0';

  rssi = rssi;
  rxSize = size;
  display_strength_on_leds((110 + rssi) / (110 / 7));
  Serial.printf("received packet \"%s\" with rssi=%d snr=%d length=%d packet_tx_id=%d\r\n", rxpacket, rssi, snr, rxSize, rxpacket[1]);


  Radio.Sleep();
  lora_idle = true;
}

void handle_button_press() {
  int press_start = millis();
  delay(50);  // debounce
  if (digitalRead(SW1) == LOW) {
    int i = 1;
    
    while (digitalRead(SW1) == LOW) {
      if (millis() - press_start > 2500) {  // if pressed for longer than 2.5 seconds
        powered_on = false;
        i = (i + 1) % 8;
        display_bits_on_leds(1 << i);
        delay(30);
      }

      delay(5);
    };

    if (!powered_on) {
      display_bits_on_leds(0);
      Serial.println("powered off");
      for (int j = 0; j < 8; j++) {
        display_bits_on_leds(to_first_led_bits((7-j)));
        delay(300);
      }
      return;
    }

    set_transmitter_id(1 + (get_transmitter_id() % 6));
    Serial.printf("\r\nChanged transmitter id to %d\r\n", get_transmitter_id());

    Radio.Sleep();
    lora_idle = true;

    display_bits_on_leds(to_first_led_bits(get_transmitter_id()));
    delay(500);
    display_bits_on_leds(0);
  }
}

void on_wake() {
  if (!powered_on) {
    powered_on = true;

    Serial.println("woke up!");
    while (digitalRead(SW1) == LOW)
      ;
  }
}

void set_transmitter_id(uint8_t id) {
  for (int i = 0; i < 4; i++) {
    sync_word[i + 2] = id;
  }

  SX126xSetSyncWord(sync_word);
  Radio.SetChannel(BASE_RF_FREQUENCY+(id-1)*FREQUENCY_STEP);
}

uint8_t get_transmitter_id() {
  return sync_word[2];
}

void display_bits_on_leds(int8_t bits) {
  for (int i = 0; i < 7; i++) {
    uint32_t color;

    if ((bits & (1 << i)) > 0) {
      color = pixels.Color(20, 20, 20);
    }else {
      color = pixels.Color(0, 0, 0);
    }
    
    pixels.setPixelColor(i, color);
  }
  pixels.show();
}

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

void display_strength_on_leds(int8_t strength) {
  display_bits_on_leds(to_last_led_bits(strength));
}
