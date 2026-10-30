# CLAUDE.md – Hidden Story escape room firmware

Context file for Claude Code sessions (and humans). `README.md` has the full topology, protocols
and build steps; this file carries conventions, decisions, current status and the open work.

## What this repo is

- Escape room controller: ESP32 master (`ESP_Master`), WLED gateway ESP32 (`ESP_WLED_svetla`),
  room 3 bridge ESP32 (`ESP_Mistnost3`) and eight AVR Arduino puzzle modules (`Arduino_*`).
- Comments, identifiers and serial log strings are Czech (serial strings ASCII-only). Keep that.
- Files are UTF-8 with CRLF line endings (git `core.autocrlf=true`).
- `Web_app/index.html` is the dashboard source. `ESP_Master/dashboard_html.h` is GENERATED from it
  (UTF-8) by `Web_app/build_dashboard.ps1` – never hand-edit the header, regenerate it.

## Build and verify

- Toolchain: arduino-cli bundled with Arduino IDE 2 (path in `.vscode/tasks.json`),
  cores `arduino:avr` 1.8.8 and `esp32:esp32` 3.3.x, libraries EthernetENC, LiquidCrystal I2C,
  Adafruit VS1053, SD, Servo.
- AVR: `arduino-cli compile --fqbn arduino:avr:uno <folder>`
- ESP32: `arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=huge_app ESP_Master`
  (`ESP_Master` also carries `sketch.yaml` and `partitions.csv`). Room 3 and WLED build with the
  plain `esp32:esp32:esp32` string and sit at 69 % / 78 % of the default app partition.
- Compile-check all 11 sketches after any change. A cold ESP32 core cache takes minutes per FQBN.
- No Python on the dev machines; scripts are PowerShell 5.1.

## Design decisions already made (do not undo silently)

- Lebka opens its own box after its 7 s countdown; the master no longer auto-sends `A`.
- Master sends `M` to Tlačítka when the Světla cascade starts (arms the 30 s inactivity timer).
- Tlačítka status pulses 4/5 are handled in every mode except 3; they last 1 s and were lost
  during the 17 s laser alarm.
- Master prints `TELEMETRY:{json}` on USB on change and every 2 s (Web Serial dashboard).
- Developer diagnostics (`X1`, `D<n>`, `I`) stay in firmware; no dashboard UI for them.
- Laser ignore switch `Y`/`Z` (added by the user 2026-10-11).
- WLED gateway pings one strip per 3.3 s with a 150 ms connect timeout and handles WiFi loss.
- Room 3 queues ESP-NOW messages and does I2C only in `loop()`.
- Manual activation of room 3 audio rhythm: web/USB command `R` -> master sends ESP-NOW `A` to I2C 21 (Audio 3), playable in both game and maintenance modes.

## Status 2026-10-13

- Everything compiled clean on 2026-10-10. The user's 2026-10-11 edits (laser ignore, thresholds,
  Tlačítka inactivity only when no attempt is in progress) were not re-verified by a compile.
- The installation reportedly crashes often. A full communications/error-handling audit was done;
  root causes are listed below. NONE of the hardening is implemented yet.

## Audit findings (why it crashes), most likely first

1. `ESP_Master` can hang forever in the EthernetENC driver: `Enc28J60Network::sendPacket` waits
   unbounded for TXIF/TXERIF and `phyRead` (used by `Ethernet.linkStatus()` every 2 s) spins on
   `MISTAT_BUSY`. A dead/glitching ENC28J60 or bad SPI wiring freezes the whole game loop. The
   task watchdog in this core build (`CONFIG_ESP_TASK_WDT_INIT=y`, 5 s) watches only IDLE0 on
   core 0; `loop()` runs on core 1, so a frozen loop is never caught.
2. `Arduino_Svetla`, `Arduino_Tlacitka`, `Arduino_Kola` call `Log()` -> `Serial.println()` inside
   the TWI `receiveEvent` ISR. The AVR core (HardwareSerial.cpp) then polls UDRE with interrupts
   off when the TX buffer is full: ~30 ms at 9600 Bd. The slave stretches SCL that long, the ESP32
   I2C hardware times out at ~13 ms -> timeouts, modules flapping offline, corrupted LCD (shares
   the bus), possibly a wedged bus. Happens exactly on mode changes (several commands in a row).
