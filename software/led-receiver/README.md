# Odbiornik z diodami

Odbiornik nasłuchuje nadajnika o wybranym numerze (id 1-8) i pokazuje siłę jego sygnału na 7 diodach. Działa tak samo jak `software/receiver/receiver.ino`, tylko na ESP32 z modułem Seeed LoRa-E5 sterowanym komendami AT.

![Schemat połączeń](docs/fritzing/led-receiver_breadboard.png)

## Hardware
- **MCU:** ESP32 DevKit (38 pinów, wersja szeroka)
- **Radio:** Seeed LoRa-E5 (STM32WLE5JC, firmware AT, UART 9600 baud)
- **LEDy:** 7x 5 mm: 2 czerwone, 3 żółte, 2 zielone
- **Przycisk:** zmiana numeru nadajnika
- **Zasilanie:** Bateria LiPo + płytka z ładowaniem USB-C i przetwornicą 5 V, wyłącznik na plusie

## Połączenia

ESP32  |  Element  |  Opis
:-------------------------:|:-------------------------:|:-------------------------
G33  |  LED1 (czerwona)  |  najsłabszy sygnał, rezystor 160 Ω
G25  |  LED2 (czerwona)  |  rezystor 160 Ω
G26  |  LED3 (żółta)  |  rezystor 160 Ω
G27  |  LED4 (żółta)  |  rezystor 160 Ω
G14  |  LED5 (żółta)  |  rezystor 160 Ω
G12  |  LED6 (zielona)  |  rezystor 30 Ω
G13  |  LED7 (zielona)  |  najsilniejszy sygnał, rezystor 30 Ω
G32  |  przycisk  |  drugi pin do GND (wewnętrzny pull-up)
G17 (TX2)  |  LoRa-E5 RX (pad 9, PB7)  |  komendy AT
G16 (RX2)  |  LoRa-E5 TX (pad 10, PB6)  |  odpowiedzi i odebrane pakiety
3V3  |  LoRa-E5 VCC (pad 1)  |
GND  |  LoRa-E5 GND (pad 2), katody LED, przycisk  |
V5  |  wyłącznik → wyjście 5 V przetwornicy  |  bateria → ładowarka/przetwornica → wyłącznik → V5

Każda dioda: pin → rezystor → anoda, katoda → GND. Antena do RFIO modułu (pad 15).

## Obsługa

- Po starcie odbiornik słucha nadajnika 1 (863.0 MHz)
- Krótkie naciśnięcie przycisku: następny nadajnik (po ostatnim kanale wraca do 1); przez 0.5 s diody pokazują nowe id
- Cyfra w monitorze portu szeregowego (115200 baud) robi to samo
- Liczba zapalonych diod (od LED1) = siła sygnału: (110 + RSSI) / 15
- Brak pakietu od wybranego nadajnika przez 1 s: diody gasną (nadajnik wysyła co 100 ms)
- Aktualizacja przez Bluetooth: diody zapalają się kolejno w miarę wgrywania

Kanały i ustawianie nadajników: [software/fox-transmitter](../fox-transmitter/README.md).

### Ustawienia

Ustawia się je tylko w **Ustawieniach** odbiornika na [stronie floty](../ota/README.md), więc nie da się ich zmienić przypadkiem na samym odbiorniku. Odbiornik pamięta je po wyłączeniu.

- **Liczba kanałów:** przez ile numerów nadajników przechodzi przycisk, domyślnie 7 (1-7)
- **Diody zwykłe** (domyślnie): zapala się tyle diod od LED7 w dół, ile wynosi id, więc najwyżej 7 kanałów
- **Diody binarne:** id zapisane dwójkowo, do 8 kanałów, ● = świeci

Id  |  LED4 (8)  |  LED5 (4)  |  LED6 (2)  |  LED7 (1)
:-------------------------:|:-------------------------:|:-------------------------:|:-------------------------:|:-------------------------:
1  |  ○  |  ○  |  ○  |  ●
2  |  ○  |  ○  |  ●  |  ○
3  |  ○  |  ○  |  ●  |  ●
4  |  ○  |  ●  |  ○  |  ○
5  |  ○  |  ●  |  ○  |  ●
6  |  ○  |  ●  |  ●  |  ○
7  |  ○  |  ●  |  ●  |  ●
8  |  ●  |  ○  |  ○  |  ○

Jeśli wybrane id nie mieści się w nowych ustawieniach (mniej kanałów, id 8 przy zwykłych diodach), odbiornik wraca do id 1.

## Programowanie

Pierwszy raz przez USB, potem przez Bluetooth ze strony floty: [software/ota](../ota/README.md).

```bash
software/ota/build.sh
software/ota/usb-flash.sh receiver /dev/ttyUSB0
```

## Pliki

- `led-receiver.ino` - program odbiornika (aktualizacje przez Bluetooth: biblioteka `software/libraries/LisekOta`)
- `docs/fritzing/` - schemat połączeń we Fritzingu (`led-receiver.fzz`), generowany przez `make_fzz.py` (wartości rezystorów: `R_RED_YELLOW`, `R_GREEN`), eksport `build.sh`
- Płytka ESP32 we Fritzingu: część `ESP32-38PinWide-fixed` autorstwa Thomasa Plunketta ([forum Fritzing](https://forum.fritzing.org/t/esp32-wroom-32u/12978)), z poprawionym opisem pinu CMD obok V5; LoRa-E5 jest narysowany jako ogólny układ SIP (piny od góry: TX, RX, GND, VCC), ładowarka jako Adafruit bq25185 + 5V boost
- Wspólne skrypty Fritzinga: `software/fritzing-lib/` (`make_fzz.py` sprawdza, czy przewody się nie krzyżują ani nie nakładają; masa może przechodzić pod przewodami diod)
