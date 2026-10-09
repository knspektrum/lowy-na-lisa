# Nadajnik ("lis")

Nadajnik wysyła co 100 ms pakiet LoRa ze swoim numerem (id 1-8). Odbiorniki ustawione na ten sam numer pokazują siłę jego sygnału na diodach.

![Schemat połączeń](docs/fritzing/fox-transmitter_breadboard.png)

## Hardware
- **MCU:** WeMos D1 R32 (ESP32, format Arduino Uno)
- **Radio:** HopeRF HM-TRLR-D-TTL-868 (LoRa SF7, 125 kHz, 20 dBm, pakiety po 32 bajty)
- **Zasilanie:** 5x AA (ok. 7.5 V) na VIN i GND

## Połączenia

D1 R32  |  HM-TRLR-D  |  Opis
:-------------------------:|:-------------------------:|:-------------------------
IO17 (TX)  |  RXD  |  komendy AT i dane do modułu
IO16 (RX)  |  TXD  |  odpowiedzi modułu
IO14  |  CONFIG  |  LOW = tryb konfiguracji (AT), HIGH = nadawanie
5V  |  5V  |  moduł przyjmuje 3.3-5.5 V
GND (górny, obok IO18)  |  GND (górny)  |
GND (dolny)  |  SLEEP  |  SLEEP na GND = moduł nie zasypia

D1 R32  |  Baterie 5x AA  |  Opis
:-------------------------:|:-------------------------:|:-------------------------
VIN  |  +  |  ok. 7.5 V (nowe baterie do ok. 8 V)
GND (obok VIN)  |  -  |

Piny modułu od góry: STATUS, CONFIG, GND, 5V, RXD, TXD, GND, A_TX, B_RX, SLEEP, RESET. Na górze modułu jest gniazdo antenowe SMA (żeńskie). STATUS, drugi GND, A_TX, B_RX i RESET nie są podłączone.

## Kanały

Numer nadajnika (id) wyznacza kanał radiowy i zawartość pakietów:
- **Kanał:** id - 1, ustawiany tylko zworkami CH_1-CH_4 na module (żadna komenda AT go nie ustawia ani nie odczytuje)
- **Częstotliwość:** 863.0 MHz + 0.5 MHz × kanał
- **Sync word:** `CAFE` + 4× id + `BABE`, zapisany w module
- **Pakiet:** 32 bajty, każdy równy id

Id  |  Kanał  |  Częstotliwość  |  Sync word  |  Odbiornik (od id 1)
:-------------------------:|:-------------------------:|:-------------------------:|:-------------------------:|:-------------------------:
1  |  0  |  863.0 MHz  |  `CAFE01010101BABE`  |  0 naciśnięć albo `1`
2  |  1  |  863.5 MHz  |  `CAFE02020202BABE`  |  1 naciśnięcie albo `2`
3  |  2  |  864.0 MHz  |  `CAFE03030303BABE`  |  2 naciśnięcia albo `3`
4  |  3  |  864.5 MHz  |  `CAFE04040404BABE`  |  3 naciśnięcia albo `4`
5  |  4  |  865.0 MHz  |  `CAFE05050505BABE`  |  4 naciśnięcia albo `5`
6  |  5  |  865.5 MHz  |  `CAFE06060606BABE`  |  5 naciśnięć albo `6`
7  |  6  |  866.0 MHz  |  `CAFE07070707BABE`  |  6 naciśnięć albo `7`
8  |  7  |  866.5 MHz  |  `CAFE08080808BABE`  |  7 naciśnięć albo `8`, tylko diody binarne

Odbiornik startuje na id 1, każde naciśnięcie przycisku przechodzi na następny numer, a po ostatnim wraca do 1. Ile jest numerów (domyślnie 7, z diodami binarnymi do 8) i jak diody je pokazują, ustawia się tylko na [stronie floty](../ota/README.md), więc nie da się tego zmienić przypadkiem; szczegóły w [software/led-receiver](../led-receiver/README.md#ustawienia). Zamiast przycisku można wpisać cyfrę w monitorze portu szeregowego odbiornika.

### Zworki

Zworki od góry do dołu, ● = założona, ○ = brak (kanał zapisany binarnie: CH_1 = 1, CH_2 = 2, CH_3 = 4, CH_4 = 8):

Zworka  |  Id 1  |  Id 2  |  Id 3  |  Id 4  |  Id 5  |  Id 6  |  Id 7  |  Id 8
:-------------------------:|:-------------------------:|:-------------------------:|:-------------------------:|:-------------------------:|:-------------------------:|:-------------------------:|:-------------------------:|:-------------------------:
CH_1  |  ○  |  ●  |  ○  |  ●  |  ○  |  ●  |  ○  |  ●
CH_2  |  ○  |  ○  |  ●  |  ●  |  ○  |  ○  |  ●  |  ●
CH_3  |  ○  |  ○  |  ○  |  ○  |  ●  |  ●  |  ●  |  ●
CH_4  |  ○  |  ○  |  ○  |  ○  |  ○  |  ○  |  ○  |  ○

## Bluetooth

Lis nadaje przez Bluetooth na [stronę](../ota/README.md) swój kanał, wersję programu i stan. Telefon z aplikacją do Bluetooth też go widzi i mógłby po sile sygnału namierzyć lisa, więc w **Ustawieniach** lisa na stronie można włączyć ukrywanie: lis milknie 10, 30 albo 60 minut po włączeniu. Domyślnie nie ukrywa się nigdy. Ukryty lis jest na stronie „wyłączony albo ukryty”; żeby go sprawdzić albo zaktualizować, trzeba go wyłączyć i włączyć. Lis nie milknie, dopóki strona jest z nim połączona albo trwa aktualizacja.

## Programowanie

1. Ustaw zworki CH_1-CH_4 dla wybranego id (tabela wyżej).
2. Podłącz D1 R32 przez USB i wgraj program (następne wersje można wgrywać przez Bluetooth, [software/ota](../ota/README.md)):
   ```bash
   software/ota/build.sh
   software/ota/usb-flash.sh transmitter /dev/ttyUSB0
   ```
3. Otwórz monitor portu szeregowego (115200 baud) i wpisz cyfrę id (1-8). Nadajnik odpowiada `updated transmitter id id=N sync_word=CAFE...BABE`.
4. Wpisz `w`, żeby zapisać ustawienia w module (`saved to flash`). Po każdym starcie program odczytuje sync word z modułu i używa zapisanego id.
5. Sprawdź na odbiorniku ustawionym na to samo id: diody pokazują siłę sygnału, a w monitorze odbiornika pojawia się `received packet ... packet_tx_id=N`.

## Pliki

- `fox-transmitter.ino` - program nadajnika (aktualizacje przez Bluetooth: biblioteka `software/libraries/LisekOta`)
- `docs/fritzing/` - schemat połączeń we Fritzingu (`fox-transmitter.fzz`), generowany przez `make_fzz.py`, eksport `build.sh`
- Płytka WeMos D1 R32 we Fritzingu: część autorstwa Petera Van Eppa ([forum Fritzing](https://forum.fritzing.org/t/looking-for-wemos-d1-r32-esp32-uno/14487)); moduł HM-TRLR-D jest narysowany jako ogólny układ SIP z pinami w tej samej kolejności co na module, gniazdo SMA to część "SMA Antenna Connector" Petera Van Eppa ([forum Fritzing](https://forum.fritzing.org/t/sma-female-connector-part/4688)), baterie jako 5 ogniw AA z biblioteki Fritzinga połączonych szeregowo
