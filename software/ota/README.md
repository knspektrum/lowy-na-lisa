# Aktualizacje przez Bluetooth

Odbiorniki ESP32 i nadajniki można aktualizować i sprawdzać bez kabla, ze strony `software/web-flasher` otwartej w Chrome/Chromium (komputer albo Android). Strona pokazuje całą flotę: które urządzenia są włączone, na jakim kanale (id) i z jakim programem.

## Jak to działa
- Każde urządzenie przez cały czas pracy nadaje przez Bluetooth LE (raz na sekundę) swoją nazwę `lisek-R-xxxx` (odbiornik) albo `lisek-T-xxxx` (nadajnik), id, stan i wersję programu
- Wersja to skrót commita (`5a431fb`), z `+` gdy program zbudowano z niezacommitowanych zmian
- Bluetooth dokłada ok. 1-3 mA do kilkudziesięciu mA, które i tak pobiera ESP32; wyłączone urządzenie nic nie pobiera
- Urządzenie przyjmuje tylko program podpisany kluczem projektu (ECDSA P-256) i zbudowany dla jego typu; inne pliki odrzuca przed zapisem
- Nowy program po restarcie czeka na potwierdzenie: strona łączy się ponownie i potwierdza, a program musi wcześniej sprawdzić, że moduł radiowy odpowiada
- Brak potwierdzenia w ciągu 5 minut, awaria albo restart przed potwierdzeniem: bootloader wraca do poprzedniej wersji

## Pierwsze wgranie (USB, raz na urządzenie)

Aktualizacje wymagają tablicy partycji z dwoma miejscami na program po 1.9 MB, którą da się wgrać tylko kablem:

```bash
software/ota/build.sh
software/ota/usb-flash.sh receiver /dev/ttyUSB0 /dev/ttyUSB1
software/ota/usb-flash.sh transmitter /dev/ttyUSB2
```

`usb-flash.sh` wgrywa na wszystkie podane porty naraz.

## Aktualizacja

1. Zbuduj i podpisz programy: `software/ota/build.sh` (wynik w `software/web-flasher/firmware/`)
2. Otwórz stronę z `localhost` (Web Bluetooth działa tylko przez https albo z `localhost`):
   ```bash
   python3 -m http.server 8765 --bind 127.0.0.1 --directory software/web-flasher
   ```
   i wejdź na http://localhost:8765. Programy wczytują się same; stronę można też otworzyć skądkolwiek i wczytać pliki `.lsk` ręcznie
3. **Dodaj urządzenie** dodaje jedno urządzenie przez okno wyboru Chrome (okno pokazuje wszystkie włączone w pobliżu)
4. **Aktualizuj** przy urządzeniu albo **Aktualizuj wszystkie włączone**
5. Nie zamykaj strony, dopóki stan nie pokaże „Zaktualizowano”: bez potwierdzenia urządzenie wróci do poprzedniej wersji

Przy odbiornikach przycisk **Tryb diod** przełącza pokazywanie id między zwykłym (1-7) a binarnym (1-8), patrz [software/led-receiver](../led-receiver/README.md#tryb-diod).

## Przeglądarki

Chrome, Edge albo Chromium; Firefox i Safari nie mają Web Bluetooth.

System  |  Bez ustawień  |  Z `chrome://flags/#enable-experimental-web-platform-features`
:-------------------------:|:-------------------------:|:-------------------------
Windows 10+  |  dodawanie, wersje, aktualizacje, stan co 30 s  |  + lista pamiętana między wizytami, sygnał na bieżąco
macOS  |  dodawanie, wersje, aktualizacje, stan co 30 s  |  + to samo oraz **Skanuj okolicę** (wszystkie naraz, bez wybierania)
Android  |  dodawanie, wersje, aktualizacje, stan co 30 s  |  + to samo oraz **Skanuj okolicę**
ChromeOS  |  dodawanie, wersje, aktualizacje, stan co 30 s  |  + lista pamiętana między wizytami
Linux  |  nic  |  dodawanie, wersje, aktualizacje, stan co 30 s, lista pamiętana

„Stan co 30 s”: strona łączy się po kolei z dodanymi urządzeniami i odczytuje wersję; urządzenie, które nie odpowiada przez ok. minutę, jest „wyłączone”. Bez flagi po odświeżeniu strony urządzenia trzeba dodać ponownie (lista z wersjami i czasem ostatniego sygnału zostaje). Źródło: [stan implementacji Web Bluetooth](https://github.com/WebBluetoothCG/web-bluetooth/blob/main/implementation-status.md).

## Klucz

- `software/ota/sign.py keygen` tworzy klucz raz: prywatny w `~/.config/lowy-na-lisa/signing-key.pem` (poza repozytorium, zrób kopię), publiczny w `software/libraries/LisekOta/src/lisek_pubkey.h`
- Bez klucza prywatnego nie da się wgrać nowego programu przez Bluetooth, tylko przez USB
- Nowy klucz wymaga ponownego wgrania wszystkich urządzeń przez USB

## Pliki

- `build.sh` - buduje oba programy, podpisuje je i zapisuje `firmware/manifest.json`
- `usb-flash.sh` - pierwsze wgranie przez USB, na wiele urządzeń naraz
- `sign.py` - klucz i podpisywanie (format nagłówka jak `struct Header` w `LisekOta.cpp`)
- `software/libraries/LisekOta/` - biblioteka Arduino używana przez oba programy
- `software/web-flasher/index.html` - strona floty i aktualizacji
