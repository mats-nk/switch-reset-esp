# Switch Reset for ESP32 / ESP8266

Factory-reset network switches over their serial console from a browser.
An ESP32 or ESP8266 sits between the switch's console port and your WiFi and offers:

- a **web terminal** (live serial console, type directly or line by line), and
- a **guided factory reset** with the same vendor profiles as the Python tool
  (Cisco IOS/IOS-XE, HP ProCurve / Aruba OS-Switch, Aruba CX, Juniper EX, Extreme EXOS, Arista EOS, Huawei VRP),
  automatic baud-rate detection, login handling (username/password are asked in the browser) and dialog answering.

```
Browser --WiFi--> ESP --UART (3.3 V)--> MAX3232 --RS-232--> console cable --> Switch
```

## Hardware

| Part | Notes |
|------|-------|
| ESP-01, Wemos D1 / D1 mini, NodeMCU (ESP8266); Wemos S2 mini (ESP32-S2); ESP32-C3; ESP32; ESP32-S3 | any dev board |
| **MAX3232 module** (3.3 V RS-232 <-> TTL) | **required** - switch consoles use RS-232 levels (+-12 V). Never connect an ESP32 pin directly. |
| Console cable | Cisco-style rollover (RJ45 -> DB9) or the cable that came with the switch |

Wiring (ESP32 -> MAX3232 module):

| ESP32 | MAX3232 | Default pins |
|-------|---------|--------------|
| `CONSOLE_TX_PIN` | TTL **RXD / TXIN** | ESP32: GPIO17, S3: GPIO17, C3: GPIO5 |
| `CONSOLE_RX_PIN` | TTL **TXD / ROUT** | ESP32: GPIO16, S3: GPIO18, C3: GPIO4 |
| 3V3 | VCC | |
| GND | GND | |

**ESP8266:** the console UART is fixed. The firmware uses UART0 swapped to **GPIO13 (D7) = RX** and **GPIO15 (D8) = TX**,
so connect the MAX3232 TTL TXD to D7 and TTL RXD to D8. The USB serial of the board is then free for flashing only;
debug output goes to GPIO2 (TX only). GPIO15 must be low at boot (dev boards already have a pull-down, keep it that way).
The FLASH button (GPIO0) works like the BOOT button on ESP32.

The RS-232 side must look like a PC (DTE): TX on DB9 pin 3, RX on pin 2, GND on pin 5. A module with a **male** DB9
connector plugs straight into the female DB9 end of a console cable; with a female connector add a null-modem adapter.
If the switch stays silent, swap TX/RX first.

Pins can be changed in `platformio.ini` (`-DCONSOLE_RX_PIN=..`, `-DCONSOLE_TX_PIN=..`).

**Not supported:** switches that only have a USB console port (e.g. Cisco mini-USB). That needs USB host mode;
use the RJ45 console port or a USB-serial adapter on a PC with the Python tool instead.

## Build script (all boards at once)

```bash
./build.sh              # builds everything and puts the binaries in the project root
./build.sh c3 s2        # only boards whose name contains "c3" or "s2"
./build.sh --list
```

| File in project root | Hardware | PlatformIO env | Console pins (RX / TX) |
|----------------------|----------|----------------|------------------------|
| `SwitchReset_ESP-01.bin`   | ESP-01 / ESP-01S        | `esp8266-esp01`  | GPIO3 / GPIO1 (UART0, no swap) |
| `SwitchReset_Wemos-D1.bin` | Wemos D1 / D1 mini      | `esp8266-d1mini` | GPIO13 (D7) / GPIO15 (D8) |
| `SwitchReset_NodeMCU.bin`  | NodeMCU v2 / v3 (ESP-12E) | `esp8266-nodemcu` | GPIO13 (D7) / GPIO15 (D8) |
| `SwitchReset_Wemos-S2.bin` | Wemos/LOLIN S2 mini     | `esp32-s2-mini`  | GPIO18 / GPIO17 |
| `SwitchReset_ESP32-C3.bin` | ESP32-C3 (Super Mini ..)| `esp32-c3`       | GPIO4 / GPIO5 |

