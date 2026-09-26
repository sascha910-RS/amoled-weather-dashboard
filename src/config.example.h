#pragma once

// Vorlage fuer src/config.h - diese Datei hier enthaelt keine Geheimnisse und
// darf eingecheckt werden. Zum Einrichten kopieren:
//
//     cp src/config.example.h src/config.h
//
// src/config.h steht in .gitignore und bleibt lokal. Beachte trotzdem: Das
// WLAN-Passwort landet beim Bauen im Klartext im Firmware-Image und laesst
// sich mit "esptool.py read_flash" wieder herausziehen. Wer physischen Zugang
// zum Board hat, kommt also daran. Am saubersten haengt das Dashboard deshalb
// im Gast- oder IoT-Netz, nicht im Hauptnetz.

// ---------- WLAN ----------
#define WIFI_SSID       ""
#define WIFI_PASS       ""

// ---------- Standort (Dezimalgrad) ----------
// Koordinaten z.B. auf openstreetmap.org ablesen
#define LATITUDE        "47.3769"
#define LONGITUDE       "8.5417"
// Name, der hinter der Wetterbeschreibung steht. Leer lassen blendet ihn aus.
#define WEATHER_PLACE   ""

// ---------- Zeitzone ----------
// POSIX-TZ-String. Mitteleuropa inkl. Sommerzeitregel:
#define TZ_STRING       "CET-1CEST,M3.5.0,M10.5.0/3"
#define NTP_SERVER_1    "de.pool.ntp.org"
#define NTP_SERVER_2    "pool.ntp.org"

// ---------- Verhalten ----------
#define FORECAST_HOURS        6     // Stundenvorschau: Anzahl Spalten (0 blendet die Zeile aus)
#define WEATHER_INTERVAL_MS   (10UL * 60UL * 1000UL)  // Wetter alle 10 Minuten holen
#define WEATHER_RETRY_MS      (30UL * 1000UL)         // nach einem Fehlversuch frueher nochmal
#define PIXEL_SHIFT_MS        (3UL * 60UL * 1000UL)   // Burn-in-Schutz: alle 3 Minuten verschieben

#define BRIGHTNESS_DAY        180   // 0..255
#define BRIGHTNESS_NIGHT      35
#define NIGHT_START_HOUR      22    // ab 22:00 gedimmt
#define NIGHT_END_HOUR        7     // bis 07:00 gedimmt
