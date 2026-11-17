/* ESP32 weather station for the LilyGo T5 4.7" e-paper display.
 *
 * The sketch connects to WiFi, fetches the Met Office hourly forecast,
 * composes a 960x540 greyscale canvas (large current conditions + clock +
 * date and six hourly icons) in the e-paper framebuffer and refreshes it.
 *
 * This is the ESP32 translation of ../weather.ipynb.  The icons come from
 * ../icons and are embedded as 4-bit greyscale data in weather_icons.h
 * (regenerate with tools/generate_icons.py).
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <algorithm>
#include <math.h>
#include <esp_heap_caps.h>
#include <esp_adc_cal.h>
#include <string.h>
#include <time.h>

#include "epd_driver.h"
#include "Ubuntu225.h"
#include "Ubuntu42.h"
#include "Ubuntu20.h"
#include "Ubuntu32.h"
#include "weather_icons.h"
#include "creds.h"

/* ------------------------------------------------------------------ */
/* Configuration                                                       */
/* ------------------------------------------------------------------ */

static const char *API_URL_TEMPLATE =
    "https://data.hub.api.metoffice.gov.uk/sitespecific/v0/point/hourly"
    "?dataSource=BD1&latitude=%s&longitude=%s";

/* Amazon Root CA 1 -- trust anchor for data.hub.api.metoffice.gov.uk. */
static const char *API_ROOT_CA =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIEkjCCA3qgAwIBAgITBn+USionzfP6wq4rAfkI7rnExjANBgkqhkiG9w0BAQsF\n"
    "ADCBmDELMAkGA1UEBhMCVVMxEDAOBgNVBAgTB0FyaXpvbmExEzARBgNVBAcTClNj\n"
    "b3R0c2RhbGUxJTAjBgNVBAoTHFN0YXJmaWVsZCBUZWNobm9sb2dpZXMsIEluYy4x\n"
    "OzA5BgNVBAMTMlN0YXJmaWVsZCBTZXJ2aWNlcyBSb290IENlcnRpZmljYXRlIEF1\n"
    "dGhvcml0eSAtIEcyMB4XDTE1MDUyNTEyMDAwMFoXDTM3MTIzMTAxMDAwMFowOTEL\n"
    "MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv\n"
    "b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj\n"
    "ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM\n"
    "9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw\n"
    "IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6\n"
    "VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L\n"
    "93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm\n"
    "jgSubJrIqg0CAwEAAaOCATEwggEtMA8GA1UdEwEB/wQFMAMBAf8wDgYDVR0PAQH/\n"
    "BAQDAgGGMB0GA1UdDgQWBBSEGMyFNOy8DJSULghZnMeyEE4KCDAfBgNVHSMEGDAW\n"
    "gBScXwDfqgHXMCs4iKK4bUqc8hGRgzB4BggrBgEFBQcBAQRsMGowLgYIKwYBBQUH\n"
    "MAGGImh0dHA6Ly9vY3NwLnJvb3RnMi5hbWF6b250cnVzdC5jb20wOAYIKwYBBQUH\n"
    "MAKGLGh0dHA6Ly9jcnQucm9vdGcyLmFtYXpvbnRydXN0LmNvbS9yb290ZzIuY2Vy\n"
    "MD0GA1UdHwQ2MDQwMqAwoC6GLGh0dHA6Ly9jcmwucm9vdGcyLmFtYXpvbnRydXN0\n"
    "LmNvbS9yb290ZzIuY3JsMBEGA1UdIAQKMAgwBgYEVR0gADANBgkqhkiG9w0BAQsF\n"
    "AAOCAQEAYjdCXLwQtT6LLOkMm2xF4gcAevnFWAu5CIw+7bMlPLVvUOTNNWqnkzSW\n"
    "MiGpSESrnO09tKpzbeR/FoCJbM8oAxiDR3mjEH4wW6w7sGDgd9QIpuEdfF7Au/ma\n"
    "eyKdpwAJfqxGF4PcnCZXmTA5YpaP7dreqsXMGz7KQ2hsVxa81Q4gLv7/wmpdLqBK\n"
    "bRRYh5TmOTFffHPLkIhqhBGWJ6bt2YFGpn6jcgAKUj6DiAdjd4lpFw85hdKrCEVN\n"
    "0FE6/V1dN2RMfjCyVSRCnTawXZwXgWHxyvkQAiSr6w10kY17RSlQOYiypok1JR4U\n"
    "akcjMS9cmvqtmg5iUaQqqcT5NJ0hGA==\n"
    "-----END CERTIFICATE-----\n";

