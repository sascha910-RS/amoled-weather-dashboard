/*
 * Desk Dashboard fuer LilyGo T-Display-S3 AMOLED 1.9" (536x240)
 * Zeigt Uhrzeit, Datum und aktuelles Wetter von Open-Meteo.
 *
 * Aufbau des Bildschirms:
 *   links  : grosse Uhr, Datum, Verbindungsstatus
 *   rechts : Wettersymbol, Temperatur, Beschreibung, Detailzeile
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <LilyGo_AMOLED.h>
#include <lvgl.h>
#include <time.h>
#include <math.h>

#include "config.h"

// Eigene Montserrat-Schnitte mit Latin-1 (Umlaute), Aufzaehlungspunkt und den
// beiden FontAwesome-Icons der WLAN-Zeile. Erzeugt mit lv_font_conv aus
// lvgl/scripts/built_in_font/. Uhrzeit und Temperatur nutzen weiter die
// eingebaute 48er - dort kommen nur Ziffern, ":" und "Grad" vor.
LV_FONT_DECLARE(ui_font_14);
LV_FONT_DECLARE(ui_font_20);

// ---------------------------------------------------------------- Farben
#define COL_BG        0x000000
#define COL_TEXT      0xF2F2F2
#define COL_ACCENT    0x4FA3FF
#define COL_SUN       0xFFC64B
#define COL_CLOUD     0xC8CDD4
#define COL_RAIN      0x4FA3FF
#define COL_MOON      0xE6E2D3   // beleuchtete Seite
#define COL_MOON_DIM  0x2B2B30   // unbeleuchtete Seite: sichtbar, aber deutlich dunkler

// ---------------------------------------------------------------- Globals
LilyGo_Class amoled;

static lv_disp_draw_buf_t draw_buf;
static lv_color_t        *buf1 = nullptr;
static lv_disp_drv_t      disp_drv;

static lv_obj_t *root;
static lv_obj_t *lbl_time, *lbl_date, *lbl_status;
static lv_obj_t *lbl_temp, *lbl_cond, *lbl_place, *lbl_detail;
static lv_obj_t *ico_box, *ico_sun, *ico_moon, *ico_cloud, *ico_rain;

// Stundenspalten: je Uhrzeit, Symbol aus drei Ebenen und Temperatur
static lv_obj_t *lbl_hour[FORECAST_HOURS], *lbl_htemp[FORECAST_HOURS];
static lv_obj_t *ico_h_sun[FORECAST_HOURS], *ico_h_moon[FORECAST_HOURS];
static lv_obj_t *ico_h_cloud[FORECAST_HOURS], *ico_h_rain[FORECAST_HOURS];

// Der Mond wird pixelweise in eine Leinwand gezeichnet - mit Rechtecken und
// Kreisen laesst sich der Terminator nicht sauber abbilden (siehe drawMoon).
// Die Puffer liegen fest im RAM: 46x46 und 6x 20x20 sind zusammen rund 9 kB.
#define MOON_BOX      46
#define MOON_BOX_MINI 20
static lv_color_t moonBuf[MOON_BOX * MOON_BOX];
static lv_color_t moonBufMini[FORECAST_HOURS][MOON_BOX_MINI * MOON_BOX_MINI];

struct Weather {
    bool  valid   = false;
    float temp    = 0;
    float feels   = 0;
    int   humid   = 0;
    float wind    = 0;
    int   code    = -1;
    bool  day     = true;    // is_day aus der API: steuert Sonne oder Mond
} weather;

// Stundenvorschau: Uhrzeit und Temperatur der naechsten Stunden
struct HourSlot {
    bool  valid = false;
    int   hour  = 0;
    float temp  = 0;
    int   code  = -1;
    bool  day   = true;
} hourly[FORECAST_HOURS];

static uint32_t lastWeather  = 0;
static uint32_t weatherWait = WEATHER_INTERVAL_MS;
static uint32_t lastShift   = 0;
static uint32_t lastTick    = 0;
static uint8_t  shiftStep   = 0;
static bool     forceFetch  = true;

static const char *WEEKDAYS[] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
static const char *MONTHS[]   = {"Januar", "Februar", "März", "April", "Mai", "Juni",
                                 "Juli", "August", "September", "Oktober", "November", "Dezember"};

// ---------------------------------------------------------------- Display-Anbindung
// Der RM67162 adressiert in 2-Pixel-Schritten: Teilflaechen muessen auf
// gerade Startkoordinaten und ungerade Endkoordinaten gerundet werden.
static void disp_rounder(lv_disp_drv_t *drv, lv_area_t *area)
{
    LV_UNUSED(drv);
    if (area->x1 & 1)    area->x1--;
    if (!(area->x2 & 1)) area->x2++;
    if (area->y1 & 1)    area->y1--;
    if (!(area->y2 & 1)) area->y2++;
}

#if LV_USE_LOG
static void lv_log_cb(const char *msg)
{
    Serial.print("LVGL: ");
    Serial.println(msg);
    Serial.flush();
}
#endif

static void disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px)
{
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;
    amoled.pushColors(area->x1, area->y1, w, h, (uint16_t *)px);
    lv_disp_flush_ready(drv);
}

// ---------------------------------------------------------------- Wettercodes (WMO)
// Liefert Klartext und setzt die drei Icon-Ebenen.
static const char *decodeWeather(int code, bool &sun, bool &cloud, bool &rain)
{
    sun = cloud = rain = false;
    switch (code) {
        case 0:  sun = true;               return "Klar";
        case 1:  sun = true;               return "Überwiegend klar";
        case 2:  sun = cloud = true;       return "Teils bewölkt";
        case 3:  cloud = true;             return "Bedeckt";
        case 45:
        case 48: cloud = true;             return "Nebel";
        case 51:
        case 53:
        case 55: cloud = rain = true;      return "Nieselregen";
        case 56:
        case 57: cloud = rain = true;      return "Gefrierender Niesel";
        case 61: cloud = rain = true;      return "Leichter Regen";
        case 63: cloud = rain = true;      return "Regen";
        case 65: cloud = rain = true;      return "Starker Regen";
        case 66:
        case 67: cloud = rain = true;      return "Gefrierender Regen";
        case 71: cloud = rain = true;      return "Leichter Schnee";
        case 73: cloud = rain = true;      return "Schnee";
        case 75: cloud = rain = true;      return "Starker Schnee";
        case 77: cloud = rain = true;      return "Schneegriesel";
        case 80: cloud = rain = true;      return "Leichte Schauer";
        case 81: cloud = rain = true;      return "Schauer";
        case 82: cloud = rain = true;      return "Kräftige Schauer";
        case 85:
        case 86: cloud = rain = true;      return "Schneeschauer";
        case 95: cloud = rain = true;      return "Gewitter";
        case 96:
        case 99: cloud = rain = true;      return "Gewitter mit Hagel";
        default: cloud = true;             return "Keine Daten";
    }
}

// ---------------------------------------------------------------- Mondphase
// Beleuchteter Anteil nach Meeus, "Astronomical Algorithms", Kapitel 48,
// gekuerzt auf die groessten Stoerterme. Open-Meteo liefert die Mondphase im
// Forecast-Endpunkt nicht mit, also wird sie aus der Uhrzeit gerechnet.
//
// Die naheliegende Abkuerzung - Alter seit einem festen Neumond geteilt durch
// die mittlere Lunation - waere kuerzer, liegt aber bis zu 0.7 Tage daneben,
// weil die echte Lunation schwankt. Das sind bis zu 8 Prozentpunkte
// Beleuchtung, bei 46 px also rund 4 px verschobener Terminator. Die Reihe
// hier bleibt dagegen im Rahmen der Rundung auf ganze Pixel.
//
// Rueckgabe 0..1; waxing unterscheidet zunehmend von abnehmend.
static double moonIllumination(bool &waxing)
{
    time_t now = time(nullptr);
    if (now < 1600000000) {          // Uhr noch nicht per NTP gestellt
        waxing = true;
        return 1.0;                  // Vollmond als neutrale Anzeige
    }

    double jd = (double)now / 86400.0 + 2440587.5;   // Unix-Zeit -> Julianisches Datum
    double T  = (jd - 2451545.0) / 36525.0;          // Jahrhunderte seit J2000.0

    // Mittlere Elongation Mond-Sonne, mittlere Anomalie der Sonne, des Mondes
    double D  = 297.8501921 + 445267.1114034 * T - 0.0018819 * T * T;
    double M  = 357.5291092 +  35999.0502909 * T - 0.0001536 * T * T;
    double Mp = 134.9633964 + 477198.8675055 * T + 0.0087414 * T * T;

    // D vor dem Einsetzen reduzieren: der Wert waechst auf sechsstellige Grad
    double Dn = fmod(D, 360.0);
    if (Dn < 0) Dn += 360.0;

    // Phasenwinkel Sonne-Mond-Erde
    double i = 180.0 - Dn
             - 6.289 * sin(Mp * DEG_TO_RAD)
             + 2.100 * sin(M  * DEG_TO_RAD)
             - 1.274 * sin((2 * D - Mp) * DEG_TO_RAD)
             - 0.658 * sin((2 * D)      * DEG_TO_RAD)
             - 0.214 * sin((2 * Mp)     * DEG_TO_RAD)
             - 0.110 * sin(D * DEG_TO_RAD);

    // Vor dem Vollmond (Elongation unter 180 Grad) nimmt der Mond zu
    waxing = (Dn < 180.0);
    return (1.0 + cos(i * DEG_TO_RAD)) / 2.0;
}

// Zeichnet den Mond mit Durchmesser d in das obere linke Quadrat der Leinwand;
// der Rest bleibt Hintergrundfarbe und ist auf dem schwarzen Panel unsichtbar.
//
// Der Terminator ist eine halbe Ellipse, keine Kreislinie. Die naheliegende
// Abkuerzung - Scheibe plus versetzte Schattenscheibe - ergibt deshalb bei
// zunehmendem Dreiviertelmond eine dicke Sichel statt der richtigen Form, weil
// sich die Trennlinie dabei immer zur falschen Seite woelbt. Pro Bildzeile die
// beleuchtete Spanne auszurechnen ist genauso kurz und stimmt fuer jede Phase.
//
// Ansicht fuer die Nordhalbkugel: zunehmend ist rechts beleuchtet.
static void drawMoon(lv_obj_t *canvas, lv_color_t *buf, lv_coord_t size, lv_coord_t d)
{
    const lv_color_t lit  = lv_color_hex(COL_MOON);
    const lv_color_t dim  = lv_color_hex(COL_MOON_DIM);
    const lv_color_t back = lv_color_hex(COL_BG);

    for (int i = 0; i < size * size; i++) buf[i] = back;

    bool   waxing = true;
    double k      = moonIllumination(waxing);   // beleuchteter Anteil 0..1

    double r  = d / 2.0;
    double cx = r - 0.5;     // Mittelpunkt in Pixelkoordinaten (Pixel 0 liegt auf 0.0)
    double cy = r - 0.5;

    for (int y = 0; y < d; y++) {
        double dy = y - cy;
        double h2 = r * r - dy * dy;
        if (h2 <= 0) continue;
        double half = sqrt(h2);                          // halbe Scheibenbreite dieser Zeile

        // Die unbeleuchtete Seite wird nicht weggelassen, sondern nur stark
        // abgedunkelt - sonst waere bei Neumond ueberhaupt nichts zu sehen.
        int x0 = (int)ceil(cx - half), x1 = (int)floor(cx + half);
        if (x0 < 0) x0 = 0;
        if (x1 > d - 1) x1 = d - 1;
        for (int x = x0; x <= x1; x++) buf[y * size + x] = dim;

        // Terminator: wandert von +half (Neumond) nach -half (Vollmond)
        double edge = half * (1.0 - 2.0 * k);
        double from = waxing ? cx + edge : cx - half;
        double to   = waxing ? cx + half : cx - edge;

        int l0 = (int)ceil(from), l1 = (int)floor(to);
        if (l0 < 0) l0 = 0;
        if (l1 > d - 1) l1 = d - 1;
        for (int x = l0; x <= l1; x++) buf[y * size + x] = lit;
    }

    lv_obj_invalidate(canvas);
}

// ---------------------------------------------------------------- UI-Bausteine
static lv_obj_t *makeLabel(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
                           lv_coord_t x, lv_coord_t y, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, text);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, x, y);
    return l;
}

// Hilfsfunktion: randloses, transparentes Rechteck ohne Scrollverhalten
static lv_obj_t *makeBox(lv_obj_t *parent, lv_coord_t w, lv_coord_t h, uint32_t color, lv_coord_t radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

// Wettersymbol aus einfachen Formen: Sonne, Wolke, Regentropfen
static void buildIcon(lv_obj_t *parent, lv_coord_t x, lv_coord_t y)
{
    ico_box = makeBox(parent, 92, 92, COL_BG, 0);
    lv_obj_set_style_bg_opa(ico_box, LV_OPA_TRANSP, 0);
    lv_obj_align(ico_box, LV_ALIGN_TOP_LEFT, x, y);

    // Sonne
    ico_sun = makeBox(ico_box, 46, 46, COL_SUN, LV_RADIUS_CIRCLE);
    lv_obj_align(ico_sun, LV_ALIGN_TOP_LEFT, 6, 4);

    // Mond: sitzt an derselben Stelle wie die Sonne und ersetzt sie nachts.
    // Vor Wolke und Regen angelegt, damit die beiden davor liegen.
    ico_moon = lv_canvas_create(ico_box);
    lv_canvas_set_buffer(ico_moon, moonBuf, MOON_BOX, MOON_BOX, LV_IMG_CF_TRUE_COLOR);
    lv_obj_add_flag(ico_moon, LV_OBJ_FLAG_HIDDEN);

    // Wolke: Grundkoerper plus zwei Ausbuchtungen
    ico_cloud = makeBox(ico_box, 92, 92, COL_BG, 0);
    lv_obj_set_style_bg_opa(ico_cloud, LV_OPA_TRANSP, 0);
    lv_obj_align(ico_cloud, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t *body = makeBox(ico_cloud, 72, 26, COL_CLOUD, 13);
    lv_obj_align(body, LV_ALIGN_TOP_LEFT, 10, 36);
    lv_obj_t *puff1 = makeBox(ico_cloud, 34, 34, COL_CLOUD, LV_RADIUS_CIRCLE);
    lv_obj_align(puff1, LV_ALIGN_TOP_LEFT, 22, 22);
    lv_obj_t *puff2 = makeBox(ico_cloud, 26, 26, COL_CLOUD, LV_RADIUS_CIRCLE);
    lv_obj_align(puff2, LV_ALIGN_TOP_LEFT, 50, 28);

    // Regen
    ico_rain = makeBox(ico_box, 92, 92, COL_BG, 0);
    lv_obj_set_style_bg_opa(ico_rain, LV_OPA_TRANSP, 0);
    lv_obj_align(ico_rain, LV_ALIGN_TOP_LEFT, 0, 0);
    for (int i = 0; i < 3; i++) {
        lv_obj_t *drop = makeBox(ico_rain, 5, 14, COL_RAIN, 3);
        lv_obj_align(drop, LV_ALIGN_TOP_LEFT, 22 + i * 20, 68);
    }
}

// Verkleinerte Fassung des grossen Symbols fuer die Stundenspalten
static void buildMiniIcon(lv_obj_t *parent, int i)
{
    lv_obj_t *box = makeBox(parent, 34, 26, COL_BG, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_align(box, LV_ALIGN_TOP_MID, 0, 24);

    ico_h_sun[i] = makeBox(box, 20, 20, COL_SUN, LV_RADIUS_CIRCLE);
    lv_obj_align(ico_h_sun[i], LV_ALIGN_TOP_LEFT, 7, 2);

    ico_h_moon[i] = lv_canvas_create(box);
    lv_canvas_set_buffer(ico_h_moon[i], moonBufMini[i], MOON_BOX_MINI, MOON_BOX_MINI,
                         LV_IMG_CF_TRUE_COLOR);
    lv_obj_add_flag(ico_h_moon[i], LV_OBJ_FLAG_HIDDEN);

    ico_h_cloud[i] = makeBox(box, 28, 13, COL_CLOUD, 6);
    lv_obj_align(ico_h_cloud[i], LV_ALIGN_TOP_LEFT, 3, 8);

    ico_h_rain[i] = makeBox(box, 34, 26, COL_BG, 0);
    lv_obj_set_style_bg_opa(ico_h_rain[i], LV_OPA_TRANSP, 0);
    lv_obj_align(ico_h_rain[i], LV_ALIGN_TOP_LEFT, 0, 0);
    for (int d = 0; d < 3; d++) {
        lv_obj_t *drop = makeBox(ico_h_rain[i], 3, 7, COL_RAIN, 2);
        lv_obj_align(drop, LV_ALIGN_TOP_LEFT, 8 + d * 9, 19);
    }
}

// Wie setIcon(), nur fuer eine Stundenspalte: bei Sonne hinter Wolke rueckt
// das Gestirn nach oben rechts und wird kleiner.
static void setMiniIcon(int i, bool sun, bool cloud, bool rain, bool night)
{
    bool showSun  = sun && !night;
    bool showMoon = sun && night;
    bool small    = sun && cloud;

    showSun  ? lv_obj_clear_flag(ico_h_sun[i],   LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(ico_h_sun[i],   LV_OBJ_FLAG_HIDDEN);
    showMoon ? lv_obj_clear_flag(ico_h_moon[i],  LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(ico_h_moon[i],  LV_OBJ_FLAG_HIDDEN);
    cloud    ? lv_obj_clear_flag(ico_h_cloud[i], LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(ico_h_cloud[i], LV_OBJ_FLAG_HIDDEN);
    rain     ? lv_obj_clear_flag(ico_h_rain[i],  LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(ico_h_rain[i],  LV_OBJ_FLAG_HIDDEN);

    lv_obj_set_size(ico_h_sun[i], small ? 14 : 20, small ? 14 : 20);
    lv_obj_align(ico_h_sun[i], LV_ALIGN_TOP_LEFT, small ? 18 : 7, small ? 0 : 2);

    // Die Leinwand bleibt 20x20 und sitzt wie die Sonne; gezeichnet wird nur
    // das obere linke Quadrat, der schwarze Rest faellt nicht auf.
    lv_obj_align(ico_h_moon[i], LV_ALIGN_TOP_LEFT, small ? 18 : 7, small ? 0 : 2);
    if (showMoon) drawMoon(ico_h_moon[i], moonBufMini[i], MOON_BOX_MINI, small ? 14 : 20);

    lv_obj_align(ico_h_cloud[i], LV_ALIGN_TOP_LEFT, 3, rain ? 6 : 8);
}

// "sun" heisst hier "klarer Himmel" - ob daraus Sonne oder Mond wird,
// entscheidet is_day aus der API.
static void setIcon(bool sun, bool cloud, bool rain, bool night)
{
    bool showSun  = sun && !night;
    bool showMoon = sun && night;
    bool small    = sun && cloud;

    showSun  ? lv_obj_clear_flag(ico_sun,   LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(ico_sun,   LV_OBJ_FLAG_HIDDEN);
    showMoon ? lv_obj_clear_flag(ico_moon,  LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(ico_moon,  LV_OBJ_FLAG_HIDDEN);
    cloud    ? lv_obj_clear_flag(ico_cloud, LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(ico_cloud, LV_OBJ_FLAG_HIDDEN);
    rain     ? lv_obj_clear_flag(ico_rain,  LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(ico_rain,  LV_OBJ_FLAG_HIDDEN);

    // Hinter der Wolke rueckt das Gestirn nach oben rechts und wird kleiner
    lv_obj_align(ico_sun, LV_ALIGN_TOP_LEFT, small ? 40 : 6, small ? 0 : 4);
    lv_obj_set_size(ico_sun, small ? 36 : 46, small ? 36 : 46);

    // Die Leinwand bleibt 46x46 und sitzt wie die Sonne; gezeichnet wird nur
    // das obere linke Quadrat, der schwarze Rest faellt auf dem Panel nicht auf.
    lv_obj_align(ico_moon, LV_ALIGN_TOP_LEFT, small ? 40 : 6, small ? 0 : 4);
    if (showMoon) drawMoon(ico_moon, moonBuf, MOON_BOX, small ? 36 : 46);
}

static void placeAfterCond();

static void buildUI()
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);

    root = makeBox(scr, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr), COL_BG, 0);
    lv_obj_align(root, LV_ALIGN_TOP_LEFT, 0, 0);

    // --- linke Spalte
    lbl_time   = makeLabel(root, &lv_font_montserrat_48, COL_TEXT, 22,  25, "--:--");
    lbl_date   = makeLabel(root, &ui_font_20, COL_TEXT,  24,  80, "");
    lbl_status = makeLabel(root, &ui_font_14, COL_TEXT,  24, 110, "Verbinde ...");

    // --- Trennlinie zwischen den beiden Spalten
    lv_obj_t *divider = makeBox(root, 2, 110, 0x2A2A2A, 1);
    lv_obj_align(divider, LV_ALIGN_TOP_LEFT, 215, 22);

    // --- rechte Spalte
    buildIcon(root, 240, 18);
    lbl_temp   = makeLabel(root, &lv_font_montserrat_48, COL_TEXT, 348,  22, "--\xC2\xB0");
    lbl_cond   = makeLabel(root, &ui_font_20, COL_ACCENT, 244,  82, "Lade Wetter ...");
    lbl_place  = makeLabel(root, &ui_font_14, COL_TEXT,   244,  88, WEATHER_PLACE);
    lbl_detail = makeLabel(root, &ui_font_14, COL_TEXT,   244, 112, "");

    // --- Stundenvorschau: Trennlinie und je Spalte Uhrzeit, Symbol, Temperatur
    if (FORECAST_HOURS > 0) {
        lv_coord_t w = lv_disp_get_hor_res(nullptr);
        lv_obj_t *line = makeBox(root, w - 48, 2, 0x2A2A2A, 1);
        lv_obj_align(line, LV_ALIGN_TOP_LEFT, 24, 140);

        lv_coord_t pitch = (w - 48) / FORECAST_HOURS;
        for (int i = 0; i < FORECAST_HOURS; i++) {
            // Eigener Container je Spalte, damit sich alles ueber LV_ALIGN_TOP_MID
            // zentriert, statt Textbreiten von Hand auszurechnen.
            lv_obj_t *col = makeBox(root, pitch, 86, COL_BG, 0);
            lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
            lv_obj_align(col, LV_ALIGN_TOP_LEFT, 24 + i * pitch, 150);

            lbl_hour[i] = lv_label_create(col);
            lv_obj_set_style_text_font(lbl_hour[i], &ui_font_14, 0);
            lv_obj_set_style_text_color(lbl_hour[i], lv_color_hex(COL_TEXT), 0);
            lv_label_set_text(lbl_hour[i], "--");
            lv_obj_align(lbl_hour[i], LV_ALIGN_TOP_MID, 0, 0);

            buildMiniIcon(col, i);

            lbl_htemp[i] = lv_label_create(col);
            lv_obj_set_style_text_font(lbl_htemp[i], &ui_font_20, 0);
            lv_obj_set_style_text_color(lbl_htemp[i], lv_color_hex(COL_TEXT), 0);
            lv_label_set_text(lbl_htemp[i], "--\xC2\xB0");
            lv_obj_align(lbl_htemp[i], LV_ALIGN_TOP_MID, 0, 54);

            setMiniIcon(i, false, true, false, false);
        }
    }

    setIcon(false, true, false, false);
    placeAfterCond();
}

// Der Ortsname haengt hinter der Beschreibung, deren Breite sich mit jedem
// Wetterwechsel aendert - also nach jeder Textaenderung neu ausrichten.
static void placeAfterCond()
{
    if (!strlen(WEATHER_PLACE)) {
        lv_obj_add_flag(lbl_place, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_update_layout(root);
    lv_obj_align_to(lbl_place, lbl_cond, LV_ALIGN_OUT_RIGHT_BOTTOM, 10, -2);

    // Bei langen Beschreibungen wie "Gewitter mit Hagel" wird es rechts eng:
    // dann buendig an den Bildschirmrand statt ueber den Rand hinaus.
    lv_obj_update_layout(root);
    lv_coord_t right = lv_obj_get_x(lbl_place) + lv_obj_get_width(lbl_place);
    lv_coord_t limit = lv_disp_get_hor_res(nullptr) - 8;
    if (right > limit) lv_obj_set_x(lbl_place, lv_obj_get_x(lbl_place) - (right - limit));
}

// ---------------------------------------------------------------- Anzeige aktualisieren
static void updateClock()
{
    struct tm ti;
    if (!getLocalTime(&ti, 50)) {
        lv_label_set_text(lbl_time, "--:--");
        return;
    }
    char buf[32];
    strftime(buf, sizeof(buf), "%H:%M", &ti);
    lv_label_set_text(lbl_time, buf);

    snprintf(buf, sizeof(buf), "%s, %d. %s",
             WEEKDAYS[ti.tm_wday], ti.tm_mday, MONTHS[ti.tm_mon]);
    lv_label_set_text(lbl_date, buf);

    // Nachtabsenkung
    static int lastBrightness = -1;
    bool night = (ti.tm_hour >= NIGHT_START_HOUR || ti.tm_hour < NIGHT_END_HOUR);
    int target = night ? BRIGHTNESS_NIGHT : BRIGHTNESS_DAY;
    if (target != lastBrightness) {
        amoled.setBrightness(target);
        lastBrightness = target;
    }
}

static void updateHourlyUI()
{
    char buf[16];
    for (int i = 0; i < FORECAST_HOURS; i++) {
        if (!hourly[i].valid) {
            lv_label_set_text(lbl_hour[i],  "--");
            lv_label_set_text(lbl_htemp[i], "--\xC2\xB0");
            setMiniIcon(i, false, true, false, false);
            continue;
        }
        snprintf(buf, sizeof(buf), "%02d", hourly[i].hour);
        lv_label_set_text(lbl_hour[i], buf);

        snprintf(buf, sizeof(buf), "%.0f\xC2\xB0", hourly[i].temp);
        lv_label_set_text(lbl_htemp[i], buf);

        bool sun, cloud, rain;
        decodeWeather(hourly[i].code, sun, cloud, rain);
        setMiniIcon(i, sun, cloud, rain, !hourly[i].day);
    }
}

static void updateWeatherUI()
{
    char buf[80];

    if (!weather.valid) {
        lv_label_set_text(lbl_cond, "Keine Verbindung");
        return;
    }

    bool sun, cloud, rain;
    const char *text = decodeWeather(weather.code, sun, cloud, rain);
    setIcon(sun, cloud, rain, !weather.day);

    snprintf(buf, sizeof(buf), "%.0f\xC2\xB0", weather.temp);
    lv_label_set_text(lbl_temp, buf);

    lv_label_set_text(lbl_cond, text);
    placeAfterCond();

    // Trenner ist U+2022 - der Mittelpunkt U+00B7 fehlt in der eingebauten Montserrat
    snprintf(buf, sizeof(buf), "Gefühlt %.0f\xC2\xB0  \xE2\x80\xA2  %d %%  \xE2\x80\xA2  %.0f km/h",
             weather.feels, weather.humid, weather.wind);
    lv_label_set_text(lbl_detail, buf);

    updateHourlyUI();
}

static void updateStatus()
{
    char buf[48];
    if (WiFi.status() == WL_CONNECTED) {
        snprintf(buf, sizeof(buf), LV_SYMBOL_WIFI "  %s", WiFi.SSID().c_str());
    } else {
        snprintf(buf, sizeof(buf), LV_SYMBOL_CLOSE "  offline");
    }
    lv_label_set_text(lbl_status, buf);
}

// ---------------------------------------------------------------- Wetter holen
// Open-Meteo liefert die Stundenwerte ab Mitternacht des ersten Tages. Statt
// den Index auszurechnen, wird der Zeitstempel der aktuellen Stunde gesucht -
// das bleibt richtig, auch wenn die API ihren Zeitraum einmal anders schneidet.
static void parseHourly(JsonDocument &doc)
{
    for (int i = 0; i < FORECAST_HOURS; i++) hourly[i].valid = false;

    JsonArray times = doc["hourly"]["time"];
    JsonArray temps = doc["hourly"]["temperature_2m"];
    JsonArray codes = doc["hourly"]["weather_code"];
    JsonArray days  = doc["hourly"]["is_day"];
    if (times.isNull() || temps.isNull()) return;

    struct tm ti;
    if (!getLocalTime(&ti, 50)) return;      // ohne gestellte Uhr keine Zuordnung

    char key[24];
    strftime(key, sizeof(key), "%Y-%m-%dT%H:00", &ti);

    int start = -1;
    for (size_t i = 0; i < times.size(); i++) {
        const char *t = times[i].as<const char *>();
        if (t && strcmp(t, key) == 0) { start = (int)i + 1; break; }
    }
    if (start < 0) {
        Serial.printf("Stundenvorschau: %s nicht in der Antwort\n", key);
        return;
    }

    for (int i = 0; i < FORECAST_HOURS && (size_t)(start + i) < times.size(); i++) {
        const char *t = times[start + i].as<const char *>();
        if (!t) break;
        hourly[i].hour  = atoi(t + 11);      // Stelle 11 im Muster JJJJ-MM-TTThh:mm
        hourly[i].temp  = temps[start + i] | 0.0f;
        hourly[i].code  = codes.isNull() ? -1 : (codes[start + i] | -1);
        hourly[i].day   = days.isNull() ? true : ((days[start + i] | 1) != 0);
        hourly[i].valid = true;
    }
}

static bool fetchWeather()
{
    if (WiFi.status() != WL_CONNECTED) return false;

    String url = String("https://api.open-meteo.com/v1/forecast?latitude=") + LATITUDE +
                 "&longitude=" + LONGITUDE +
                 "&current=temperature_2m,relative_humidity_2m,apparent_temperature,"
                 "weather_code,wind_speed_10m,is_day"
                 "&hourly=temperature_2m,weather_code,is_day"
                 // zwei Tage, damit die Vorschau ueber Mitternacht hinweg reicht
                 "&timezone=auto&forecast_days=2";

    WiFiClientSecure client;
    client.setInsecure();          // Open-Meteo ohne Zertifikatspruefung; fuer ein Dashboard ok
    client.setTimeout(8);

    HTTPClient http;
    http.setConnectTimeout(8000);
    http.useHTTP10(true);          // keine chunked-Antwort
    if (!http.begin(client, url)) return false;

    int status = http.GET();
    if (status != HTTP_CODE_OK) {
        Serial.printf("HTTP-Fehler: %d\n", status);
        http.end();
        return false;
    }

    // Komplett einlesen statt streamen: getString() entpackt auch
    // chunked-Antworten korrekt. Die Antwort ist nur rund 1 kB gross.
    String payload = http.getString();
    http.end();
    Serial.printf("Antwort (%u Zeichen): %.160s\n", payload.length(), payload.c_str());

    // Nur die benoetigten Felder parsen spart Speicher
    JsonDocument filter;
    filter["current"]["temperature_2m"]       = true;
    filter["current"]["apparent_temperature"] = true;
    filter["current"]["relative_humidity_2m"] = true;
    filter["current"]["weather_code"]         = true;
    filter["current"]["wind_speed_10m"]       = true;
    filter["current"]["is_day"]               = true;
    filter["hourly"]["time"]                  = true;
    filter["hourly"]["temperature_2m"]        = true;
    filter["hourly"]["weather_code"]          = true;
    filter["hourly"]["is_day"]                = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload,
                                               DeserializationOption::Filter(filter));

    if (err) {
        Serial.printf("JSON-Fehler: %s\n", err.c_str());
        return false;
    }
    if (!doc["current"].is<JsonObject>()) {
        Serial.println("Antwort ohne Wetterdaten");
        return false;
    }

    JsonObject cur = doc["current"];
    weather.temp  = cur["temperature_2m"]       | 0.0f;
    weather.feels = cur["apparent_temperature"] | 0.0f;
    weather.humid = cur["relative_humidity_2m"] | 0;
    weather.code  = cur["weather_code"]         | -1;
    weather.wind  = cur["wind_speed_10m"]       | 0.0f;
    // Fehlt is_day, bleibt es bei Tag - lieber eine Sonne zu viel als ein
    // Mond am Mittag.
    weather.day   = (cur["is_day"]              | 1) != 0;
    weather.valid = true;

    parseHourly(doc);

    bool waxing = true;
    double moon = moonIllumination(waxing);
    Serial.printf("Wetter: %.1f C, Code %d, %s, Mond %.0f%% %s, Vorschau %dh-%dh\n",
                  weather.temp, weather.code, weather.day ? "Tag" : "Nacht",
                  moon * 100.0, waxing ? "zunehmend" : "abnehmend",
                  hourly[0].valid ? hourly[0].hour : -1,
                  hourly[FORECAST_HOURS - 1].valid ? hourly[FORECAST_HOURS - 1].hour : -1);
    return true;
}

// ---------------------------------------------------------------- Burn-in-Schutz
// Der ganze Inhalt wandert im Kreis um wenige Pixel, damit sich nichts einbrennt.
static void pixelShift()
{
    static const int8_t dx[] = {0, 2, 2, 0};
    static const int8_t dy[] = {0, 0, 2, 2};
    shiftStep = (shiftStep + 1) % 4;
    lv_obj_set_style_translate_x(root, dx[shiftStep], 0);
    lv_obj_set_style_translate_y(root, dy[shiftStep], 0);
}

// ---------------------------------------------------------------- Setup
void setup()
{
    Serial.begin(115200);
    delay(1500);
    Serial.println("=== Start ===");

    // Panel initialisieren; begin() erkennt die Board-Variante selbst.
    if (!amoled.begin()) {
        Serial.println("Display nicht gefunden - Verkabelung/Board pruefen");
        while (true) delay(1000);
    }
    // Der RM67162 ist nativ 240x536. Querformat 536x240 gibt es nur bei
    // Rotation 0 bzw. 2 - Rotation 1 und 3 sind Hochformat.
    amoled.setRotation(0);
    amoled.setBrightness(BRIGHTNESS_DAY);
    Serial.printf("Display: %ux%u\n", amoled.width(), amoled.height());

    // LVGL mit Vollbildpuffer im PSRAM, wie es der Lib-Helper der Boardreihe macht
    lv_init();
#if LV_USE_LOG
    lv_log_register_print_cb(lv_log_cb);
#endif
    size_t bufPixels = amoled.width() * amoled.height();
    buf1 = (lv_color_t *)ps_malloc(bufPixels * sizeof(lv_color_t));
    if (!buf1) {
        Serial.println("PSRAM-Puffer fehlgeschlagen - Board neu starten");
        while (true) delay(1000);
    }
    lv_disp_draw_buf_init(&draw_buf, buf1, nullptr, bufPixels);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res    = amoled.width();
    disp_drv.ver_res    = amoled.height();
    disp_drv.flush_cb   = disp_flush;
    disp_drv.rounder_cb = disp_rounder;   // Pflicht beim RM67162
    disp_drv.draw_buf   = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    buildUI();
    lv_timer_handler();

    // WLAN
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
        lv_timer_handler();
        delay(100);
    }
    Serial.printf("WLAN-Status: %d, IP: %s\n",
                  WiFi.status(), WiFi.localIP().toString().c_str());
    updateStatus();
    lv_timer_handler();

    // Zeit per NTP, Zeitzone inklusive Sommerzeitregel
    configTzTime(TZ_STRING, NTP_SERVER_1, NTP_SERVER_2);

    lastTick = millis();
}

// ---------------------------------------------------------------- Loop
void loop()
{
    uint32_t now = millis();

    // LVGL braucht einen Zeitgeber - ausser es zaehlt selbst (LV_TICK_CUSTOM)
#if !LV_TICK_CUSTOM
    lv_tick_inc(now - lastTick);
    lastTick = now;
#endif
    lv_timer_handler();

    // Uhr jede Sekunde
    static uint32_t lastClock = 0;
    if (now - lastClock >= 1000) {
        lastClock = now;
        updateClock();
    }

    // WLAN-Status im Blick behalten
    static uint32_t lastNet = 0;
    if (now - lastNet >= 5000) {
        lastNet = now;
        if (WiFi.status() != WL_CONNECTED) {
            WiFi.reconnect();
            forceFetch = true;
        }
        updateStatus();
    }

    // Wetter periodisch holen. Bewusst hier und nicht im lv_timer,
    // damit die Oberflaeche waehrend des Requests nicht blockiert wirkt.
    if (forceFetch || now - lastWeather >= weatherWait) {
        if (WiFi.status() == WL_CONNECTED) {
            forceFetch = false;
            lastWeather = now;
            if (fetchWeather()) {
                updateWeatherUI();
                // Die Vorschau braucht die per NTP gestellte Uhr. Steht die beim
                // ersten Abruf noch nicht, lieber bald nochmal fragen.
                bool vorschauDa = (FORECAST_HOURS == 0) || hourly[0].valid;
                weatherWait = vorschauDa ? WEATHER_INTERVAL_MS : WEATHER_RETRY_MS;
            } else {
                // Serverfehler oder Funkloch: bald nochmal versuchen statt
                // das volle Intervall mit veralteten Daten dazustehen.
                weatherWait = WEATHER_RETRY_MS;
            }
        }
    }

    // Burn-in-Schutz
    if (now - lastShift >= PIXEL_SHIFT_MS) {
        lastShift = now;
        pixelShift();
    }

    delay(5);
}