3. `Arduino_Laser` parses Lebka's serial replies with `String` every 150 ms on a 2 kB-RAM chip
   (heap fragmentation) and accepts any noise; a corrupted line can yield a false "all crystals".
4. `Arduino_Audio_3` has ~700 B RAM free with log strings in RAM (SD + VS1053 need stack).
5. LCD is rewritten every 400 ms (~130 bus transactions, ~40 % bus load) interleaved with polling.
6. Master has no I2C error accounting or bus recovery; nothing records the reset reason.
7. Hardware suspects: brownout (ESP32 + ENC28J60 ~180 mA + LCD backlight on a dev-board LDO),
   long I2C cables / weak pull-ups.

## Hardening plan (todo, priority order)

1. `ESP_Master`: `esp_task_wdt_reconfigure` to 30 s with `trigger_panic`, `esp_task_wdt_add(NULL)`
   for loopTask, `esp_task_wdt_reset()` once per `loop()`. Arm early in `setup()`.
2. `ESP_Master`: before `Ethernet.begin` call `Ethernet.init(cs)` then `Enc28J60Network::getrev()`
   (public static, returns 0 when absent); skip Ethernet entirely when 0, re-probe every 10 s,
   guard `maintain()/handleHttpClient()/linkStatus()` with the presence flag.
3. Světla/Tlačítka/Kola: in `receiveEvent` keep only `i2c_req` (0x99/0x98) handling, push other
   bytes to an 8-byte ring buffer, process in `loop()` (pattern already used in `Arduino_Laser`).
4. I2C self-healing: master counts `endTransmission()` codes 4/5 (bus) separately from 2/3 (NACK
   = offline); on a streak do 9 SCL pulses + STOP on GPIO 21/22, `Wire.end()/begin()`,
   `setClock`, `setTimeOut(25)`, re-init LCD. Each slave: `Wire.end(); Wire.begin(addr);` +
   re-register handlers after 15 s without master traffic (`lastI2CActivity` set in both ISRs).
   Consider `Wire.setClock(50000)` on master and room 3 for long cables.
5. `Arduino_Laser`: fixed `char[48]` line buffer; accept only digit/comma lines with exactly
   5 commas (telemetry) or <= 3 chars (status); `LOG:` and `OK` handled explicitly.
6. `Arduino_Audio_3`: add `Log(const __FlashStringHelper*)` using `strncpy_P`, switch calls to `F()`.
7. LCD: compose both 16-char lines in buffers, write a line only when it changed, periodic full
   refresh, re-init after bus recovery.
8. Telemetry `"sys"` object: uptime s, `esp_reset_reason()` as text, boots since power-on
   (`RTC_NOINIT_ATTR` counter + magic), free/min heap, i2c error + recovery counters, eth present.
   Dashboard: system card + log line when uptime drops (master restarted).
9. `ESP_WLED_svetla` and `ESP_Mistnost3`: same task-WDT setup, print reset reason at boot,
   `Serial2.setTimeout(50)` on WLED, `Wire.setTimeOut(25)` on room 3.
10. Optional AVR hardware watchdog behind `#define POUZIT_WATCHDOG 0` (`wdt_enable(WDTO_2S)`,
    `wdt_reset()` in loop). Only for Optiboot boards (UNO, Nano "new bootloader"); a Nano with
    the old bootloader reset-loops after the first WDT reset, so default off and document it.
11. Hardware checklist for the room: separate 3.3 V supply or a proper 5 V rail for the master
    board, measure I2C bus with a scope if cables exceed a few metres, check "Brownout detector
    was triggered" in the master's USB log.

## Working style the user expects

- After a review, make the design calls, implement the whole list, document decisions in code
  comments; do not ask per item.
- Say how long builds take and post progress lines during multi-minute ESP32 compiles.