/* Layout: same numbers as the notebook. */
#define CANVAS_WIDTH EPD_WIDTH
#define CANVAS_HEIGHT EPD_HEIGHT
#define ICON_SIZE WEATHER_ICON_SIZE
#define SIDE_MARGIN 20
#define TOP_MARGIN 20
#define BOTTOM_MARGIN 20
#define FORECAST_COUNT 6
#define MAX_FORECAST_ROWS 64
#define REFRESH_INTERVAL_MS (60UL * 1000UL)
#define DATA_REFRESH_INTERVAL_MS (30UL * 60UL * 1000UL)

/* The text fonts are TFT_eSPI "VLW" fonts: a header, one 28-byte metric record
 * per glyph and an 8-bit alpha bitmap per glyph.  Glyph count, ascent and
 * descent are read from each font header at runtime.  The clock, date, current
 * labels and forecast labels use Ubuntu225, Ubuntu42, Ubuntu32 and Ubuntu20 at
 * native scale (1:1), so no text is resampled. */
#define CLOCK_FONT_BITS Ubuntu225
#define DATE_FONT_BITS Ubuntu42
#define ICON_FONT_BITS Ubuntu20
#define CURRENT_FONT_BITS Ubuntu32
#define FONT_HEADER_SIZE 24
#define FONT_METRIC_SIZE 28

/* Declared before the drawing helpers so Arduino's prototype generator
 * sees these types when it scans the sketch. */
enum TextAlign
{
    ALIGN_LEFT,
    ALIGN_CENTER,
    ALIGN_RIGHT
};

struct FontGlyph {
    uint16_t code;
    uint8_t height, width, advance;
    int16_t dY;                         /* baseline to top of the bitmap */
    int8_t dX;                          /* cursor to left of the bitmap */
    uint32_t bitmap;                    /* offset into the font's bitmap data */
};

struct Font {
    const uint8_t *bits;
    uint16_t glyphCount;
    int ascent;
    int lineHeight;
    bool ready;
};

struct ForecastRow {
    char time[17];              /* "YYYY-MM-DDTHH:MMZ" */
    int code;                   /* Met Office significantWeatherCode */
    float temp;                 /* screenTemperature, deg C */
    float precip;               /* totalPrecipAmount, mm */
    int prob;                   /* probOfPrecipitation, % */
    float wind;                 /* windSpeed10m, m/s */
};

static ForecastRow forecast[MAX_FORECAST_ROWS];
static size_t forecastCount = 0;
static uint8_t *framebuffer = nullptr;
static Font clockFont = { CLOCK_FONT_BITS, 0, 0, 0, false };
static Font dateFont = { DATE_FONT_BITS, 0, 0, 0, false };
static Font iconFont = { ICON_FONT_BITS, 0, 0, 0, false };
static Font currentFont = { CURRENT_FONT_BITS, 0, 0, 0, false };

static unsigned long lastDataRefreshMs = 0;
static bool dataRefreshDue = true;

/* ------------------------------------------------------------------ */
/* Networking / time                                                   */
/* ------------------------------------------------------------------ */

static bool connectToWiFi()
{
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.printf("Connecting to WiFi '%s'", WIFI_SSID);
    for (int i = 0; i < 60 && WiFi.status() != WL_CONNECTED; ++i)
    {
        delay(500);
        Serial.print('.');
    }
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("\nWiFi connection failed");
        WiFi.mode(WIFI_OFF);
        return false;
    }
    Serial.printf("\nConnected, IP: %s\n", WiFi.localIP().toString().c_str());
    return true;
}

