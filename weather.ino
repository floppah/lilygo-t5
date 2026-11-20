/* ESP32 weather station for the LilyGo T5 4.7" e-paper display.
 *
 * The sketch connects to WiFi, fetches the Met Office hourly forecast and
 * reads upcoming iCloud CalDAV events, then composes a 960x540 greyscale
 * canvas (current conditions, date, up to three calendar boxes and six
 * hourly icons) in the e-paper framebuffer.  The panel refreshes only when
 * the image has actually changed.
 *
 * This is the ESP32 translation of ../weather.ipynb.  The icons come from
 * ../icons and are embedded as 4-bit greyscale data in weather_icons.h
 * (regenerate with tools/generate_icons.py).  The thermometer and raindrop
 * beside the current conditions come from the SVG icons in this directory and
 * are embedded as 1-bit data in mono_icons.h
 * (regenerate with tools/generate_mono_icons.py).
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <StreamString.h>
#include <ArduinoJson.h>
#include <algorithm>
#include <math.h>
#include <stdlib.h>
#include <esp_heap_caps.h>
#include <esp_adc_cal.h>
#include <string.h>
#include <time.h>

#include "epd_driver.h"
#include "Ubuntu42.h"
#include "Ubuntu20.h"
#include "Ubuntu25.h"
#include "Ubuntu32.h"
#include "weather_icons.h"
#include "mono_icons.h"
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

/* DigiCert Global Root G3 -- trust anchor for caldav.icloud.com and the
 * pNN-caldav.icloud.com partition hosts. */
static const char *ICLOUD_ROOT_CA =
    "-----BEGIN CERTIFICATE-----\n"
    "MIICPzCCAcWgAwIBAgIQBVVWvPJepDU1w6QP1atFcjAKBggqhkjOPQQDAzBhMQsw\n"
    "CQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3d3cu\n"
    "ZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBHMzAe\n"
    "Fw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVTMRUw\n"
    "EwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5jb20x\n"
    "IDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEczMHYwEAYHKoZIzj0CAQYF\n"
    "K4EEACIDYgAE3afZu4q4C/sLfyHS8L6+c/MzXRq8NOrexpu80JX28MzQC7phW1FG\n"
    "fp4tn+6OYwwX7Adw9c+ELkCDnOg/QW07rdOkFFk2eJ0DQ+4QE2xy3q6Ip6FrtUPO\n"
    "Z9wj/wMco+I+o0IwQDAPBgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAd\n"
    "BgNVHQ4EFgQUs9tIpPmhxdiuNkHMEWNpYim8S8YwCgYIKoZIzj0EAwMDaAAwZQIx\n"
    "AK288mw/EkrRLTnDCgmXc/SINoyIJ7vmiI1Qhadj+Z4y3maTD/HMsQmP3Wyr+mt/\n"
    "oAIwOWZbwmSNuJ5Q3KjVSaLtx9zRSX8XAbjIho9OjIgrqJqpisXRAL34VOKa5Vt8\n"
    "sycX\n"
    "-----END CERTIFICATE-----\n";

/* Layout: same numbers as the notebook. */
#define CANVAS_WIDTH EPD_WIDTH
#define CANVAS_HEIGHT EPD_HEIGHT
/* 4-bpp framebuffer: one byte covers two pixels. */
#define FRAMEBUFFER_BYTES ((size_t)(CANVAS_WIDTH / 2) * CANVAS_HEIGHT)
#define ICON_SIZE WEATHER_ICON_SIZE
#define SIDE_MARGIN 20
#define TOP_MARGIN 20
#define BOTTOM_MARGIN 20
/* Geometry of the large current-conditions icon.  The calendar boxes share
 * its bottom edge but are 30 px shorter, with the height taken off the top. */
#define CURRENT_ICON_X ((int)(SIDE_MARGIN * 1.5))
#define CURRENT_ICON_Y ((int)(TOP_MARGIN * 1.5) + 25)
#define CURRENT_ICON_SIZE (ICON_SIZE * 2)
/* Gap between a 1-bit glyph and the label it sits beside. */
#define ICON_LABEL_GAP 4
#define FORECAST_COUNT 6 /* hourly icons */
#define MAX_FORECAST_ROWS 64
#define CALENDAR_EVENT_COUNT 3
#define CALENDAR_WINDOW_DAYS 14
#define MAX_CALENDARS 12
#define MAX_ICAL_EVENTS 32
#define CALENDAR_TITLE_LEN 64
#define CALENDAR_NOTES_LEN 160
#define LOOP_INTERVAL_MS (5UL * 60UL * 1000UL)
/* Data is fetched on the wall clock at :01 and :31.  While the clock is still
 * unset (before the first NTP sync) retries use this interval instead. */
#define DATA_REFRESH_FALLBACK_MS (30UL * 60UL * 1000UL)
#define REFRESH_MINUTE_EARLY 1
#define REFRESH_MINUTE_LATE 31

/* Hard caps for HTTP response bodies.  HTTPClient::getString() buffers the
 * whole payload with no limit, so without these a large or hostile response
 * could exhaust the heap. */
#define MAX_FORECAST_BODY_BYTES (256UL * 1024UL)
#define MAX_CALDAV_BODY_BYTES (256UL * 1024UL)

/* The text fonts are TFT_eSPI "VLW" fonts: a header, one 28-byte metric record
 * per glyph and an 8-bit alpha bitmap per glyph.  Glyph count, ascent and
 * descent are read from each font header at runtime.  The date, current labels,
 * forecast labels and calendar title/time use Ubuntu42, Ubuntu32, Ubuntu20 and
 * Ubuntu25 at native scale (1:1), so no text is resampled. */
#define DATE_FONT_BITS Ubuntu42
#define ICON_FONT_BITS Ubuntu20
#define CURRENT_FONT_BITS Ubuntu32
#define CALENDAR_FONT_BITS Ubuntu25
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

struct CalendarEvent {
    time_t start;
    time_t duration;
    char title[CALENDAR_TITLE_LEN];
    char notes[CALENDAR_NOTES_LEN];
    bool allDay;
};

