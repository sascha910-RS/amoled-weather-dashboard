# Desk Dashboard – LilyGo T-Display-S3 AMOLED 1.9"

<a id="deutsch"></a>
**Deutsch** · [English](#english)

Uhr, Datum und aktuelles Wetter (Open-Meteo) auf dem 536×240-AMOLED, dazu eine
Stundenvorschau am unteren Rand. Nach Sonnenuntergang zeigt das Wettersymbol
statt der Sonne einen Mond in der aktuellen Phase. Gebaut mit PlatformIO,
LVGL 8 und der offiziellen LilyGo-Bibliothek.

## Einrichtung

1. **Projekt öffnen** – Ordner in VS Code mit PlatformIO-Erweiterung öffnen.

2. **`src/config.h` anlegen** – aus der Vorlage kopieren und ausfüllen:

   ```
   cp src/config.example.h src/config.h
   ```

   Hinein gehören WLAN-Zugang, die Koordinaten deines Standorts und der
   Ortsname, der hinter der Wetterbeschreibung erscheint. Die Zeitzone ist
   bereits auf Mitteleuropa inklusive Sommerzeitregel gesetzt.

   `src/config.h` steht in `.gitignore` und bleibt damit lokal. Das schützt
   aber nur vor versehentlichem Einchecken: Das WLAN-Passwort landet beim
   Bauen im Klartext im Firmware-Image und lässt sich von dort mit
   `esptool.py read_flash` plus `strings` wieder herausziehen. Wer das Board
   in die Hand bekommt, kommt also an das Passwort. Deshalb gehört so ein
   Gerät ins Gast- oder IoT-Netz und nicht ins Hauptnetz.

3. **`lv_conf.h` anlegen** – der einzige etwas fummelige Schritt. Einmal bauen,
   damit PlatformIO die Bibliotheken lädt, dann:

   ```
   cp .pio/libdeps/t-display-s3-amoled/lvgl/lv_conf_template.h include/lv_conf.h
   ```

   In der Datei anpassen:

   | Einstellung | Wert | warum |
   |---|---|---|
   | `#if 0` ganz oben | `#if 1` | sonst ist die ganze Datei deaktiviert |
   | `LV_COLOR_DEPTH` | `16` | RGB565 |
   | `LV_COLOR_16_SWAP` | `1` | die QSPI-Schnittstelle erwartet die beiden Farbbytes vertauscht |
   | `LV_TICK_CUSTOM` | `1` | LVGL zählt die Zeit selbst über `millis()` |
   | `LV_MEM_SIZE` | `(64U * 1024U)` | |
   | `LV_USE_CANVAS` | `1` | steht in der Vorlage schon so – nicht ausschalten, die Mondphase wird pixelweise gezeichnet |
   | `LV_FONT_MONTSERRAT_14` | `1` | LVGL-Standardschrift |
   | `LV_FONT_MONTSERRAT_20` | `0` | ersetzt durch `src/fonts/ui_font_20.c` |
   | `LV_FONT_MONTSERRAT_48` | `1` | Uhrzeit und Temperatur |

4. **Flashen** – Board per USB-C anschließen, `Upload` drücken. Falls der Port
   nicht erscheint: BOOT-Taste halten, kurz RESET drücken, loslassen.

## Wenn etwas nicht stimmt

- **Schrift ist blau-grün-lila verfärbt**: `LV_COLOR_16_SWAP` steht auf `0`.
  Bei Schwarz und Weiß fällt das nicht auf, aber jeder Zwischenton der
  Kantenglättung kippt in eine andere Farbe.
- **Board friert nach wenigen Sekunden ein**, die Anzeige bleibt beim
  Startbild stehen: In `setup()` muss `disp_drv.rounder_cb` gesetzt sein. Der
  RM67162 adressiert in 2-Pixel-Schritten; ohne das Runden auf gerade
  Koordinaten geht der erste Teilbereich schief, der nicht zufällig passt.
- **Nur die linke Bildhälfte ist sichtbar**: `setRotation(1)` ist Hochformat
  240×536. Querformat 536×240 liefern `0` und `2`, die sich um 180° unterscheiden.
- **Kästchen statt Zeichen**: Das Zeichen fehlt in der Schrift. Die eingebaute
  Montserrat deckt nur `0x20-0x7F`, `0xB0` und `0x2022` ab – kein Umlaut, kein
  `·`. Dafür liegen die erweiterten Schnitte in `src/fonts/`.
- **Uhr zeigt `--:--`**: NTP braucht nach dem Start ein paar Sekunden und
  funktioniert nur mit bestehender WLAN-Verbindung.
- **Nachts erscheint trotzdem eine Sonne**: Tag und Nacht kommen aus dem Feld
  `is_day` der API. Fehlt es in der Antwort, wird bewusst auf Tag
  zurückgefallen – neue Felder müssen zusätzlich in den `filter` in
  `fetchWeather()` eingetragen werden, sonst verwirft ArduinoJson sie beim
  Parsen.
- **Upload findet den Port nicht**: Läuft noch ein serieller Monitor? Der
  belegt den Port. Hängt die Firmware, kommt auch der automatische Reset nicht
  durch – dann BOOT halten, kurz RESET drücken, loslassen.

## Aufbau des Codes

| Bereich | Funktion |
|---|---|
| `buildUI()` | legt das Layout an: links Uhr, rechts Wetter |
| `buildIcon()` / `setIcon()` | Wettersymbol aus Kreisen und Rechtecken, vier ein- und ausblendbare Ebenen (Sonne, Mond, Wolke, Regen) |
| `moonIllumination()` / `drawMoon()` | Mondphase nach Meeus Kap. 48, pixelweise in eine LVGL-Leinwand gezeichnet |
| `fetchWeather()` | HTTPS-Abfrage bei Open-Meteo, gefiltertes JSON-Parsing |
| `decodeWeather()` | WMO-Wettercode → deutscher Klartext und Symbolkombination |
| `pixelShift()` | verschiebt den Inhalt alle 3 Minuten um 2 Pixel gegen Einbrennen |
| `updateClock()` | Uhrzeit, Datum und Nachtabsenkung der Helligkeit |
| `placeAfterCond()` | rückt den Ortsnamen hinter die je nach Wetter unterschiedlich lange Beschreibung |
| `parseHourly()` / `updateHourlyUI()` | Stundenvorschau am unteren Rand, Spaltenzahl über `FORECAST_HOURS` |
| `src/fonts/` | mit `lv_font_conv` erzeugte Montserrat-Schnitte inkl. Umlauten (Latin-1) und den zwei FontAwesome-Icons der WLAN-Zeile |

### Zur Mondphase

Open-Meteo liefert die Mondphase im Forecast-Endpunkt nicht mit, sie wird
deshalb aus der per NTP gestellten Uhr gerechnet – nach Meeus,
*Astronomical Algorithms*, Kapitel 48, gekürzt auf die größten Störterme.

Die kürzere Variante, Alter seit einem festen Neumond geteilt durch die
mittlere Lunation, liegt bis zu 0,7 Tage daneben, weil die echte Lunation
schwankt. Das sind bis zu 8 Prozentpunkte Beleuchtung und damit rund 4 px
verschobener Terminator – auf 46 px Symbolgröße sichtbar.

Gezeichnet wird pixelweise in eine `lv_canvas` statt aus Kreisen und
Rechtecken wie die übrigen Symbole: Der Terminator ist eine halbe Ellipse, und
die übliche Abkürzung aus Scheibe plus versetzter Schattenscheibe wölbt ihn
beim Dreiviertelmond zur falschen Seite. Die unbeleuchtete Hälfte wird nur
abgedunkelt, nicht weggelassen, damit bei Neumond noch etwas zu sehen ist.
Die Ansicht gilt für die Nordhalbkugel: zunehmend ist rechts beleuchtet.

## Naheliegende Erweiterungen

- **Touch**: Wenn deine Variante ein Touchpanel hat, `amoled.getPoint()` in einen
  `lv_indev` hängen und zwischen mehreren Seiten wechseln.
- **Stundenvorschau als Kurve**: Die Werte holt `fetchWeather()` bereits. Statt
  der Textspalten ließe sich daraus ein `lv_chart` zeichnen.
- **Innentemperatur**: ein BME280 am I²C-Port ergänzt die rechte Spalte.
- **Südhalbkugel**: `drawMoon()` zeichnet fest für den Norden. Ein Spiegeln
  anhand des Breitengrads aus `config.h` wären wenige Zeilen.
- **Gehäuse**: Das Board hat Befestigungslöcher – ein schräger Ständer mit
  Kabelführung nach hinten ist ein dankbarer Druck.

---

<a id="english"></a>
# Desk Dashboard – LilyGo T-Display-S3 AMOLED 1.9"

[Deutsch](#deutsch) · **English**

Clock, date and current weather (Open-Meteo) on the 536×240 AMOLED, with an
hourly forecast along the bottom. After sunset the weather icon shows a moon in
its current phase instead of the sun. Built with PlatformIO, LVGL 8 and the
official LilyGo library.

## Setup

1. **Open the project** – open the folder in VS Code with the PlatformIO
   extension.

2. **Create `src/config.h`** – copy the template and fill it in:

   ```
   cp src/config.example.h src/config.h
   ```

   It holds your Wi-Fi credentials, the coordinates of your location and the
   place name shown after the weather description. The time zone is already set
   to Central Europe including the DST rule.

   `src/config.h` is listed in `.gitignore` and stays local. That only guards
   against committing it by accident, though: the Wi-Fi password ends up in the
   firmware image in plain text and can be pulled back out with
   `esptool.py read_flash` and `strings`. Anyone holding the board can read it,
   so put a device like this on a guest or IoT network rather than your main one.

3. **Create `lv_conf.h`** – the one fiddly step. Build once so PlatformIO
   fetches the libraries, then:

   ```
   cp .pio/libdeps/t-display-s3-amoled/lvgl/lv_conf_template.h include/lv_conf.h
   ```

   Change these settings in the file:

   | Setting | Value | Why |
   |---|---|---|
   | `#if 0` at the very top | `#if 1` | otherwise the whole file is disabled |
   | `LV_COLOR_DEPTH` | `16` | RGB565 |
   | `LV_COLOR_16_SWAP` | `1` | the QSPI interface expects the two colour bytes swapped |
   | `LV_TICK_CUSTOM` | `1` | LVGL keeps time itself via `millis()` |
   | `LV_MEM_SIZE` | `(64U * 1024U)` | |
   | `LV_USE_CANVAS` | `1` | already the template default – leave it on, the moon phase is drawn pixel by pixel |
   | `LV_FONT_MONTSERRAT_14` | `1` | LVGL's default font |
   | `LV_FONT_MONTSERRAT_20` | `0` | replaced by `src/fonts/ui_font_20.c` |
   | `LV_FONT_MONTSERRAT_48` | `1` | clock and temperature |

4. **Flash** – connect the board over USB-C and hit `Upload`. If the port does
   not show up: hold BOOT, tap RESET, release.

## Troubleshooting

- **Text has a blue-green-purple tint**: `LV_COLOR_16_SWAP` is set to `0`. Pure
  black and white look fine, but every intermediate shade from the
  anti-aliasing shifts to a different colour.
- **The board freezes after a few seconds** and the display stays on the splash
  screen: `disp_drv.rounder_cb` has to be set in `setup()`. The RM67162
  addresses in 2-pixel steps; without rounding to even coordinates, the first
  partial area that does not happen to line up goes wrong.
- **Only the left half of the screen is visible**: `setRotation(1)` is portrait,
  240×536. Landscape 536×240 comes from `0` and `2`, which differ by 180°.
- **Boxes instead of characters**: the glyph is missing from the font. The
  built-in Montserrat only covers `0x20-0x7F`, `0xB0` and `0x2022` – no umlauts,
  no `·`. The extended cuts in `src/fonts/` cover those.
- **The clock shows `--:--`**: NTP needs a few seconds after startup and only
  works with an established Wi-Fi connection.
- **A sun still appears at night**: day and night come from the API's `is_day`
  field. If it is missing from the response the code deliberately falls back to
  day – note that new fields also have to be added to the `filter` in
  `fetchWeather()`, otherwise ArduinoJson drops them while parsing.
- **Upload cannot find the port**: is a serial monitor still running? It holds
  the port. If the firmware has hung, the automatic reset will not get through
  either – then hold BOOT, tap RESET, release.

## How the code is organised

| Area | Purpose |
|---|---|
| `buildUI()` | builds the layout: clock on the left, weather on the right |
| `buildIcon()` / `setIcon()` | weather icon from circles and rectangles, four layers that are shown or hidden (sun, moon, cloud, rain) |
| `moonIllumination()` / `drawMoon()` | moon phase per Meeus ch. 48, drawn pixel by pixel into an LVGL canvas |
| `fetchWeather()` | HTTPS request to Open-Meteo, filtered JSON parsing |
| `decodeWeather()` | WMO weather code → German plain text and icon combination |
| `pixelShift()` | shifts the content by 2 pixels every 3 minutes against burn-in |
| `updateClock()` | time, date and the night-time brightness drop |
| `placeAfterCond()` | positions the place name after the weather description, whose width varies |
| `parseHourly()` / `updateHourlyUI()` | hourly forecast along the bottom, column count via `FORECAST_HOURS` |
| `src/fonts/` | Montserrat cuts generated with `lv_font_conv`, including umlauts (Latin-1) and the two FontAwesome icons of the Wi-Fi line |

### About the moon phase

Open-Meteo does not include the moon phase in its forecast endpoint, so it is
computed from the NTP-synchronised clock – following Meeus,
*Astronomical Algorithms*, chapter 48, reduced to the largest periodic terms.

The shorter approach, age since a fixed new moon divided by the mean lunation,
is off by up to 0.7 days because the real lunation varies. That is up to
8 percentage points of illumination, which moves the terminator by roughly
4 px – visible at an icon size of 46 px.

Unlike the other icons, the moon is drawn pixel by pixel into an `lv_canvas`
rather than assembled from circles and rectangles: the terminator is half an
ellipse, and the usual shortcut of a disc plus an offset shadow disc curves it
the wrong way for a gibbous moon. The unlit half is dimmed rather than omitted,
so there is still something to see at new moon. The view is drawn for the
northern hemisphere: waxing is lit on the right.

## Obvious extensions

- **Touch**: if your variant has a touch panel, hook `amoled.getPoint()` into an
  `lv_indev` and switch between several pages.
- **Hourly forecast as a curve**: `fetchWeather()` already retrieves the values.
  An `lv_chart` could replace the text columns.
- **Indoor temperature**: a BME280 on the I²C port rounds out the right column.
- **Southern hemisphere**: `drawMoon()` is hard-wired for the north. Mirroring
  it based on the latitude from `config.h` would take a few lines.
- **Enclosure**: the board has mounting holes – an angled stand with cable
  routing out the back is a rewarding print.
