---
theme:
  override:
    code:
      alignment: left
      background: false
---

Łowy na Lisa - Update 15.01.26
====
- targi hobby
- nadajniki się zakłucały
- plany na przyszłość

<!-- end_slide -->

targi hobby
===
- pierwsza działająca wersja/prezentacja dla dzieci
- zauważenie że antena za duża dla dzieci
- dobre wrażenie - obudowa poręczna
- dobra dekoracja
- swiatełka wzbudzały zainteresowanie

![](targi_hobby.png)


<!-- end_slide -->

nadajniki się zakłucały
===
- było tylko odróżnianie po id wysyłanym w pakiecie lora
- nadawały były na tym samym kanale
- prosty fix: różne kanały

![](fighting_transmitters.jpg)

<!-- end_slide -->

plany na przyszłość
===
- ekran
- animacje
- antena
- pcb

![](fox_future.jpg)

<!-- end_slide -->

ekran
===
- maskotka lisek
- wizualizacja sygnału (lisek smutny/sczęsliwy)
- zmiana kalibracji
- ekran z deltalami: snr/rssi / detale na main screen
- stan baterii
- hint obsługi

![](fox_frames.png)



<!-- end_slide -->

## własnoręcznie robione anteny DIY
- druty
- kable
- druk 3d :)

![](custom_antenna.png)

<!-- end_slide -->


#### pomysły na zabawe
- zdobywanie nadajników (nadajnik blisko + potwierdzenie guzikiem) (przeskoczenie na następny kanał?)
- rywalizacja (broadcast aktualnego poziomu/nadajnika na innym kanale)
- story wymysleć

## polepszenie obudowy
- integracja pcb

<!-- end_slide -->

pcb
===
- stan baterii (voltage divider)
- ładowanie (moduł tpXXXX) + usb c
- Regulator LDO bateria -> 3.3v
- integracja z modułami CubeCell AM01
- podłączenie do anteny U.FL Receptacle jest już integrowane w CubeCell
- ledy + rezystory
- złącze na ekran
- złącza na guziki (reset + menu1 + menu2)

![](pcb_mockup.png)

<!-- end_slide -->

dalszy plan
===
- 25.02.26 środa spotkanko 17:00 - pcb v1 i test antena
- 05.03.26 pcb final - zamówienie
- 25.03.26 spotkanko, zkładanie, lutowanie
- wrzesień - noc naukowców


![](foxcalendar.jpg)

<!-- end_slide -->

# pytania?

<!-- end_slide -->

<!-- end_slide -->
<!-- end_slide -->
<!-- end_slide -->
<!-- end_slide -->

### cel - noc naukowców - wrzesień

<!-- end_slide -->

## kalibracja
- przy resetcie?
#### animacje
- animacja jak się jest blisko do nadajnika - będzie karton, karton się otworzy i lis wyskoczy

# rozłożenie zadań
- animacje/pixelizacja liska - Adrian - do 15.03.26
- pcb - v1 Kasia - do 25.02.26 - muszę podać plan pinów
- antena - Hubert + Antek - do 25.02.26 (blocked by order + druk (Max))
- programowanko - Adrian i Max - bez stanu bateri do 15.03.26 - dodanie stanu bateri do 30.03.26
- story - Antek
- obudowa - Antek3D (blocked by pcb (Kasia))

# plan prezentacji
- Hubert - targi hobby
- Max - zakułucanie się nadajników
- Adrian - animacje/interfejs ekranu