static void disconnectWiFi()
{
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    Serial.println("WiFi off");
}

static void syncUtcTime()
{
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    setenv("TZ", "UTC0", 1);
    tzset();

    Serial.print("Waiting for NTP time");
    time_t now = time(nullptr);
    for (int i = 0; i < 60 && now < 1600000000L; ++i)
    {
        delay(500);
        Serial.print('.');
        now = time(nullptr);
    }
    Serial.println();

    if (now < 1600000000L)
    {
        Serial.println("WARNING: NTP sync failed, using compile time");
    }
}

static bool fetchForecast()
{
    WiFiClientSecure client;
    client.setCACert(API_ROOT_CA);

    char apiUrl[256];
    snprintf(apiUrl, sizeof(apiUrl), API_URL_TEMPLATE, LATITUDE, LONGITUDE);

    HTTPClient http;
    http.begin(client, apiUrl);
    http.addHeader("apikey", API_KEY);

    int status = http.GET();
    if (status != HTTP_CODE_OK)
    {
        Serial.printf("API request failed: HTTP %d\n", status);
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();
    Serial.printf("Received %u bytes of forecast data\n", payload.length());

    /* Only keep the six fields the canvas actually uses. */
    DynamicJsonDocument filter(512);
    /* Keeping element [0] tells ArduinoJson to retain the whole array. */
    filter["features"][0]["properties"]["timeSeries"][0]["time"] = true;
    filter["features"][0]["properties"]["timeSeries"][0]["screenTemperature"] = true;
    filter["features"][0]["properties"]["timeSeries"][0]["significantWeatherCode"] = true;
    filter["features"][0]["properties"]["timeSeries"][0]["windSpeed10m"] = true;
    filter["features"][0]["properties"]["timeSeries"][0]["probOfPrecipitation"] = true;
    filter["features"][0]["properties"]["timeSeries"][0]["totalPrecipAmount"] = true;

    DynamicJsonDocument doc(24576);
    DeserializationOption::Filter deserializationFilter(filter);
    DeserializationError err = deserializeJson(doc, payload.c_str(),
                                               deserializationFilter);
    if (err)
    {
        Serial.printf("JSON parse failed: %s\n", err.c_str());
        return false;
    }

    JsonArray series = doc["features"][0]["properties"]["timeSeries"];
    if (series.isNull())
    {
        Serial.println("API response has no timeSeries");
        return false;
    }

    forecastCount = 0;
    for (JsonObject item : series)
    {
        if (forecastCount >= MAX_FORECAST_ROWS)
            break;

        const char *t = item["time"];
        if (t == nullptr)
            continue;

        ForecastRow &row = forecast[forecastCount++];
        strlcpy(row.time, t, sizeof(row.time));
        row.code = item["significantWeatherCode"] | -1;
        row.temp = item["screenTemperature"] | 0.0f;
        row.precip = item["totalPrecipAmount"] | 0.0f;
        row.prob = item["probOfPrecipitation"] | 0;
        row.wind = item["windSpeed10m"] | 0.0f;
    }

    /* The notebook sorts ascending by time before selecting hours. */
    for (size_t i = 1; i < forecastCount; ++i)
    {
        ForecastRow tmp = forecast[i];
        size_t j = i;
        while (j > 0 && strcmp(forecast[j - 1].time, tmp.time) > 0)
        {
            forecast[j] = forecast[j - 1];
            --j;
        }
        forecast[j] = tmp;
    }

    Serial.printf("Parsed %u forecast rows\n", forecastCount);
    return forecastCount > 0;
}

/* ------------------------------------------------------------------ */
/* Small drawing helpers                                               */
/* ------------------------------------------------------------------ */

/* VLW integers are stored big-endian, regardless of the host CPU. */
static uint32_t fontInt32(Font &font, size_t offset)
{
    return ((uint32_t)pgm_read_byte(font.bits + offset) << 24) |
           ((uint32_t)pgm_read_byte(font.bits + offset + 1) << 16) |
           ((uint32_t)pgm_read_byte(font.bits + offset + 2) << 8) |
           (uint32_t)pgm_read_byte(font.bits + offset + 3);
}

static void ensureFont(Font &font)
{
    if (font.ready)
        return;
    font.glyphCount = (uint16_t)fontInt32(font, 0);
    font.ascent = (int)fontInt32(font, 16);
    font.lineHeight = font.ascent + (int)fontInt32(font, 20);
    font.ready = true;
}

static bool findGlyph(Font &font, uint16_t code, FontGlyph &glyph)
{
    ensureFont(font);
    uint32_t bitmap = FONT_HEADER_SIZE + font.glyphCount * FONT_METRIC_SIZE;
    for (int i = 0; i < font.glyphCount; ++i)
    {
        size_t record = FONT_HEADER_SIZE + i * FONT_METRIC_SIZE;
        int w = (int)fontInt32(font, record + 8);
        int h = (int)fontInt32(font, record + 4);
        if ((uint16_t)fontInt32(font, record) == code)
        {
            glyph.code = code;
            glyph.height = (uint8_t)h;
            glyph.width = (uint8_t)w;
            glyph.advance = (uint8_t)fontInt32(font, record + 12);
            glyph.dY = (int16_t)fontInt32(font, record + 16);
            glyph.dX = (int8_t)fontInt32(font, record + 20);
            glyph.bitmap = bitmap;
            return true;
        }
        bitmap += (uint32_t)(w * h);
    }
    return false;
}

static int textWidth(Font &font, const char *text, float f)
{
    int width = 0;
    for (const char *p = text; *p != '\0'; ++p)
    {
        FontGlyph glyph = {};
        if (!findGlyph(font, (uint8_t)*p, glyph))
            findGlyph(font, '?', glyph);
        width += (int)lroundf(glyph.advance * f);
    }
    return width;
}

/* The framebuffer is 4-bpp: even pixels in the low nibble, odd in the high. */
static uint8_t backgroundGray(int x, int y)
{
    uint8_t byte = framebuffer[(size_t)y * CANVAS_WIDTH / 2 + x / 2];
    uint8_t nibble = (x & 1) ? (byte >> 4) : (byte & 0x0F);
    return nibble * 17;
}

static void drawGlyph(Font &font, const FontGlyph &glyph, int x, int y,
                      float f, uint8_t color)
{
    if (glyph.width == 0 || glyph.height == 0)
        return;

    int width = std::max(1, (int)lroundf(glyph.width * f));
    int height = std::max(1, (int)lroundf(glyph.height * f));
    int originX = x + (int)lroundf(glyph.dX * f);
    int originY = y + (int)lroundf((font.ascent - glyph.dY) * f);
    uint8_t foreground = color * 17;

    for (int dy = 0; dy < height; ++dy)
    {
        /* Sample at destination pixel centres so downscaling keeps strokes. */
        int sy = (2 * dy + 1) * glyph.height / (2 * height);
        for (int dx = 0; dx < width; ++dx)
        {
            int sx = (2 * dx + 1) * glyph.width / (2 * width);
            uint8_t alpha = pgm_read_byte(font.bits + glyph.bitmap +
                                          sx + sy * glyph.width);
            if (alpha == 0)
                continue;
            int px = originX + dx;
            int py = originY + dy;
            if (px < 0 || px >= CANVAS_WIDTH || py < 0 || py >= CANVAS_HEIGHT)
                continue;

            uint8_t gray = foreground;
            if (alpha != 0xFF)
            {
                uint8_t background = backgroundGray(px, py);
                gray = (uint8_t)((foreground * alpha +
                                  background * (255 - alpha) + 127) / 255);
            }
            epd_draw_pixel(px, py, gray, framebuffer);
        }
    }
}

static void drawText(Font &font, const char *text, int x, int y, float f,
                     TextAlign align, uint8_t color)
{
    int width = textWidth(font, text, f);
    if (align == ALIGN_CENTER)
        x -= width / 2;
    else if (align == ALIGN_RIGHT)
        x -= width;

    for (const char *p = text; *p != '\0'; ++p)
    {
        FontGlyph glyph = {};
        if (!findGlyph(font, (uint8_t)*p, glyph) && !findGlyph(font, '?', glyph))
            continue;
        drawGlyph(font, glyph, x, y, f, color);
        x += (int)lroundf(glyph.advance * f);
    }
}

/* Equivalent of the notebook's draw_text(): fractions of the icon box. */
static void drawIconLabel(Font &font, const char *text, int iconX, int iconY,
                          int size, float xFraction, float yFraction,
                          TextAlign align)
{
    int anchorX = iconX + (int)(xFraction * size);
    int baseline = iconY + (int)(yFraction * size);
    /* cv2.putText uses a baseline; our font is positioned by its top edge. */
    ensureFont(font);
    int top = baseline - font.ascent;
    drawText(font, text, anchorX, top, 1.0f, align, 0);
}

static const uint8_t *iconData(int code)
{
    for (int i = 0; i < weather_icon_count; ++i)
    {
        if (weather_icons[i].code == code)
            return weather_icons[i].data;
    }
    /* Unknown code: fall back to the "unknown" icon (-1) when present. */
    for (int i = 0; i < weather_icon_count; ++i)
    {
        if (weather_icons[i].code == -1)
            return weather_icons[i].data;
    }
    return weather_icons[0].data;
}

static void drawIcon(int code, int x, int y, int size)
{
    const uint8_t *data = iconData(code);
    for (int dy = 0; dy < size; ++dy)
    {
        int sy = dy * ICON_SIZE / size;
        int rowOffset = sy * (ICON_SIZE / 2);
        for (int dx = 0; dx < size; ++dx)
        {
            int sx = dx * ICON_SIZE / size;
            uint8_t byte = data[rowOffset + sx / 2];
            uint8_t nibble = (sx & 1) ? (byte >> 4) : (byte & 0x0F);
            epd_draw_pixel(x + dx, y + dy, nibble * 17, framebuffer);
        }
    }
}

/* Battery level, adapted from the original OWM_EPD47_epaper_v2.5 sketch.
 * GPIO36 (ADC1_CH0) reads the divider on the LilyGo T5 4.7 board.  The
 * eFuse-calibrated Vref is used when available, otherwise the default 1100 mV. */
static void drawBattery(int x, int y)
{
    static uint32_t vref = 1100;
    esp_adc_cal_characteristics_t adcChars;
    esp_adc_cal_value_t calType =
        esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11,
                                 ADC_WIDTH_BIT_12, 1100, &adcChars);
    if (calType == ESP_ADC_CAL_VAL_EFUSE_VREF)
        vref = adcChars.vref;

    float voltage = analogRead(36) / 4096.0f * 6.566f * (vref / 1000.0f);
    if (voltage <= 1.0f)
        return; /* No valid reading (e.g. running on USB only). */

    float level = 2836.9625f * powf(voltage, 4) -
                  43987.4889f * powf(voltage, 3) +
                  255233.8134f * powf(voltage, 2) -
                  656689.7123f * voltage + 632041.7303f;
    if (voltage >= 4.20f)
        level = 100.0f;
    if (voltage <= 3.20f)
        level = 0.0f;
    int percentage = (int)lroundf(std::max(0.0f, std::min(100.0f, level)));
    Serial.printf("Battery: %d%%, %.2fV\n", percentage, voltage);

    const int bodyWidth = 40, bodyHeight = 16;
    epd_draw_rect(x, y, bodyWidth, bodyHeight, 0, framebuffer);
    epd_fill_rect(x + bodyWidth, y + 5, 4, 6, 0, framebuffer);
    int fillWidth = (bodyWidth - 4) * percentage / 100;
    if (fillWidth > 0)
        epd_fill_rect(x + 2, y + 2, fillWidth, bodyHeight - 4, 0, framebuffer);

    char text[16];
    snprintf(text, sizeof(text), "%d%% %.1fV", percentage, voltage);
    drawText(iconFont, text, x + bodyWidth + 9, y, 1.0f, ALIGN_LEFT, 0);
}

