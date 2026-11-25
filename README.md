# HamProp Display

A tiny round radio-propagation report for amateur radio, living on an ESP32-C3 and a 1.28" GC9A01 screen.
It shows which HF bands are open **right now** from your grid locator, using live ionosonde data, so you can
stop refreshing a website and start refreshing your antenna tuner instead.

## What it shows

Tap the BOOT button to cycle through the pages:

| Page | Content |
|---|---|
| 1 | Band ring (80, 40, 30, 20, 17, 15, 12, 10 m) coloured open / marginal / closed, with MUF, LUF, SFI and K in the middle |
| 2 | Solar details: SFI, A and K index, sunspot number, solar wind, X-ray class |
| 3 | The ionosondes nearest to you: distance, MUF(3000) and foF2 |
| 4 | Band table with frequency and status |
| 5 | A pixel-art hamster running in a wheel. It is purely decorative. No, it does not power the device. Probably. |

Hold BOOT for 3 seconds to wipe Wi-Fi and locator and return to setup mode.

## How the numbers are made

- **MUF and foF2**: real measurements. The firmware fetches the current ionosonde readings from
  [KC2G](https://prop.kc2g.com/) (which aggregates GIRO data), keeps the stations within 1500 km of your locator
  and averages them, weighted by distance. Readings older than 3 hours are ignored.
- **SFI, A, K, sunspots, solar wind, X-ray**: from [HamQSL](https://www.hamqsl.com/solar.html) (N0NBH).
- **LUF**: nobody publishes this, so it is an **estimate** (the display marks it `LUF~`). It is derived from the
  sun's elevation at your location, the solar flux and the K index. Treat it as a rough D-layer absorption guide,
  not as gospel.
- **Band status**: a band is open if it sits between the LUF and 85% of the MUF, marginal near either edge, and
  closed otherwise. The MUF is the 3000 km figure, so short NVIS paths and exotic ones will differ.

## Hardware

- ESP32-C3 Super Mini
- 1.28" round GC9A01 240x240 SPI display

| Display | ESP32-C3 |
|---|---|
| VCC | 3V3 |
| GND | GND |
| RST | GPIO 0 |
| CS | GPIO 1 |
| DC | GPIO 10 |
| SDA (MOSI) | GPIO 3 |
| SCL (SCLK) | GPIO 4 |
| BOOT button | GPIO 9 (built in) |

## Setup

1. Flash the firmware (see below).
2. On first boot the board opens a Wi-Fi access point called `HamProp-Setup`. Join it from a phone or laptop
   (the portal should pop up, otherwise open `192.168.4.1`).
3. Enter your 2.4 GHz Wi-Fi credentials and your **grid locator** (4 or 6 characters, for example `JO62` or
   `JO62qm`). The ESP32-C3 cannot use 5 GHz networks.
4. The board syncs the time, fetches the data and draws the ring. Data refreshes every 10 minutes.

## Build and flash

Needs [PlatformIO](https://platformio.org/). It currently wants Python 3.10 to 3.13, so if your system Python is
newer, use a venv.

```sh
pio run -t upload
pio device monitor        # 115200 baud, USB CDC
```

No toolchain handy? Build a single image with `esptool merge_bin` (bootloader at `0x0`, partitions at `0x8000`,
`boot_app0` at `0xe000`, app at `0x10000`) and flash it at offset `0x0` with
[esptool-js](https://espressif.github.io/esptool-js/) in Chrome or Edge.

Layout:

```
platformio.ini        board, libraries, partition table
include/config.h      default locator, refresh interval, data URLs
include/lgfx_config.hpp   display driver and pins
include/hamster.h     the hamster (pixel art, drawn in code)
src/main.cpp          everything else
```

## Inspiration and credits

This project would not exist without:

- **[ESP32-Plane-Radar](https://github.com/MatixYo/ESP32-Plane-Radar)** by MatixYo (MIT). This hardware, the
  wiring, and the idea of a round-display gadget that sips live data from the internet come from there. The
  device was originally running that firmware, and the pin mapping here is the same. If you want planes
  instead of ionospheric physics, go flash that one.
- **[funkpower.de](https://funkpower.de/)** and its Funkwetter-Display, which inspired the idea of a
  per-band propagation report on a small display. This is an independent project with its own code and its own
  data sources, not a copy or a clone. If you want a finished, nicely made product, support them.
- Data from **[KC2G](https://prop.kc2g.com/)**, **[GIRO](https://giro.uml.edu/)** and the ionosonde operators
  behind them, and from **[HamQSL / N0NBH](https://www.hamqsl.com/)**.
- Libraries: [LovyanGFX](https://github.com/lovyan03/LovyanGFX),
  [WiFiManager](https://github.com/tzapu/WiFiManager) and [ArduinoJson](https://arduinojson.org/).

## Disclaimer

**Made with AI, soldered by humans.** The code was written in a back-and-forth with an AI assistant (Claude).
The solder joints, the USB cable wrangling and all the "why is the screen black" moments were human.

Please read the fine print:

- This is a hobby gadget, not a forecasting tool. The MUF is measured at a handful of sounders and averaged, the
  LUF is a guess, and the band status is a simple rule. Real propagation depends on path, time, season, antenna,
  power, mode, and the mood of the ionosphere.
- If the display says a band is closed and you work a DX station on it anyway, the ionosphere was wrong, not you.
  If it says a band is open and you hear nothing, check your antenna, your cables and your coffee level.
- Data comes from third-party public services that may change, rate-limit or disappear. Please be a good
  citizen and do not lower the refresh interval. Their servers are run by volunteers.
- The HTTPS connections skip certificate validation to keep the code small. That is acceptable for public,
  read-only data on a desk gadget, and not something to copy into anything that handles secrets.
- No warranty of any kind. The hamster is not a certified power source.

73!
