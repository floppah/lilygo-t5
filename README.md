# Draw Images

Simple firmware for a ESP32 displaying a static image on an EPaper Screen.

## ESP-IDF

You need to install the `quilt` tool and execute `quilt push -a` in the current directory, which will automatically generate the `esp-idf` project file.

## 关于ESP32-S3

When using the `arduino` environment, the program may crash and restart frequently.

This is not the case with the development environment using `esp-idf`.

The reason for this occurrence has not yet been found.

If you know what the problem is, please let me know.

## Weather forecast firmware

`weather.ino` is a complete weather display: it connects to WiFi, synchronises
the clock over NTP, fetches the Met Office hourly forecast over HTTPS, composes
a 960x540 greyscale canvas (current conditions, UTC clock, date and six hourly
icons) and refreshes the e-paper display. It uses `ArduinoJson` (declared in
`platformio.ini`) and the embedded icon data in `weather_icons.h`.

Before building, copy `creds.h.example` to `creds.h` and fill in your WiFi
SSID/password and Met Office API key. `creds.h` is listed in `.gitignore` so
your credentials are never committed.

The icon header is generated from the 150x150 4-bit BMPs in `../icons`:

```sh
python3 tools/generate_icons.py
```

The clock, date, current-condition labels and forecast labels use `Ubuntu225.h`, `Ubuntu42.h`, `Ubuntu32.h` and `Ubuntu20.h` respectively, all at native size.
