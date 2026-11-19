# LilyGo T5 Weather Station

`weather.ino` is firmware for the LilyGo T5 4.7" e-paper board. It connects to
WiFi, synchronises the clock over NTP, fetches the Met Office hourly forecast
for a fixed location and refreshes the display with:

- current conditions (weather icon, temperature, wind and precipitation),
- the date,
- the next three iCloud Calendar events (within 14 days), in three rounded
  boxes side by side in the upper right of the panel, each showing the title,
  date, time and the event's notes (`DESCRIPTION`),
- the next six hourly forecasts,
- a battery indicator.

The main loop runs every five minutes. Forecast and calendar data are fetched
every 30 minutes while WiFi is on, and the screen is only refreshed when the
composed image has changed since the last refresh. The sketch uses
`ArduinoJson` (declared in `platformio.ini`), bundled fonts and the embedded
icon data in `weather_icons.h`.

## Credentials

Before building, copy `creds.h.example` to `creds.h` and fill in your WiFi
SSID/password, Met Office API key, forecast location (latitude/longitude) and
iCloud CalDAV Apple ID plus app-specific password (generated at
https://appleid.apple.com). `ICLOUD_TZ` is a POSIX timezone string used for
calendar times that carry a `TZID`.

```sh
cp creds.h.example creds.h
```