/* ------------------------------------------------------------------ */
/* Date / forecast selection                                           */
/* ------------------------------------------------------------------ */

static int dayOfWeek(int year, int month, int day)
{
    static const int offset[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (month < 3)
        --year;
    return (year + year / 4 - year / 100 + year / 400 + offset[month - 1] + day) % 7;
}

static void selectForecast(int &currentIndex, int upcoming[FORECAST_COUNT])
{
    time_t now = time(nullptr);
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);

    char nowString[17];
    snprintf(nowString, sizeof(nowString), "%04d-%02d-%02dT%02d:%02dZ",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min);

    currentIndex = 0;
    for (size_t i = 0; i < forecastCount; ++i)
    {
        if (strcmp(forecast[i].time, nowString) <= 0)
            currentIndex = (int)i;
    }

    int count = 0;
    for (size_t i = 0; i < forecastCount && count < FORECAST_COUNT; ++i)
    {
        if (strcmp(forecast[i].time, nowString) > 0)
            upcoming[count++] = (int)i;
    }
    /* Notebook fallback: if nothing is in the future, use the first rows. */
    if (count == 0)
    {
        for (size_t i = 0; i < forecastCount && count < FORECAST_COUNT; ++i)
            upcoming[count++] = (int)i;
    }
    while (count < FORECAST_COUNT)
        upcoming[count++] = (int)forecastCount - 1;
}

