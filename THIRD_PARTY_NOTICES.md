# Third-party notices

inkSpeed's own source is MIT-licensed (see `LICENSE`). Everything else
that ships in this tree keeps its own license.

## InkBoy display driver and pulse counters

The following files were copied unchanged from a public source tarball of
[InkBoy](https://github.com/kuslota/InkBoy) commit
`823265ea5a9307749e5f61ea07eb7a40df793cc8`:

- `components/ssd1681_fast/` (custom SSD1681 driver, vendor waveform tables,
  grayscale waveform helpers)
- `src/drive_counters.c` and `src/drive_counters.h`

InkBoy itself is MIT-licensed; its license is reproduced in
`third_party/inkboy/LICENSE`. File hashes and the download URL are in
`third_party/inkboy/UPSTREAM.txt`. InkBoy's own third-party list is kept at
`third_party/inkboy/THIRD_PARTY_NOTICES.md`.

Board pin assignments used here (panel SPI and power, 1-bit SDMMC CLK/CMD/D0,
BOOT/PWR buttons, battery latch) follow that same InkBoy commit. The original
InkBoy application, Game Boy emulator, audio path, BLE/USB gamepad code, and
frame-dropping display scheduler are **not** included.

InkBoy's per-pixel driving approach was inspired by Wenting Zhang (Zephray)'s
[PaperBoy](https://gitlab.com/zephray/paperboy). No PaperBoy code is included
here.

## Waveshare e-Paper waveform tables

`components/ssd1681_fast/ssd1681_vendor_tables.h` contains two 159-byte arrays
that InkBoy reports as byte-identical to `WF_Full_1IN54` and
`WF_PARTIAL_1IN54_0` from Waveshare's e-Paper library,
https://github.com/waveshareteam/e-Paper (files
`RaspberryPi_JetsonNano/c/lib/e-Paper/EPD_1in54_V2.c` and
`Arduino/epd1in54_V2/epd1in54_V2.cpp`). Waveshare's repository has no top-level
LICENSE file and the header has no copyright line. The permission notice is
reproduced exactly as it appears there, including its spelling, with the
leading `#` comment markers removed:

> Permission is hereby granted, free of charge, to any person obtaining a copy
> of this software and associated documnetation files (the "Software"), to deal
> in the Software without restriction, including without limitation the rights
> to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
> copies of the Software, and to permit persons to  whom the Software is
> furished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in
> all copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
> FITNESS OR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
> AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
> LIABILITY WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
> OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
> SOFTWARE.

## ESP-IDF / PlatformIO

Firmware images built with PlatformIO link ESP-IDF (Apache-2.0, Espressif
Systems) and its bundled third-party libraries. ESP-IDF is not vendored here.
This project targets the official PlatformIO `espressif32@6.12.0` platform,
whose `framework-espidf` package `~3.50500.0` is ESP-IDF 5.5.0 — not
InkBoy's upstream ESP-IDF 6.1.

## Built-in bitmap fonts (Inter Medium)

`src/font_data.c` is a 1-bit bitmap rendering of Inter Medium, produced by
`tools/gen_font.py` (4x supersample, reduced). It is a Modified Version under
the SIL Open Font License 1.1, not a rebuilt outline font, and must not be
presented as the Inter font itself. The exact Inter release used to generate
the current tables was not recorded.

Copyright (c) 2016 The Inter Project Authors (https://github.com/rsms/inter)

The full license text, copied from the upstream repository's `LICENSE.txt`,
is in `third_party/inter/OFL.txt`. Regenerate the tables with:

```sh
python3 tools/gen_font.py --ttf /path/to/Inter-Medium.otf
```
