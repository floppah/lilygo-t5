# LilyGo T5 Weather Station

`weather.ino` is firmware for the LilyGo T5 4.7" e-paper board. It connects to
WiFi, synchronises the clock over NTP, fetches the Met Office hourly forecast
for a fixed location and refreshes the display with:

- current conditions (weather icon, temperature, wind and precipitation),
- the UTC clock and date,
- the next six hourly forecasts,
- a battery indicator.

Forecast data is fetched every 30 minutes while the screen updates every
minute. WiFi is switched off between fetches. The sketch uses `ArduinoJson`
(declared in `platformio.ini`), bundled fonts and the embedded icon data in
`weather_icons.h`.

## Credentials

Before building, copy `creds.h.example` to `creds.h` and fill in your WiFi
SSID/password, Met Office API key and forecast location (latitude/longitude):

```sh
cp creds.h.example creds.h
```