Needs `pio` **or** [uv](https://docs.astral.sh/uv/) (then PlatformIO is fetched automatically). The host tests run first if `g++` exists.

The `.bin` files are the application images (what `pio run -t upload` writes and what OTA expects):

```bash
# ESP8266 (write at 0x0):
uv tool run esptool --chip esp8266 write_flash 0x0 SwitchReset_Wemos-D1.bin
# ESP32-C3 / S2 (app image at 0x10000; bootloader + partition table must already be on the chip,
# e.g. after one normal `pio run -e esp32-c3 -t upload`):
uv tool run esptool --chip esp32c3 write_flash 0x10000 SwitchReset_ESP32-C3.bin
```
For a blank ESP32 chip use `pio run -e <env> -t upload` once.

### Board notes

- **ESP-01:** only TX/RX are available, so the console uses UART0 directly: MAX3232 TTL TXD -> ESP-01 RX, TTL RXD -> ESP-01 TX.
  Flashing needs a USB-serial adapter with GPIO0 held low at power-up (disconnect the MAX3232 while flashing).
  The module needs a solid 3.3 V supply (~300 mA peaks). There is no button, so a lost web password is reset by
  erasing the flash (`esptool erase_flash`) and flashing again. At power-up the ESP-01 prints boot text on TX
  (74880 baud), which the switch console may show as a few junk characters.
- **Wemos S2 mini:** native USB (used for flashing only; debug text goes to UART0). First upload: hold button 0, press RST, release 0, then upload.
- **NodeMCU:** RX = D7, TX = D8 (same as Wemos D1). Use the board's FLASH button (GPIO0) for the factory reset at power-up.
- **Wemos D1:** the D1 mini and the larger D1 board use the same pins (D7/D8) and the same build.

## Build and flash (single board)

```bash
# PlatformIO via uv (no install needed):
uv tool run platformio run -e esp8266-d1mini -t upload # Wemos D1 mini (ESP8266)
uv tool run platformio run -e esp8266-nodemcu -t upload # NodeMCU / ESP-12E
uv tool run platformio run -e esp32dev -t upload       # classic ESP32
uv tool run platformio run -e esp32-s3 -t upload       # ESP32-S3
uv tool run platformio run -e esp32-c3 -t upload       # ESP32-C3 (Super Mini ...)
# or with an installed PlatformIO:  pio run -e esp32dev -t upload
```

Later updates also work over the air (ArduinoOTA, host `switch-reset`, password = web UI password):
`pio run -e esp32dev -t upload --upload-port switch-reset.local`

## First start

1. The ESP32 opens the WiFi **`SwitchReset-Setup`** (password `switchreset`).
2. Connect to it, the setup portal opens (or browse to 192.168.4.1). Pick your WiFi and set the **web UI password**.
3. Open `http://switch-reset.local/` (or the IP address shown on the serial monitor) and log in.

**Forgot the password / moved to another WiFi?** Hold the BOOT (ESP8266: FLASH) button while powering up for 3 seconds: WiFi settings
are cleared and the web password goes back to `admin`.

## Using it

- **Terminal:** shows everything the switch prints. Click into the black area to type directly, or use the input line.
  Buttons for Enter, Space and Ctrl-C. The baud selector changes the UART speed (default 9600).
- **Factory reset:** choose the switch type and the **reset mode**, type `YES`, press *Start reset*. Progress is shown in the log;
  if the switch asks for a username/password a dialog appears. *Abort* stops the job. While a reset runs the terminal
  is read-only (it still mirrors the switch output). When it is done, the terminal shows the reboot.

## Security notes

- The web UI is protected by a password, but HTTP/WebSocket are **not encrypted**. Use it on a trusted network only.
  A factory reset of a switch is destructive, so do not expose the device to the internet.
- Credentials typed for the switch are kept in RAM only for the running job.

## Project layout

```
platformio.ini              build environments (esp32dev, esp32-s3, esp32-c3)
src/main.cpp                WiFi setup, web server, WebSocket, UART bridge, job runner (ESP32 + ESP8266)
src/web_ui.h                the web page (embedded in flash)
lib/switchreset/profiles.*  switch profiles (add your own here)
lib/switchreset/engine.*    reset logic (platform independent)
lib/switchreset/textutil.*  prompt/dialog detection (no regex library needed)
tools/host_test/            tests on your PC against simulated switches
```

### Reset modes

Every switch type offers one or more modes; the first one is the default (same behaviour as before).
The command list of the selected mode is shown before you start.

| Switch type | Modes |
|-------------|-------|
| Cisco IOS / IOS-XE | Config reset + reboot (default) / Erase config only (no reboot) / Full factory reset (`factory-reset all`, Catalyst 9000, IOS-XE 16.9+) / Bootloader reset (older Catalyst 2960/3560/3750 ...) |
| HP ProCurve / Aruba OS-Switch | Config reset + reboot (default) / Full factory reset (`erase all`) |
| Aruba CX | Config reset + reboot (default) / Erase config only / Full factory reset (`erase all zeroize`) |
| Juniper EX | Zeroize + reboot (default) / Zeroize with media wipe (`request system zeroize media`) |
| Extreme EXOS | Full reset (`unconfigure switch all`, default) / Standard reset (`unconfigure switch`) |
| Arista EOS | Config reset + reboot (default) / Erase config only |
| Huawei VRP | Config reset + reboot (default) / Erase saved config only |

The "full factory reset" commands are not available on every model or software version; if the switch answers with an
error (`% Invalid input`, `Unrecognized command`) the job stops and shows it, nothing else is sent. Modes without reboot
leave the switch running with its current configuration until it is reloaded.

**Bootloader reset (older Catalyst):** these switches have no `factory-reset` command. Power-cycle the switch while holding the
MODE button until the `switch:` prompt appears (about 10-15 s, SYST LED), then start this mode: the tool runs
`flash_init`, `load_helper`, deletes `config.text`, `private-config.text` and `vlan.dat` (missing files are only warnings) and
runs `boot`. If the switch is not at the `switch:` prompt the job stops with a hint and sends nothing. With
`no service password-recovery` configured the switch asks at the MODE-button stage whether to reset to the default
configuration instead - answer `y` in the terminal.

### Adding a switch type or mode

Add `Step` arrays, a `Mode` list and a `Profile` entry in `lib/switchreset/profiles.cpp`. Dialog patterns are lower case,
case-insensitive; `*` matches anything, `|` separates alternatives. An answer of `"\r"` sends only Enter,
`" "` sends a space, anything else is sent followed by Enter. Mark the last rule `final` when the switch reboots, or set the step's `rebootOk` flag if the number of confirmations varies (silence after an answered dialog then counts as a reboot).

### Tests (no hardware needed)

```bash
./tools/host_test/run.sh        # needs only g++
```

The real engine runs against simulated Cisco, HP, Aruba CX, Juniper and Huawei switches (login, enable, initial dialog,
baud detection, errors, ROMMON, abort).

## ESP32 vs ESP8266

Both run the same code. The reset job runs cooperatively inside `loop()` and keeps the web server serviced while it waits,
so no RTOS is needed. ESP8266 notes: only 80 KB RAM (the terminal buffer is smaller and fast bursts of output may be
dropped in the browser view; the reset logic itself reads the UART directly and is not affected), UART pins are fixed
(GPIO13/GPIO15), the web password is stored in EEPROM instead of NVS, and the baud selector is limited to 115200.

## Differences from the Python tool

- Serial port selection is replaced by the fixed UART; baud detection is the same.
- Global dialog answers (e.g. the Cisco initial configuration dialog `no`) now always send Enter after the answer.
- Regular expressions were replaced by small matchers (`*` / `|` patterns), because regex is heavy on a microcontroller.
- Windows/macOS/Linux port handling and the log file do not exist here; the log is in the browser.

MIT license (see LICENSE).