static const char *WEEKDAY_NAMES[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *MONTH_NAMES[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

static ForecastRow forecast[MAX_FORECAST_ROWS];
static size_t forecastCount = 0;
static uint8_t *framebuffer = nullptr;
static Font dateFont = { DATE_FONT_BITS, 0, 0, 0, false };
static Font iconFont = { ICON_FONT_BITS, 0, 0, 0, false };
static Font currentFont = { CURRENT_FONT_BITS, 0, 0, 0, false };
static Font calendarFont = { CALENDAR_FONT_BITS, 0, 0, 0, false };

static unsigned long lastDataRefreshMs = 0;
static time_t lastFetchTime = 0;
static bool dataRefreshDue = true;
static uint64_t lastDisplayedHash = 0;
static bool displayInitialized = false;
static int batteryPercent = 0;
static float batteryVoltage = 0.0f;
static bool batteryValid = false;
static CalendarEvent calendarEvents[MAX_ICAL_EVENTS];
static size_t calendarEventCount = 0;

/* ------------------------------------------------------------------ */
/* Networking / time                                                   */
/* ------------------------------------------------------------------ */

/* A StreamString that refuses to grow past a fixed byte budget.  Passing this
 * to HTTPClient::writeToStream() keeps response bodies bounded even when the
 * server uses chunked transfer encoding (where Content-Length is unknown). */
class BoundedStreamString : public StreamString
{
public:
    explicit BoundedStreamString(size_t limit) : limit_(limit) {}

    size_t write(const uint8_t *buffer, size_t size) override
    {
        if (overflowed_)
            return 0;
        size_t room = limit_ - length();
        if (size > room)
        {
            size_t written = room > 0 ? StreamString::write(buffer, room) : 0;
            overflowed_ = true;
            return written;
        }
        return StreamString::write(buffer, size);
    }

    size_t write(uint8_t data) override
    {
        if (overflowed_ || length() >= limit_)
        {
            overflowed_ = true;
            return 0;
        }
        return StreamString::write(data);
    }

    bool overflowed() const { return overflowed_; }

private:
    size_t limit_;
    bool overflowed_ = false;
};

static bool readLimitedBody(HTTPClient &http, String &body, size_t limit)
{
    int announced = http.getSize();
    if (announced > 0 && (size_t)announced > limit)
    {
        Serial.printf("Response body too large: %d bytes (limit %u)\n",
                      announced, (unsigned)limit);
        return false;
    }

    BoundedStreamString sink(limit);
    if (announced > 0)
        sink.reserve((size_t)announced + 1);
    else
        sink.reserve(16384);

    int result = http.writeToStream(&sink);
    if (sink.overflowed())
    {
        Serial.printf("Response body exceeded %u byte limit\n",
                      (unsigned)limit);
        return false;
    }
    if (result < 0)
    {
        Serial.printf("Reading response body failed: %d\n", result);
        return false;
    }

    body = static_cast<const String &>(sink);
    return true;
}

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
    /* time() stays UTC; TZ only affects mktime()/localtime_r() used when
     * interpreting calendar times that carry a TZID. */
    setenv("TZ", ICLOUD_TZ, 1);
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

    String payload;
    bool received = readLimitedBody(http, payload, MAX_FORECAST_BODY_BYTES);
    http.end();
    if (!received)
    {
        Serial.println("Forecast response rejected");
        return false;
    }
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
    std::sort(forecast, forecast + forecastCount,
              [](const ForecastRow &a, const ForecastRow &b) {
                  return strcmp(a.time, b.time) < 0;
              });

    Serial.printf("Parsed %u forecast rows\n", forecastCount);
    return forecastCount > 0;
}

/* ------------------------------------------------------------------ */
/* iCloud CalDAV calendar                                              */
/* ------------------------------------------------------------------ */

/* Namespace prefixes vary between servers (and iCloud uses both default
 * namespaces and d:/c: prefixes), so strip them before doing simple tag
 * lookups on the response XML. */
static String xmlStripPrefixes(const String &xml)
{
    String out;
    out.reserve(xml.length());
    for (int i = 0; i < xml.length();)
    {
        if (xml[i] != '<')
        {
            out += xml[i++];
            continue;
        }
        out += '<';
        ++i;
        if (i < xml.length() && xml[i] == '/')
        {
            out += '/';
            ++i;
        }
        int j = i;
        while (j < xml.length() && xml[j] != ':' && xml[j] != '>' &&
               xml[j] != ' ' && xml[j] != '\t' && xml[j] != '\r' &&
               xml[j] != '\n' && xml[j] != '/')
            ++j;
        if (j < xml.length() && xml[j] == ':')
            i = j + 1;
    }
    return out;
}

static bool xmlBlock(const String &xml, const char *tag, int from,
                     String &content, int &next)
{
    String open = String("<") + tag;
    int p = xml.indexOf(open, from);
    while (p >= 0)
    {
        int nameEnd = p + open.length();
        char c = xml.charAt(nameEnd);
        if (c == '>' || c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '/')
        {
            int gt = xml.indexOf('>', nameEnd);
            if (gt < 0)
                return false;
            if (gt > nameEnd && xml.charAt(gt - 1) == '/')
            {
                content = "";
                next = gt + 1;
                return true;
            }
            String close = String("</") + tag + ">";
            int ce = xml.indexOf(close, gt + 1);
            if (ce < 0)
                return false;
            content = xml.substring(gt + 1, ce);
            next = ce + close.length();
            return true;
        }
        p = xml.indexOf(open, p + 1);
    }
    return false;
}

static bool xmlValue(const String &xml, const char *tag, String &value)
{
    String content;
    int next = 0;
    if (!xmlBlock(xml, tag, 0, content, next))
        return false;
    value = content;
    value.trim();
    return true;
}

static String xmlUnescape(const String &s)
{
    String out;
    out.reserve(s.length());
    for (int i = 0; i < s.length(); ++i)
    {
        if (s[i] != '&')
        {
            out += s[i];
            continue;
        }
        int semi = s.indexOf(';', i);
        if (semi < 0 || semi - i > 10)
        {
            out += s[i];
            continue;
        }
        String ent = s.substring(i + 1, semi);
        if (ent == "amp") out += '&';
        else if (ent == "lt") out += '<';
        else if (ent == "gt") out += '>';
        else if (ent == "quot") out += '"';
        else if (ent == "apos") out += '\'';
        else if (ent.startsWith("#x") || ent.startsWith("#X"))
            out += (char)strtol(ent.substring(2).c_str(), nullptr, 16);
        else if (ent.startsWith("#"))
            out += (char)strtol(ent.substring(1).c_str(), nullptr, 10);
        else
            out += s.substring(i, semi + 1);
        i = semi;
    }
    return out;
}

static String calendarDataText(const String &data)
{
    int a = data.indexOf("<![CDATA[");
    if (a < 0)
        return xmlUnescape(data);
    a += 9;
    int b = data.indexOf("]]>", a);
    return b < 0 ? data.substring(a) : data.substring(a, b);
}

/* iCloud returns absolute URLs, including partition hosts such as
 * pXX-caldav.icloud.com.  Credentials are attached to every request, so only
 * ever follow HTTPS URLs on icloud.com -- never a host chosen by the response. */
static bool isTrustedCalDavUrl(const String &url)
{
    if (!url.startsWith("https://"))
        return false;

    /* Reject whitespace and control characters (request-splitting). */
    for (int i = 0; i < (int)url.length(); ++i)
    {
        uint8_t c = (uint8_t)url[i];
        if (c <= 0x20 || c == 0x7F)
            return false;
    }

    String authority = url.substring(8);
    int end = authority.length();
    for (int i = 0; i < (int)authority.length(); ++i)
    {
        if (authority[i] == '/' || authority[i] == '?' || authority[i] == '#')
        {
            end = i;
            break;
        }
    }
    authority = authority.substring(0, end);

    /* No userinfo tricks such as https://caldav.icloud.com@evil.example/. */
    if (authority.indexOf('@') >= 0)
        return false;

    /* Drop an optional :port suffix; iCloud does not use one. */
    int colon = authority.indexOf(':');
    if (colon >= 0)
        authority = authority.substring(0, colon);

    authority.toLowerCase();
    return authority == "icloud.com" || authority.endsWith(".icloud.com");
}

static String resolveCalDavUrl(const String &base, const String &href)
{
    String resolved;
    if (href.startsWith("http://") || href.startsWith("https://"))
    {
        resolved = href;
    }
    else
    {
        int scheme = base.indexOf("://");
        if (scheme < 0)
            return String();
        int hostStart = scheme + 3;
        int slash = base.indexOf('/', hostStart);
        String origin = slash < 0 ? base : base.substring(0, slash);
        if (href.startsWith("/"))
        {
            resolved = origin + href;
        }
        else
        {
            String path = slash < 0 ? String("/") : base.substring(slash);
            int lastSlash = path.lastIndexOf('/');
            resolved = origin + path.substring(0, lastSlash + 1) + href;
        }
    }

    if (!isTrustedCalDavUrl(resolved))
    {
        Serial.println("CalDAV: refusing untrusted URL");
        return String();
    }
    return resolved;
}

static bool calDavRequest(const String &url, const char *method,
                          const char *depth, const String &body,
                          String &response)
{
    if (!isTrustedCalDavUrl(url))
    {
        Serial.println("CalDAV: refusing request to untrusted URL");
        return false;
    }

    WiFiClientSecure client;
    client.setCACert(ICLOUD_ROOT_CA);

    HTTPClient http;
    http.begin(client, url);
    http.setAuthorization(ICLOUD_USERNAME, ICLOUD_APP_PASSWORD);
    http.addHeader("Depth", depth);
    http.addHeader("Content-Type", "application/xml; charset=utf-8");
    http.setTimeout(20000);

    int status = http.sendRequest(method, body);
    bool ok = false;
    if (status >= 200 && status < 300)
    {
        ok = readLimitedBody(http, response, MAX_CALDAV_BODY_BYTES);
        if (!ok)
            Serial.printf("CalDAV %s response rejected\n", method);
    }
    else
    {
        Serial.printf("CalDAV %s %s failed: HTTP %d\n", method, url.c_str(), status);
    }
    http.end();
    return ok;
}

/* Howard Hinnant's civil-date algorithm: days since 1970-01-01. */
static long daysFromCivil(int year, int month, int day)
{
    year -= month <= 2;
    int era = (year >= 0 ? year : year - 399) / 400;
    unsigned yoe = (unsigned)(year - era * 400);
    unsigned doy = (153 * (unsigned)(month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (long)era * 146097 + (long)doe - 719468;
}

static time_t makeCalendarTime(int year, int month, int day,
                               int hour, int minute, int second, bool utc)
{
    if (utc)
        return daysFromCivil(year, month, day) * 86400L +
               hour * 3600L + minute * 60L + second;
    struct tm timeinfo = {};
    timeinfo.tm_year = year - 1900;
    timeinfo.tm_mon = month - 1;
    timeinfo.tm_mday = day;
    timeinfo.tm_hour = hour;
    timeinfo.tm_min = minute;
    timeinfo.tm_sec = second;
    timeinfo.tm_isdst = -1;
    return mktime(&timeinfo);
}

/* "YYYYMMDDTHHMMSS" from a time_t, with an optional iCal 'Z' UTC suffix. */
static void formatUtcStamp(time_t t, char *out, size_t len, bool withZ)
{
    struct tm timeinfo;
    gmtime_r(&t, &timeinfo);
    snprintf(out, len, withZ ? "%04d%02d%02dT%02d%02d%02dZ"
                             : "%04d%02d%02dT%02d%02d%02d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
}

static String icsParam(const String &params, const char *name)
{
    int p = 0;
    while (p < params.length())
    {
        int end = params.indexOf(';', p);
        if (end < 0)
            end = params.length();
        String part = params.substring(p, end);
        int eq = part.indexOf('=');
        if (eq > 0 && part.substring(0, eq).equalsIgnoreCase(name))
        {
            String value = part.substring(eq + 1);
            value.trim();
            return value;
        }
        p = end + 1;
    }
    return String();
}

static bool parseIcsTime(const String &value, const String &params,
                         time_t &result, bool &allDay)
{
    String v = value;
    v.trim();
    bool utc = v.endsWith("Z");
    if (utc)
        v.remove(v.length() - 1);

    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    allDay = v.length() == 8 ||
             icsParam(params, "VALUE").equalsIgnoreCase("DATE");
    if (allDay)
    {
        if (sscanf(v.c_str(), "%4d%2d%2d", &year, &month, &day) != 3)
            return false;
    }
    else
    {
        if (sscanf(v.c_str(), "%4d%2d%2dT%2d%2d%2d",
                   &year, &month, &day, &hour, &minute, &second) < 5)
            return false;
    }
    result = makeCalendarTime(year, month, day, hour, minute, second, utc);
    return true;
}

/* keepLineBreaks: notes are drawn by drawWrappedText(), which understands
 * explicit newlines; titles are drawn on a single line, so a break there
 * would render as a missing glyph. */
static String icsUnescapeText(const String &s, bool keepLineBreaks = false)
{
    String out;
    out.reserve(s.length());
    for (int i = 0; i < s.length(); ++i)
    {
        if (s[i] == '\\' && i + 1 < s.length())
        {
            char c = s[++i];
            if (c == 'n' || c == 'N')
                out += keepLineBreaks ? '\n' : ' ';
            else out += c;
        }
        else
        {
            out += s[i];
        }
    }
    return out;
}

static void splitIcsProperty(const String &line, String &name,
                             String &params, String &value)
{
    int colon = -1;
    bool quoted = false;
    for (int i = 0; i < line.length(); ++i)
    {
        if (line[i] == '"') quoted = !quoted;
        else if (line[i] == ':' && !quoted) { colon = i; break; }
    }
    if (colon < 0)
    {
        name = line;
        params = "";
        value = "";
        return;
    }
    String head = line.substring(0, colon);
    value = line.substring(colon + 1);
    int semi = head.indexOf(';');
    if (semi < 0)
    {
        name = head;
        params = "";
    }
    else
    {
        name = head.substring(0, semi);
        params = head.substring(semi + 1);
    }
}

static String unfoldIcs(const String &ics)
{
    String out;
    out.reserve(ics.length());
    int start = 0;
    while (start <= ics.length())
    {
        int nl = ics.indexOf('\n', start);
        String line = nl < 0 ? ics.substring(start) : ics.substring(start, nl);
        if (line.endsWith("\r"))
            line.remove(line.length() - 1);
        if (!line.isEmpty() && (line[0] == ' ' || line[0] == '\t'))
        {
            if (!out.isEmpty())
                out += line.substring(1);
        }
        else
        {
            if (!out.isEmpty())
                out += '\n';
            out += line;
        }
        if (nl < 0)
            break;
        start = nl + 1;
    }
    return out;
}

static void addCalendarOccurrence(time_t start, time_t duration,
                                  const String &title, const String &notes,
                                  bool allDay, time_t windowStart,
                                  time_t windowEnd, const String &exdates)
{
    if (start < windowStart || start >= windowEnd ||
        calendarEventCount >= MAX_ICAL_EVENTS)
        return;

    char stamp[16];
    formatUtcStamp(start, stamp, sizeof(stamp), false);
    if (exdates.indexOf(stamp) >= 0)
        return;

    for (size_t i = 0; i < calendarEventCount; ++i)
    {
        if (calendarEvents[i].start == start &&
            strcmp(calendarEvents[i].title, title.c_str()) == 0)
            return;
    }

    CalendarEvent &event = calendarEvents[calendarEventCount++];
    event.start = start;
    event.duration = duration;
    event.allDay = allDay;
    strlcpy(event.title, title.c_str(), sizeof(event.title));
    strlcpy(event.notes, notes.c_str(), sizeof(event.notes));
}

static time_t addLocalDays(time_t t, int days)
{
    struct tm timeinfo;
    localtime_r(&t, &timeinfo);
    timeinfo.tm_mday += days;
    timeinfo.tm_isdst = -1;
    return mktime(&timeinfo);
}

static void addRecurringOccurrences(time_t first, time_t duration,
                                    const String &title, const String &notes,
                                    bool allDay, const String &rrule,
                                    const String &exdates,
                                    time_t windowStart, time_t windowEnd)
{
    int freq = 0;               /* 0 = daily, 1 = weekly */
    int interval = 1;
    int count = -1;
    time_t until = 0;
    int byDayMask = 0;

    int p = 0;
    while (p < rrule.length())
    {
        int end = rrule.indexOf(';', p);
        if (end < 0)
            end = rrule.length();
        String part = rrule.substring(p, end);
        int eq = part.indexOf('=');
        if (eq > 0)
        {
            String key = part.substring(0, eq);
            String value = part.substring(eq + 1);
            if (key.equalsIgnoreCase("FREQ"))
                freq = value.equalsIgnoreCase("WEEKLY") ? 1 : 0;
            else if (key.equalsIgnoreCase("INTERVAL"))
                interval = std::max(1, (int)value.toInt());
            else if (key.equalsIgnoreCase("COUNT"))
                count = value.toInt();
            else if (key.equalsIgnoreCase("UNTIL"))
            {
                bool ignored;
                parseIcsTime(value, "", until, ignored);
            }
            else if (key.equalsIgnoreCase("BYDAY"))
            {
                int q = 0;
                while (q < value.length())
                {
                    int comma = value.indexOf(',', q);
                    if (comma < 0) comma = value.length();
                    String day = value.substring(q, comma);
                    if (day.endsWith("SU")) byDayMask |= 1 << 0;
                    else if (day.endsWith("MO")) byDayMask |= 1 << 1;
                    else if (day.endsWith("TU")) byDayMask |= 1 << 2;
                    else if (day.endsWith("WE")) byDayMask |= 1 << 3;
                    else if (day.endsWith("TH")) byDayMask |= 1 << 4;
                    else if (day.endsWith("FR")) byDayMask |= 1 << 5;
                    else if (day.endsWith("SA")) byDayMask |= 1 << 6;
                    q = comma + 1;
                }
            }
        }
        p = end + 1;
    }

    struct tm timeinfo;
    localtime_r(&first, &timeinfo);
    int firstHour = timeinfo.tm_hour;
    int firstMinute = timeinfo.tm_min;
    int firstSecond = timeinfo.tm_sec;

    /* Same struct rolled back to local midnight: the first candidate day. */
    timeinfo.tm_hour = 0;
    timeinfo.tm_min = 0;
    timeinfo.tm_sec = 0;
    timeinfo.tm_isdst = -1;
    time_t day = mktime(&timeinfo);

    int emitted = 0;
    for (int dayIndex = 0; day < windowEnd && dayIndex < 365; ++dayIndex,
                            day = addLocalDays(day, 1))
    {
        if (until && day > until)
            break;
        if (count >= 0 && emitted >= count)
            break;

        bool include;
        if (freq == 1)
        {
            include = (dayIndex / 7) % interval == 0;
            if (include && byDayMask)
            {
                struct tm tmDay;
                localtime_r(&day, &tmDay);
                include = (byDayMask & (1 << tmDay.tm_wday)) != 0;
            }
        }
        else
        {
            include = dayIndex % interval == 0;
        }
        if (!include)
            continue;

        struct tm occurrence;
        localtime_r(&day, &occurrence);
        occurrence.tm_hour = firstHour;
        occurrence.tm_min = firstMinute;
        occurrence.tm_sec = firstSecond;
        occurrence.tm_isdst = -1;
        time_t start = mktime(&occurrence);
        if (start < first || start < windowStart)
            continue;
        addCalendarOccurrence(start, duration, title, notes, allDay,
                              windowStart, windowEnd, exdates);
        ++emitted;
    }
}

static void parseCalendarData(const String &ics,
                              time_t windowStart, time_t windowEnd)
{
    String lines = unfoldIcs(ics);
    bool inEvent = false;
    time_t start = 0, end = 0;
    String title, notes, rrule, exdates;
    bool allDay = false;
    int startPos = 0;

    while (startPos <= lines.length())
    {
        int nl = lines.indexOf('\n', startPos);
        String line = nl < 0 ? lines.substring(startPos) : lines.substring(startPos, nl);
        if (line == "BEGIN:VEVENT")
        {
            inEvent = true;
            start = end = 0;
            title = notes = rrule = exdates = "";
            allDay = false;
        }
        else if (line == "END:VEVENT" && inEvent)
        {
            inEvent = false;
            if (start == 0 || title.isEmpty())
                continue;
            time_t duration = end > start ? end - start : (allDay ? 86400 : 0);
            if (rrule.isEmpty())
                addCalendarOccurrence(start, duration, title, notes, allDay,
                                      windowStart, windowEnd, exdates);
            else
                addRecurringOccurrences(start, duration, title, notes, allDay,
                                        rrule, exdates, windowStart, windowEnd);
        }
        else if (inEvent)
        {
            String name, params, value;
            splitIcsProperty(line, name, params, value);
            if (name == "DTSTART")
                parseIcsTime(value, params, start, allDay);
            else if (name == "DTEND")
            {
                bool endAllDay;
                parseIcsTime(value, params, end, endAllDay);
            }
            else if (name == "SUMMARY")
                title = icsUnescapeText(value);
            else if (name == "DESCRIPTION")
                notes = icsUnescapeText(value, true);
            else if (name == "RRULE")
                rrule = value;
            else if (name == "EXDATE")
            {
                if (!exdates.isEmpty())
                    exdates += ',';
                exdates += value;
            }
        }
        if (nl < 0)
            break;
        startPos = nl + 1;
    }
}

static void sortCalendarEvents()
{
    std::sort(calendarEvents, calendarEvents + calendarEventCount,
              [](const CalendarEvent &a, const CalendarEvent &b) {
                  return a.start < b.start;
              });
    if (calendarEventCount > CALENDAR_EVENT_COUNT)
        calendarEventCount = CALENDAR_EVENT_COUNT;
}

static bool fetchCalendarEvents()
{
    const String root = "https://caldav.icloud.com/";
    const String principalBody =
        "<propfind xmlns=\"DAV:\"><prop><current-user-principal/></prop></propfind>";
    const String homeSetBody =
        "<propfind xmlns=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">"
        "<prop><c:calendar-home-set/></prop></propfind>";
    const String calendarListBody =
        "<propfind xmlns=\"DAV:\"><prop><displayname/><resourcetype/></prop></propfind>";

    String response;
    if (!calDavRequest(root, "PROPFIND", "0", principalBody, response))
        return false;

    String xml = xmlStripPrefixes(response);
    String block, href;
    int next = 0;
    if (!xmlBlock(xml, "current-user-principal", 0, block, next) ||
        !xmlValue(block, "href", href))
    {
        Serial.println("CalDAV: no current-user-principal in response");
        return false;
    }
    String principalUrl = resolveCalDavUrl(root, href);
    if (principalUrl.isEmpty())
    {
        Serial.println("CalDAV: untrusted principal URL");
        return false;
    }

    if (!calDavRequest(principalUrl, "PROPFIND", "0", homeSetBody, response))
        return false;
    xml = xmlStripPrefixes(response);
    if (!xmlBlock(xml, "calendar-home-set", 0, block, next) ||
        !xmlValue(block, "href", href))
    {
        Serial.println("CalDAV: no calendar-home-set in response");
        return false;
    }
    String homeUrl = resolveCalDavUrl(principalUrl, href);
    if (homeUrl.isEmpty())
    {
        Serial.println("CalDAV: untrusted calendar-home URL");
        return false;
    }

    if (!calDavRequest(homeUrl, "PROPFIND", "1", calendarListBody, response))
        return false;
    xml = xmlStripPrefixes(response);

    String calendarUrls[MAX_CALENDARS];
    size_t calendarCount = 0;
    int pos = 0;
    while (xmlBlock(xml, "response", pos, block, next))
    {
        /* iCloud writes <calendar xmlns="urn:ietf:params:xml:ns:caldav"/>,
         * so match the tag name rather than an exact literal string. */
        String calendarTag;
        int calendarNext = 0;
        if (xmlBlock(block, "calendar", 0, calendarTag, calendarNext))
        {
            String calendarHref;
            if (xmlValue(block, "href", calendarHref) &&
                calendarCount < MAX_CALENDARS)
            {
                String calendarUrl = resolveCalDavUrl(homeUrl, calendarHref);
                if (!calendarUrl.isEmpty())
                    calendarUrls[calendarCount++] = calendarUrl;
            }
        }
        pos = next;
    }
    Serial.printf("CalDAV: found %u calendars\n", calendarCount);

    time_t now = time(nullptr);
    time_t windowStart = now;
    time_t windowEnd = now + CALENDAR_WINDOW_DAYS * 86400L;
    char startStamp[17], endStamp[17];   /* "YYYYMMDDTHHMMSSZ" + NUL */
    formatUtcStamp(windowStart, startStamp, sizeof(startStamp), true);
    formatUtcStamp(windowEnd, endStamp, sizeof(endStamp), true);

    String reportBody =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<c:calendar-query xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">"
        "<d:prop><d:getetag/><c:calendar-data/></d:prop>"
        "<c:filter><c:comp-filter name=\"VCALENDAR\">"
        "<c:comp-filter name=\"VEVENT\">"
        "<c:time-range start=\"" + String(startStamp) +
        "\" end=\"" + String(endStamp) + "\"/>"
        "</c:comp-filter></c:comp-filter></c:filter></c:calendar-query>";

    calendarEventCount = 0;
    for (size_t i = 0; i < calendarCount; ++i)
    {
        if (!calDavRequest(calendarUrls[i], "REPORT", "1", reportBody, response))
            continue;
        String calendarXml = xmlStripPrefixes(response);
        int responsePos = 0;
        while (xmlBlock(calendarXml, "response", responsePos, block, next))
        {
            String data;
            int dataNext = 0;
            if (xmlBlock(block, "calendar-data", 0, data, dataNext))
                parseCalendarData(calendarDataText(data), windowStart, windowEnd);
            responsePos = next;
        }
    }

    sortCalendarEvents();
    Serial.printf("Parsed %u upcoming calendar events\n", calendarEventCount);
    return calendarEventCount > 0;
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

/* Find a glyph, falling back to '?' when the font lacks a character. */
static bool glyphFor(Font &font, char c, FontGlyph &glyph)
{
    return findGlyph(font, (uint8_t)c, glyph) || findGlyph(font, '?', glyph);
}

static int textWidth(Font &font, const char *text, float f)
{
    int width = 0;
    for (const char *p = text; *p != '\0'; ++p)
    {
        FontGlyph glyph = {};
        if (!glyphFor(font, *p, glyph))
            continue;
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
        if (!glyphFor(font, *p, glyph))
            continue;
        drawGlyph(font, glyph, x, y, f, color);
        x += (int)lroundf(glyph.advance * f);
    }
}

/* Equivalent of the notebook's draw_text(): fractions of the icon box. */
static void drawIconLabel(Font &font, const char *text, int iconX, int iconY,
                          int size, float xFraction, float yFraction,
                          TextAlign align, int yOffset = 0)
{
    int anchorX = iconX + (int)(xFraction * size);
    int baseline = iconY + (int)(yFraction * size) + yOffset;
    /* cv2.putText uses a baseline; our font is positioned by its top edge. */
    ensureFont(font);
    int top = baseline - font.ascent;
    drawText(font, text, anchorX, top, 1.0f, align, 0);
}

static const uint8_t *iconData(int code)
{
    const uint8_t *unknown = nullptr;
    for (int i = 0; i < weather_icon_count; ++i)
    {
        if (weather_icons[i].code == code)
            return weather_icons[i].data;
        /* Remember the "unknown" icon (-1) as the fallback. */
        if (unknown == nullptr && weather_icons[i].code == -1)
            unknown = weather_icons[i].data;
    }
    return unknown != nullptr ? unknown : weather_icons[0].data;
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

/* Draw one of the 1-bit mono_icons.h glyphs with its top-left at (x, y).  The
 * bitmaps mark 1 as background, so only the clear pixels are drawn. */
static void drawMonoIcon(const uint8_t *data, int width, int height, int x, int y)
{
    for (int dy = 0; dy < height; ++dy)
    {
        for (int dx = 0; dx < width; ++dx)
        {
            uint8_t byte = pgm_read_byte(data + dy * (width / 8) + dx / 8);
            if (byte & (0x80 >> (dx & 7)))
                continue;
            int px = x + dx;
            int py = y + dy;
            if (px < 0 || px >= CANVAS_WIDTH || py < 0 || py >= CANVAS_HEIGHT)
                continue;
            epd_draw_pixel(px, py, 0, framebuffer);
        }
    }
}

/* Vertical centre of a run of digits in `font` sitting on `baseline`; used to
 * line a 1-bit glyph up with the text drawn beside it. */
static int fontMidY(const Font &font, int baseline)
{
    int descent = font.lineHeight - font.ascent;
    return baseline - (font.ascent - descent) / 2;
}

/* Draw text with a 1-bit glyph on one side, both centred on `baseline`.  `x` is
 * the outer edge of the pair: the glyph's left edge when glyphLeft is true,
 * otherwise its right edge.  This is the layout of the temperature and
 * precipitation labels in both the current and the forecast blocks. */
static void drawGlyphLabel(Font &font, const char *text, int x, int baseline,
                           const uint8_t *glyph, int glyphWidth, int glyphHeight,
                           bool glyphLeft)
{
    ensureFont(font);
    int glyphX = glyphLeft ? x : x - glyphWidth;
    int glyphY = fontMidY(font, baseline) - glyphHeight / 2;
    drawMonoIcon(glyph, glyphWidth, glyphHeight, glyphX, glyphY);

    int textX = glyphLeft ? x + glyphWidth + ICON_LABEL_GAP
                          : x - glyphWidth - ICON_LABEL_GAP;
    drawText(font, text, textX, baseline - font.ascent, 1.0f,
             glyphLeft ? ALIGN_LEFT : ALIGN_RIGHT, 0);
}

/* Battery level, adapted from the original OWM_EPD47_epaper_v2.5 sketch.
 * GPIO36 (ADC1_CH0) reads the divider on the LilyGo T5 4.7 board.  The
 * eFuse-calibrated Vref is used when available, otherwise the default 1100 mV.
 *
 * The ADC is sampled once per data refresh and cached.  Sampling it while
 * composing the image let ADC noise change the drawn percentage every loop,
 * which made the framebuffer hash differ and forced a full refresh every five
 * minutes even when the forecast and calendar were unchanged. */
static void sampleBattery()
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
    {
        /* No valid reading (e.g. running on USB only). */
        batteryValid = false;
        Serial.println("Battery: no valid reading");
        return;
    }

    float level = 2836.9625f * powf(voltage, 4) -
                  43987.4889f * powf(voltage, 3) +
                  255233.8134f * powf(voltage, 2) -
                  656689.7123f * voltage + 632041.7303f;
    if (voltage >= 4.20f)
        level = 100.0f;
    if (voltage <= 3.20f)
        level = 0.0f;

    batteryVoltage = voltage;
    float clamped = std::max(0.0f, std::min(100.0f, level));
    batteryPercent = (int)(lroundf(clamped / 5.0f) * 5.0f);
    batteryValid = true;
    Serial.printf("Battery: %d%%, %.2fV\n", batteryPercent, batteryVoltage);
}

static void drawBattery(int rightEdge, int y)
{
    if (!batteryValid)
        return;

    char text[16];
    snprintf(text, sizeof(text), "%d%%", batteryPercent);

    const int bodyWidth = 40, bodyHeight = 16;
    int width = bodyWidth + 4 + 9 + textWidth(iconFont, text, 1.0f);
    int x = rightEdge - width;

    epd_draw_rect(x, y, bodyWidth, bodyHeight, 0, framebuffer);
    epd_fill_rect(x + bodyWidth, y + 5, 4, 6, 0, framebuffer);
    int fillWidth = (bodyWidth - 4) * batteryPercent / 100;
    if (fillWidth > 0)
        epd_fill_rect(x + 2, y + 2, fillWidth, bodyHeight - 4, 0, framebuffer);

    drawText(iconFont, text, x + bodyWidth + 9, y, 1.0f, ALIGN_LEFT, 0);
}

/* ------------------------------------------------------------------ */
/* Date / forecast selection                                           */
/* ------------------------------------------------------------------ */

static int dayOfWeek(int year, int month, int day)
{
    /* daysFromCivil() counts from 1970-01-01, which was a Thursday. */
    return (int)((daysFromCivil(year, month, day) + 4) % 7);
}

/* The API supplies timestamps like "YYYY-MM-DDTHH:MMZ".  The whole timestamp
 * is validated, but only the date fields the canvas uses are returned. */
static bool parseForecastTime(const char *text, int &year, int &month, int &day)
{
    int y = 0, m = 0, d = 0, h = 0, min = 0;
    if (sscanf(text, "%4d-%2d-%2dT%2d:%2d", &y, &m, &d, &h, &min) != 5)
        return false;
    if (y < 1970 || y > 9999 || m < 1 || m > 12 || d < 1 || d > 31 ||
        h < 0 || h > 23 || min < 0 || min > 59)
        return false;

    year = y;
    month = m;
    day = d;
    return true;
}

static void selectForecast(int &currentIndex, int upcoming[FORECAST_COUNT])
{
    if (forecastCount == 0)
    {
        currentIndex = 0;
        for (int i = 0; i < FORECAST_COUNT; ++i)
            upcoming[i] = 0;
        return;
    }

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
/* Calendar boxes                                                      */
/* ------------------------------------------------------------------ */

static void drawRoundRect(int x, int y, int width, int height, int radius,
                          uint8_t color, int thickness = 1)
{
    radius = std::min(radius, std::min(width, height) / 2);
    thickness = std::max(1, std::min(thickness, std::min(width, height) / 2));
    /* Draw the outline once per pixel of thickness, each pass one pixel
     * further in, so the border reads as a solid stroke. */
    for (int t = 0; t < thickness; ++t)
    {
        int rx = x + t;
        int ry = y + t;
        int rw = width - 2 * t;
        int rh = height - 2 * t;
        int r = std::max(0, radius - t);
        for (int dy = 0; dy <= r; ++dy)
        {
            int inset = r - (int)lroundf(sqrtf((float)(r * r - dy * dy)));
            epd_draw_pixel(rx + inset, ry + r - dy, color, framebuffer);
            epd_draw_pixel(rx + rw - 1 - inset, ry + r - dy, color, framebuffer);
            epd_draw_pixel(rx + inset, ry + rh - 1 - r + dy, color, framebuffer);
            epd_draw_pixel(rx + rw - 1 - inset, ry + rh - 1 - r + dy,
                           color, framebuffer);
        }
        for (int px = rx + r; px <= rx + rw - 1 - r; ++px)
        {
            epd_draw_pixel(px, ry, color, framebuffer);
            epd_draw_pixel(px, ry + rh - 1, color, framebuffer);
        }
        for (int py = ry + r; py <= ry + rh - 1 - r; ++py)
        {
            epd_draw_pixel(rx, py, color, framebuffer);
            epd_draw_pixel(rx + rw - 1, py, color, framebuffer);
        }
    }
}

static void drawTextFitted(Font &font, const char *text, int x, int y,
                           int maxWidth, float scale, TextAlign align,
                           uint8_t color)
{
    String value(text);
    if (textWidth(font, value.c_str(), scale) <= maxWidth)
    {
        drawText(font, value.c_str(), x, y, scale, align, color);
        return;
    }
    while (value.length() > 1 &&
           textWidth(font, (value + "...").c_str(), scale) > maxWidth)
        value.remove(value.length() - 1);
    value += "...";
    drawText(font, value.c_str(), x, y, scale, align, color);
}

/* Greedy word wrap used for calendar notes.  An explicit newline in the note
 * starts a new row; otherwise up to maxLines rows are drawn, and the final row
 * gets an ellipsis if the text does not fit. */
static void drawWrappedText(Font &font, const char *text, int x, int y,
                            int maxWidth, int lineHeight, int maxLines,
                            uint8_t color)
{
    String remaining(text);
    remaining.replace("\r\n", "\n");
    remaining.replace("\r", "\n");
    remaining.trim();

    int line = 0;
    while (line < maxLines && !remaining.isEmpty())
    {
        int newline = remaining.indexOf('\n');
        String paragraph =
            newline < 0 ? remaining : remaining.substring(0, newline);
        remaining = newline < 0 ? String() : remaining.substring(newline + 1);
        paragraph.trim();
        remaining.trim();
        if (paragraph.isEmpty())
            continue;

        while (!paragraph.isEmpty() && line < maxLines)
        {
            int len = 0;
            while (len < paragraph.length() &&
                   textWidth(font, paragraph.substring(0, len + 1).c_str(), 1.0f)
                       <= maxWidth)
                ++len;
            if (len == 0)
                len = 1;

            bool fitsWhole = len >= paragraph.length();
            bool more = !fitsWhole || !remaining.isEmpty();
            if (!fitsWhole && line < maxLines - 1)
            {
                int space = paragraph.lastIndexOf(' ', len - 1);
                if (space > 0)
                    len = space;
            }

            String row = paragraph.substring(0, len);
            row.trim();
            if (more && line == maxLines - 1)
            {
                while (row.length() > 1 &&
                       textWidth(font, (row + "...").c_str(), 1.0f) > maxWidth)
                    row.remove(row.length() - 1);
                row += "...";
            }
            drawText(font, row.c_str(), x, y + line * lineHeight, 1.0f,
                     ALIGN_LEFT, color);
            ++line;

            paragraph = paragraph.substring(len);
            paragraph.trim();
        }
    }
}

static void drawCalendarEvents()
{
    const int regionX = 350;                          /* clear of the current icon */
    const int regionRight = CANVAS_WIDTH - SIDE_MARGIN;
    const int regionWidth = regionRight - regionX;
    const int boxGap = 12;
    const int boxWidth =
        (regionWidth - (CALENDAR_EVENT_COUNT - 1) * boxGap) / CALENDAR_EVENT_COUNT;
    /* Bottom edge stays aligned with the current-conditions icon; the
     * reduced height is taken from the top. */
    const int boxHeight = CURRENT_ICON_SIZE - 30;
    const int boxTop = CURRENT_ICON_Y + CURRENT_ICON_SIZE - boxHeight;
    const int inset = 14;

    /* Draw only the boxes that have events, right-aligned in the region so
     * they fill the slots nearest the right edge. */
    const size_t shown = std::min(calendarEventCount,
                                  (size_t)CALENDAR_EVENT_COUNT);
    const int firstSlot = CALENDAR_EVENT_COUNT - (int)shown;

    for (size_t i = 0; i < shown; ++i)
    {
        int slot = firstSlot + (int)i;
        int x = regionX + slot * (boxWidth + boxGap);
        drawRoundRect(x, boxTop, boxWidth, boxHeight, 12, 0, 3);

        const CalendarEvent &event = calendarEvents[i];
        struct tm timeinfo;
        localtime_r(&event.start, &timeinfo);

        /* Title, rule, date, time, rule, notes -- spread over the full icon
         * height instead of the shorter box used before. */
        drawTextFitted(calendarFont, event.title, x + inset, boxTop + 20,
                       boxWidth - 2 * inset, 1.0f, ALIGN_LEFT, 0);
        epd_fill_rect(x + inset, boxTop + 56, boxWidth - 2 * inset, 1,
                      0, framebuffer);

        char dateLine[24];
        snprintf(dateLine, sizeof(dateLine), "%s %02d %s",
                 WEEKDAY_NAMES[timeinfo.tm_wday], timeinfo.tm_mday,
                 MONTH_NAMES[timeinfo.tm_mon]);
        drawText(iconFont, dateLine, x + inset, boxTop + 66,
                 1.0f, ALIGN_LEFT, 0);

        char timeLine[16];
        if (event.allDay)
            snprintf(timeLine, sizeof(timeLine), "All day");
        else
            snprintf(timeLine, sizeof(timeLine), "%02d:%02d",
                     timeinfo.tm_hour, timeinfo.tm_min);
        drawTextFitted(calendarFont, timeLine, x + inset, boxTop + 98,
                       boxWidth - 2 * inset, 1.0f, ALIGN_LEFT, 0);

        epd_fill_rect(x + inset, boxTop + 142, boxWidth - 2 * inset, 1,
                      0, framebuffer);
        if (event.notes[0] != '\0')
            drawWrappedText(iconFont, event.notes, x + inset, boxTop + 156,
                            boxWidth - 2 * inset, 26, 4, 0);
    }
}

/* ------------------------------------------------------------------ */
/* Canvas composition                                                  */
/* ------------------------------------------------------------------ */

static void composeImage()
{
    memset(framebuffer, 0xFF, FRAMEBUFFER_BYTES);

    /* Battery level in the top-right corner. */
    drawBattery(CANVAS_WIDTH - SIDE_MARGIN, 12);

    if (forecastCount == 0)
    {
        ensureFont(dateFont);
        drawText(dateFont, "No forecast data", CANVAS_WIDTH / 2,
                 CANVAS_HEIGHT / 2 - dateFont.ascent / 2, 1.0f,
                 ALIGN_CENTER, 0);
        return;
    }

    int currentIndex = 0;
    int upcoming[FORECAST_COUNT];
    selectForecast(currentIndex, upcoming);
    const ForecastRow &current = forecast[currentIndex];

    /* Current conditions: twice the forecast icon size, top left. */
    int currentX = CURRENT_ICON_X;
    int currentY = CURRENT_ICON_Y;
    int currentSize = CURRENT_ICON_SIZE;
    char text[24];

    drawIcon(current.code, currentX, currentY, currentSize);
    /* Time above the current icon, positioned like the forecast hour labels
     * but using the larger Ubuntu32 font. */
    snprintf(text, sizeof(text), "%.5s", current.time + 11);
    drawIconLabel(currentFont, text, currentX, currentY, currentSize,
                  0.5f, 0.14f, ALIGN_CENTER, 32);
    /* Current temperature, preceded by the thermometer glyph. */
    snprintf(text, sizeof(text), "%d\xB0", (int)ceilf(current.temp));
    drawGlyphLabel(currentFont, text, currentX + (int)(0.06f * currentSize),
                   currentY + (int)(0.80f * currentSize),
                   thermometer_icon_data, THERMOMETER_ICON_WIDTH,
                   THERMOMETER_ICON_HEIGHT, true);
    snprintf(text, sizeof(text), "%dkts", (int)lroundf(current.wind * 1.943844f));
    drawIconLabel(currentFont, text, currentX, currentY, currentSize, 0.06f, 0.92f, ALIGN_LEFT);
    /* Precipitation chance, with the raindrop glyph to its right. */
    snprintf(text, sizeof(text), "%d%%", current.prob);
    drawGlyphLabel(currentFont, text, currentX + (int)(0.94f * currentSize),
                   currentY + (int)(0.80f * currentSize),
                   raindrop_icon_data, RAINDROP_ICON_WIDTH,
                   RAINDROP_ICON_HEIGHT, false);
    snprintf(text, sizeof(text), "%.1fmm", current.precip);
    drawIconLabel(currentFont, text, currentX, currentY, currentSize, 0.94f, 0.90f, ALIGN_RIGHT);

    /* Date from the current forecast row, falling back to the system clock if
     * the API supplied a malformed timestamp. */
    int year = 0, month = 0, day = 0;
    if (!parseForecastTime(current.time, year, month, day))
    {
        time_t now = time(nullptr);
        struct tm timeinfo;
        gmtime_r(&now, &timeinfo);
        year = timeinfo.tm_year + 1900;
        month = timeinfo.tm_mon + 1;
        day = timeinfo.tm_mday;
    }
    char dateString[32];
    snprintf(dateString, sizeof(dateString), "%s %02d %s %04d",
             WEEKDAY_NAMES[dayOfWeek(year, month, day)], day,
             MONTH_NAMES[month - 1], year);
    /* Date in Ubuntu42 at native scale (1:1, no resampling), top left. */
    drawText(dateFont, dateString, SIDE_MARGIN, TOP_MARGIN,
             1.0f, ALIGN_LEFT, 0);

    drawCalendarEvents();

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
                      ALIGN_CENTER, 10);

        snprintf(text, sizeof(text), "%d\xB0", (int)ceilf(row.temp));
        drawGlyphLabel(iconFont, text, x + (int)(0.06f * ICON_SIZE),
                       yPosition + (int)(0.80f * ICON_SIZE),
                       thermometer_small_icon_data,
                       THERMOMETER_SMALL_ICON_WIDTH,
                       THERMOMETER_SMALL_ICON_HEIGHT, true);
        snprintf(text, sizeof(text), "%dkts", (int)lroundf(row.wind * 1.943844f));
        drawIconLabel(iconFont, text, x, yPosition, ICON_SIZE, 0.06f, 0.94f,
                      ALIGN_LEFT);
        snprintf(text, sizeof(text), "%d%%", row.prob);
        drawGlyphLabel(iconFont, text, x + (int)(0.94f * ICON_SIZE),
                       yPosition + (int)(0.80f * ICON_SIZE),
                       raindrop_small_icon_data, RAINDROP_SMALL_ICON_WIDTH,
                       RAINDROP_SMALL_ICON_HEIGHT, false);
        snprintf(text, sizeof(text), "%.1fmm", row.precip);
        drawIconLabel(iconFont, text, x, yPosition, ICON_SIZE, 0.94f, 0.92f,
                      ALIGN_RIGHT);
    }
}

/* 64-bit FNV-1a over the 4-bpp framebuffer.  Used to detect whether the newly
 * composed image differs from the one currently on the e-paper. */
static uint64_t framebufferHash()
{
    uint64_t hash = 14695981039346656037ULL; /* FNV-1a 64-bit offset basis */
    size_t size = FRAMEBUFFER_BYTES;
    for (size_t i = 0; i < size; ++i)
    {
        hash ^= framebuffer[i];
        hash *= 1099511628211ULL;            /* FNV-1a 64-bit prime */
    }
    return hash;
}

static void refreshDisplay()
{
    epd_poweron();
    epd_clear();
    epd_draw_grayscale_image(epd_full_screen(), framebuffer);
    epd_poweroff();
}

static bool timeIsValid()
{
    return time(nullptr) >= 1600000000L;
}

/* Start of the refresh slot that `now` falls inside (:01 or :31 minute), or 0
 * when the current minute is not a slot. */
static time_t refreshSlotStart(time_t now)
{
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    if (timeinfo.tm_min != REFRESH_MINUTE_EARLY &&
        timeinfo.tm_min != REFRESH_MINUTE_LATE)
        return 0;
    return now - timeinfo.tm_sec;
}

/* True when a slot is open and nothing has been fetched since it began. */
static bool refreshDue()
{
    if (dataRefreshDue)
        return true;
    if (!timeIsValid())
        return millis() - lastDataRefreshMs >= DATA_REFRESH_FALLBACK_MS;

    time_t now = time(nullptr);
    time_t slot = refreshSlotStart(now);
    /* The 60 s guard avoids a second fetch when a cycle happened to finish
     * just before the slot opened. */
    return slot != 0 && lastFetchTime < slot && now - lastFetchTime >= 60;
}

/* Milliseconds until the next fetch slot (:01 or :31 local time); 0 when one
 * is due now.  Falls back to a fixed interval while the clock is unset. */
static uint32_t msUntilNextRefresh()
{
    if (!timeIsValid())
    {
        unsigned long elapsed = millis() - lastDataRefreshMs;
        return elapsed >= DATA_REFRESH_FALLBACK_MS
                   ? 0
                   : (uint32_t)(DATA_REFRESH_FALLBACK_MS - elapsed);
    }

    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    int minute = timeinfo.tm_min;
    int target = minute < REFRESH_MINUTE_EARLY ? REFRESH_MINUTE_EARLY
               : minute < REFRESH_MINUTE_LATE  ? REFRESH_MINUTE_LATE
                                               : 60 + REFRESH_MINUTE_EARLY;
    int seconds = target * 60 - (minute * 60 + timeinfo.tm_sec);
    return seconds > 0 ? (uint32_t)seconds * 1000UL : 0;
}

/* ------------------------------------------------------------------ */
/* Arduino entry points                                                */
/* ------------------------------------------------------------------ */

void setup()
{
    Serial.begin(115200);
    delay(100);

    epd_init();
    size_t frameSize = FRAMEBUFFER_BYTES;
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
    /* NTP time, forecast and calendar are refreshed together at :01 and :31
     * past the hour.  WiFi is only powered up for the duration of that cycle. */
    if (refreshDue())
    {
        bool updated = false;
        bool calendarUpdated = false;
        if (connectToWiFi())
        {
            syncUtcTime();
            updated = fetchForecast();
            calendarUpdated = fetchCalendarEvents();
            disconnectWiFi();
        }
        sampleBattery();
        lastDataRefreshMs = millis();
        lastFetchTime = time(nullptr);
        dataRefreshDue = false;
        Serial.println(updated ? "Forecast updated"
                               : "Keeping previous forecast");
        Serial.println(calendarUpdated ? "Calendar updated"
                                       : "Keeping previous calendar events");
    }

    /* Compose into the framebuffer and only drive the e-paper when the image
     * has changed since the last refresh. */
    composeImage();
    uint64_t hash = framebufferHash();
    if (!displayInitialized || hash != lastDisplayedHash)
    {
        refreshDisplay();
        lastDisplayedHash = hash;
        displayInitialized = true;
        Serial.println("Display updated");
    }
    else
    {
        Serial.println("Display unchanged, skipping refresh");
    }

    /* Sleep up to the next slot, keeping the existing five minute heartbeat so
     * the image hash still gets rechecked now and then. */
    uint32_t wait = msUntilNextRefresh();
    if (wait == 0)
        wait = 1000;                     /* slot just served; recheck shortly */
    delay(wait < LOOP_INTERVAL_MS ? wait : LOOP_INTERVAL_MS);
}