/* ------------------------------------------------------------------ */
/* Canvas composition                                                  */
/* ------------------------------------------------------------------ */

static void composeImage()
{
    memset(framebuffer, 0xFF, CANVAS_WIDTH * CANVAS_HEIGHT / 2);

    /* Battery level in the top-left corner, above the current icon. */
    drawBattery(SIDE_MARGIN, 12);

    int currentIndex = 0;
    int upcoming[FORECAST_COUNT];
    selectForecast(currentIndex, upcoming);
    const ForecastRow &current = forecast[currentIndex];

    /* Current conditions: twice the forecast icon size, top left. */
    int currentX = (int)(SIDE_MARGIN * 1.5);
    int currentY = (int)(TOP_MARGIN * 1.5);
    int currentSize = ICON_SIZE * 2;
    char text[24];

    drawIcon(current.code, currentX, currentY, currentSize);
    snprintf(text, sizeof(text), "%d\xB0", (int)ceilf(current.temp));
    drawIconLabel(currentFont, text, currentX, currentY, currentSize, 0.06f, 0.80f, ALIGN_LEFT);
    snprintf(text, sizeof(text), "%dkts", (int)lroundf(current.wind * 1.943844f));
    drawIconLabel(currentFont, text, currentX, currentY, currentSize, 0.06f, 0.92f, ALIGN_LEFT);
    snprintf(text, sizeof(text), "%d%%", current.prob);
    drawIconLabel(currentFont, text, currentX, currentY, currentSize, 0.94f, 0.80f, ALIGN_RIGHT);
    snprintf(text, sizeof(text), "%.1fmm", current.precip);
    drawIconLabel(currentFont, text, currentX, currentY, currentSize, 0.94f, 0.90f, ALIGN_RIGHT);

    /* UTC clock in Ubuntu225 at native scale (1:1, no resampling). */
    time_t now = time(nullptr);
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);
    char clockString[6];
    snprintf(clockString, sizeof(clockString), "%02d:%02d",
             timeinfo.tm_hour, timeinfo.tm_min);

    int clockWidth = textWidth(clockFont, clockString, 1.0f);
    drawText(clockFont, clockString, CANVAS_WIDTH - SIDE_MARGIN - clockWidth,
             TOP_MARGIN * 3, 1.0f, ALIGN_LEFT, 0);

    /* Date from the current forecast row, above the clock. */
    static const char *weekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int year, month, day, hour, minute;
    sscanf(current.time, "%4d-%2d-%2dT%2d:%2d", &year, &month, &day, &hour, &minute);
    char dateString[32];
    snprintf(dateString, sizeof(dateString), "%s %02d %s %04d",
             weekdays[dayOfWeek(year, month, day)], day, months[month - 1], year);
    /* Date in Ubuntu42 at native scale (1:1, no resampling). */
    drawText(dateFont, dateString,
             CANVAS_WIDTH - SIDE_MARGIN - textWidth(dateFont, dateString, 1.0f),
             TOP_MARGIN, 1.0f, ALIGN_LEFT, 0);

    /* Six hourly icons along the bottom with their labels. */
    int span = CANVAS_WIDTH - 2 * SIDE_MARGIN - ICON_SIZE;
    int yPosition = CANVAS_HEIGHT - BOTTOM_MARGIN - ICON_SIZE;
    for (int i = 0; i < FORECAST_COUNT; ++i)
    {
        const ForecastRow &row = forecast[upcoming[i]];
        int x = SIDE_MARGIN + i * span / (FORECAST_COUNT - 1);
        drawIcon(row.code, x, yPosition, ICON_SIZE);

        char hourString[6];
        snprintf(hourString, sizeof(hourString), "%.5s", row.time + 11);
        drawIconLabel(iconFont, hourString, x, yPosition, ICON_SIZE, 0.5f, 0.14f,
                      ALIGN_CENTER);

        snprintf(text, sizeof(text), "%d\xB0", (int)ceilf(row.temp));
        drawIconLabel(iconFont, text, x, yPosition, ICON_SIZE, 0.06f, 0.80f,
                      ALIGN_LEFT);
        snprintf(text, sizeof(text), "%dkts", (int)lroundf(row.wind * 1.943844f));
        drawIconLabel(iconFont, text, x, yPosition, ICON_SIZE, 0.06f, 0.92f,
                      ALIGN_LEFT);
        snprintf(text, sizeof(text), "%d%%", row.prob);
        drawIconLabel(iconFont, text, x, yPosition, ICON_SIZE, 0.94f, 0.80f,
                      ALIGN_RIGHT);
        snprintf(text, sizeof(text), "%.1fmm", row.precip);
        drawIconLabel(iconFont, text, x, yPosition, ICON_SIZE, 0.94f, 0.90f,
                      ALIGN_RIGHT);
    }
}

