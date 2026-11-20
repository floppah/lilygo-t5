# LilyGo T5 Weather Station

`weather.ino` is firmware for the LilyGo T5 4.7" e-paper board. It connects to
WiFi, synchronises the clock over NTP, fetches the Met Office hourly forecast
for a fixed location and refreshes the display with:

- current conditions (weather icon, time, temperature, wind and precipitation),
  with a thermometer beside the temperature and a raindrop beside the
  precipitation chance,
- the date,
- the next three iCloud Calendar events (within 14 days), in three rounded
  boxes side by side in the upper right of the panel, each showing the title,
  date, time and the event's notes (`DESCRIPTION`),
- the next six hourly forecasts, each with a smaller thermometer and raindrop
  beside its temperature and precipitation chance,
- a battery indicator (leave it off with `-D HAS_BATTERY=false`).

The main loop keeps a five-minute heartbeat. Forecast and calendar data are
fetched at :01 and :31 past the hour (with a fixed fallback interval until NTP
has set the clock), and the screen is only refreshed when the composed image has
changed since the last refresh. The battery is sampled five times half a second
apart, averaged and rounded to the nearest 5 %.

The sketch uses `ArduinoJson` (declared in `platformio.ini`), the bundled fonts
and the icon data in `weather_icons.h` and `mono_icons.h`.

## Icons

`weather_icons.h` holds the 150x150 4-bit weather icons and `mono_icons.h` the
1-bit thermometer and raindrop glyphs. Both are generated from the original
artwork and should not be edited by hand:

```sh
python3 tools/generate_icons.py --icons <directory of NNN.bmp icons>
python3 tools/generate_mono_icons.py
```

`generate_icons.py` converts the 4-bit BMP set, replacing the "MIST" and
"DRIZZLE" lettering in the source artwork with light-grey tildes and raindrops.
`generate_mono_icons.py` rasterises the thermometer and raindrop SVGs in
`unused/` at the sizes used by the sketch (beside Ubuntu32 text in the current
block and Ubuntu20 text in the forecast rows).

## Building

The project builds with PlatformIO:

```sh
pio run -e t5-4-7 -t upload
```

If the board is not detected automatically, pass the serial port explicitly,
e.g. `--upload-port /dev/cu.usbserial-0001`. Battery sampling can be disabled
at build time with `-D HAS_BATTERY=false` (the indicator is then left off the
canvas and the ADC is not read).

## Credentials

Before building, copy `creds.h.example` to `creds.h` and fill in your WiFi
SSID/password, Met Office API key, forecast location (latitude/longitude) and
iCloud CalDAV Apple ID plus app-specific password (generated at
https://appleid.apple.com). `ICLOUD_TZ` is a POSIX timezone string used for
calendar times that carry a `TZID`.

```sh
cp creds.h.example creds.h
```
