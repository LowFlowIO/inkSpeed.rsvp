# inkSpeed — RSVP Reader

**v1.0.4** · open-source firmware · MIT

Website: [inkspeed.rsvp](https://inkspeed.rsvp/) · Source: [github.com/LowFlowIO/inkSpeed.rsvp](https://github.com/LowFlowIO/inkSpeed.rsvp)

A TXT-first RSVP reader for the Waveshare **ESP32-S3-ePaper-1.54 V2**
(the same board as [InkBoy](https://github.com/kuslota/InkBoy)): a 200×200
SSD1681 panel, 8 MB flash, 8 MB octal PSRAM, microSD, and two side buttons.

Words appear one at a time in the middle of the screen. The panel is driven
by InkBoy's custom mixed-mode pulse driver after a single vendor full refresh
at boot. There is no page-turner remote in this build.

## Recent changes

**v1.0.4** — new sleep/splash logo; Wi-Fi upload portal buttons use the same 8 px rounded corners as the site.

## Hardware

- **Waveshare ESP32-S3-ePaper-1.54 V2** (ESP32-S3-PICO-1-N8R8). Built and
  tested on the V2 board only.
- A FAT32 microSD card with UTF-8 `.txt` books in `/books`.
- A USB-C data cable.

## Install

**Easiest:** open [inkspeed.rsvp](https://inkspeed.rsvp/) in desktop Chrome,
Edge or Opera, plug the board in, and use **Flash it**. The page writes the
merged image over Web Serial. Firefox and Safari don't support Web Serial.

**With esptool:** the release image `inkspeed.bin` is merged (bootloader,
partition table and app) and goes at offset 0:

```sh
esptool.py --chip esp32s3 write_flash --flash_mode dio --flash_freq 40m --flash_size 8MB 0x0 inkspeed.bin
```

If the board doesn't connect, hold BOOT (Up) while plugging in or resetting
to enter download mode. **From source:** see [Build](#build).

Settings are stored in flash and reset if you erase it; books and bookmarks
live on the SD card.

## Warning

This firmware reuses InkBoy's custom waveforms and high-rate per-pixel
pulsing. Custom waveforms and high refresh rates can cause **permanent image
retention or panel damage**. Long-term panel life has not been validated.
Use it at your own risk.

After a crash, watchdog, or brownout the firmware refuses to start on its
own, so a fault cannot keep refreshing the panel. Power-cycle the board to
retry. Flashing this project replaces whatever was on the board.

## Controls

The two side buttons are named **Up** and **Down** on screen.

| Name | Button | GPIO |
| --- | --- | --- |
| Up | BOOT | 0 |
| Down | PWR | 18 |

Debounce is 30 ms. A hold is 1.5 s. Up and Down together for 2.0 s is power
off. A Down double-tap within 400 ms refreshes the panel (a vendor full
refresh that clears ghosting).

USB download mode still uses the physical BOOT button: hold BOOT while
resetting.

### Everywhere

| Input | Action |
| --- | --- |
| Up + Down for 2 s | Save and power off |
| Down double-tap | Refresh the panel |

### Home, Books, Settings

| Input | Action |
| --- | --- |
| Up tap | Select, or change the highlighted setting |
| Down tap | Next item |
| Up hold | Previous item |
| Down hold | Back (settings are saved) |

Home items: Books, Settings, Upload, Power off. There is no Continue item.
If a selected book was stopped part way through, a screen asks Continue or
Start over before it opens.

Word timing follows an absolute deadline so the average rate matches the selected WPM even when a panel scan is longer than one word slot; each word still appears for at least one scan. If a single scan already takes longer than the slot, the panel is the limit. Pause, rewind, settings and a full refresh resync the timeline.

Settings: Speed (100–700 WPM, steps of 25, default 300), Size (Small /
Medium / Large), Pauses (Off / Normal / Long), Count (0–3 s startup
countdown), Align (ORP or Center), Hints (On / Off), Guide. Hints Off hides
the button band while reading and centers the word on the panel. Guide is a
short scrollable page of the same controls.

### While reading

| Input | Action |
| --- | --- |
| Up tap | Play / pause |
| Down tap | Rewind to the previous sentence, and pause |
| Up hold | Settings, then return paused |
| Down hold | Save and return to the library |

On pause you get nearby text with the current word inverted, a percent
estimate, and the current WPM. The Finished screen waits for any button,
then returns Home.

### On-screen hints

Hints are two short lines in the small face, sized to fit the 200 px panel.
They name the same actions the buttons perform.

| Screen | Line 1 | Line 2 |
| --- | --- | --- |
| Menus | Up: Select / Down: Next | Hold Down: Back |
| Settings | Up: Change / Down: Next | Hold Down: Back |
| Playing | Up: Pause / Down: Rewind | Hold Up: Settings |
| Paused | Up: Play / Down: Rewind | Hold Up: Settings |
| Finished | Any Button: Home | |
| Messages | | Hold Down: Back |
| Upload | | Hold Down: Stop |

## Upload

Home → Upload starts a WPA2 hotspot and a one-page site.

| | |
| --- | --- |
| WiFi | inkSpeed |
| Password | inkspeed |
| Page | http://192.168.4.1 |

The page is the upload itself: drop or choose a file, leave **Prepare for
reading** on, and upload. Preparation runs in the browser and matches
`format_for_inkspeed.py`: strip markup, rejoin OCR line-break hyphens, drop
page numbers and repeated headers, and fold characters the panel cannot
draw. Books land in `/books` on the card as UTF-8 `.txt`. Each book in the list
has a Delete button. Hold Down on the board to stop the hotspot.

Power the board off before inserting or removing the card.

## Books

FAT32 microSD. UTF-8 `.txt` files in `/books` (subfolders allowed, up to 128
files). The whole file is never loaded into RAM. Bookmarks live in
`/books/.inkpulse/`. Settings and the last book live in NVS under
`inkpulse`. If a book file changes length, its bookmark is ignored and
reading starts at the beginning.

The built-in faces cover ASCII, Latin-1, en/em dashes, curly quotes, and
ellipsis. Anything else draws as a box. Abbreviations such as `Mr.` are
treated as the end of a sentence.

## Fonts

Three bitmap faces, rasterized from Inter Medium and reduced from a 4×
supersample so stems stay solid on the 1-bit panel:

| Face | Size | Used for |
| --- | --- | --- |
| Small | 15 px | Menus, hints, pause text |
| Medium | 22 px | Default RSVP word |
| Large | 32 px | Large RSVP word |

Regenerate with `tools/gen_font.py` after editing the face list. The
generator expects Inter Medium (or pass `--ttf`).

Page titles sit in a black bar with white text, so they are not another
menu row.

## How the panel is driven

One vendor full refresh at boot (the splash) establishes panel state. After
that, every screen is a mixed-mode target change: only pixels whose color
changed are pulsed, eight black pulses and eight white pulses, then a final
HOLD scan. Analog power drops after about 750 ms of idle and resumes before
the next scan. There is no periodic vendor full "clean"; Down double-tap is
the manual one.

Sleep paints the branded screen in mixed mode, drains it to the HOLD ending,
then cuts panel power and deep-sleeps. Only Up (BOOT / GPIO0) wakes the
board.

## Build

ESP-IDF 5.5 via PlatformIO (`espressif32@6.12.0`). From the project root:

```sh
pio run
pio run -t upload
pio device monitor
```

`sdkconfig.defaults` sets 8 MB DIO 40 MHz flash, octal PSRAM at 80 MHz, a
32 KiB main task stack, USB-Serial-JTAG, and the WiFi SoftAP. Bluetooth is
not enabled. If an older `sdkconfig` still has NimBLE in it, delete
`sdkconfig` and `sdkconfig.old` before building so the defaults win.

Host tests (no board required):

```sh
tools/run_host_tests.sh
```

## Layout

```
src/                 application
components/ssd1681_fast/   InkBoy's custom SSD1681 driver
tools/               font generator, host tests, the text formatter
examples/books/      a short sample .txt
third_party/         InkBoy upstream licence/provenance, Inter OFL
```

## Credits and licences

- **inkSpeed** original code: MIT (`LICENSE`).
- **Display driver:** the custom fast SSD1681 driver and pulse counters come
  from [InkBoy](https://github.com/kuslota/InkBoy) by kuslota (MIT,
  `third_party/inkboy/`). inkSpeed would not be possible without it.
- **Fonts:** bitmap faces rendered from [Inter](https://github.com/rsms/inter)
  Medium, SIL Open Font License 1.1 (`third_party/inter/OFL.txt`).
- **ESP-IDF** (Apache-2.0) and the Waveshare SSD1681 waveform tables.

Full details: `THIRD_PARTY_NOTICES.md`.

Note: saved settings use the NVS namespace `inkpulse` and bookmarks live in
`/books/.inkpulse/`. That is the project's pre-rename name, kept so existing
settings and bookmarks survive updates.