static void refreshDisplay()
{
    epd_poweron();
    epd_clear();
    epd_draw_grayscale_image(epd_full_screen(), framebuffer);
    epd_poweroff();
}

/* ------------------------------------------------------------------ */
/* Arduino entry points                                                */
/* ------------------------------------------------------------------ */

void setup()
{
    Serial.begin(115200);
    delay(100);

    epd_init();
    size_t frameSize = (size_t)(CANVAS_WIDTH / 2) * CANVAS_HEIGHT;
    framebuffer = (uint8_t *)heap_caps_malloc(frameSize, MALLOC_CAP_SPIRAM);
    if (framebuffer == nullptr)
        framebuffer = (uint8_t *)malloc(frameSize);
    if (framebuffer == nullptr)
    {
        Serial.println("FATAL: could not allocate framebuffer");
        while (true)
            delay(1000);
    }
    memset(framebuffer, 0xFF, frameSize);
}

void loop()
{
    /* NTP time and forecast data are refreshed together, every half hour.
     * WiFi is only powered up for the duration of this cycle. */
    if (dataRefreshDue ||
        (millis() - lastDataRefreshMs >= DATA_REFRESH_INTERVAL_MS))
    {
        bool updated = false;
        if (connectToWiFi())
        {
            syncUtcTime();
            updated = fetchForecast();
            disconnectWiFi();
        }
        lastDataRefreshMs = millis();
        dataRefreshDue = false;
        Serial.println(updated ? "Forecast updated"
                               : "Keeping previous forecast");
    }

    /* The display itself (including the clock) updates every minute. */
    composeImage();
    refreshDisplay();
    Serial.println("Display updated");

    delay(REFRESH_INTERVAL_MS);
}
