# Łowy na Lisa

Gra terenowa dla dzieci oparta na LoRa - polega na znalezieniu ukrytych nadajnikiów ("lisów") wykorzystując siłę sygnału radiowego

## Zdjęcia

### Schemat PCB
![Schemat PCB](pcb/images/lowy-na-lisa.svg)

### Plot PCB
![Plot PCB](pcb/images/lowy-na-lisa-F_Cu.svg)

### 3D render PCB
![3D render PCB](pcb/images/lowy-na-lisa-3d.jpg)

## O projekcie

"Łowy na Lisa" to otwarty projekt hardware'owy tworzący ręczne urządzenia do polowania na nadajniki LoRa. Każdy uczestnik otrzymuje kompaktowe urządzenie wyposażone w:

- Wyświetlacz OLED z animowaną mascotką lisa (szczęśliwy smutny w zależności od siły sygnału)
- Pasek 7 diod WS2812B pokazujących kierunek i siłę sygnału
- 4 przyciski do obsługi menu
- Obsługę baterii LiPo z ładowaniem przez USB-C
- Tryb głębokiego snu dla długiej pracy na baterii

### Hardware
- **MCU:** CubeCell HTCC-AM01
- **LEDy:** 7x WS2812B NeoPixel (adresowalne)
- **Wyświetlacz:** 0.96" OLED I2C
- **Przyciski:** 4x na boku
- **Zasilanie:** Bateria LiPo ~500mAh + ładowarka TP4054
- **USB:** USB-C z konwerterem CH340K (programowanie)
- **Antena:** 868MHz, Własnoręcznie stworzona przez nas antena yagi (wczesniej zewnętrzna antena yagi telewizyjna) 
- **Wymiary:** Podłużna obudowa przyjazna dla dzieci

## PCB

Projekt PCB znajduje się w katalogu `pcb/` i jest przygotowany pod produkcję w JLCPCB (SMT assembly).

## Historia projektu

### Faza 1: Eksperymenty
- Testy modułów nRF905 - działają ale brak RSSI
- Przejście na moduły HM-TRLR-D-TTL-868 (LoRa/FSK/GFSK + RSSI)
- Brute-forceowanie komend AT modemu LoRa

### Faza 2: Targi Hobby
- Pierwsza działająca wersja
- Testy z dziećmi - obudowa sprawdza się, anteny za duże
- Problem: nadajniki się zakłócały (rozwiązane: różne kanały)
- Pozytywne wrażenia: światełka wzbudzają zainteresowanie

### Faza 3: PCB
- Projekt PCB zintegrowanego z modułem CubeCell
- Optymalizacja pod baterię (deep sleep, odcięcie LEDów)
- Przygotowanie pod produkcję SMT w JLCPCB

## Pomysły/Plany na rozwój

- **Tryby gry:**
  - Zdobywanie nadajników (zbliżenie się + potwierdzenie przyciskiem)
  - Rywalizacja (broadcast pozycji na innym kanale)
  - Tryb fabularny z questami

- **Animacje:**
  - Lis wyskakujący z krzaka przy zbliżeniu do nadajnika
  - Wizualizacja siły sygnału na ekranie i LEDach
  - Różne stany emocjonalne mascotki

- **Hardware:**
  - Własnoręcznie robione anteny DIY
  - Ulepszona obudowa z integracją PCB
  - Druk 3D

## Autorzy

- Adrian - Animacje i interfejs ekranu
- Hubert - Antena
- Antoni Sacewicz - Antena
- Judyta - Antena
- Antoni Antosik - obudowa 3D
- Katarzyna - Lider, Projekt PCB
- Maximilian Gaedig - Lider, Projekt PCB, Software

## Licencja

MIT
