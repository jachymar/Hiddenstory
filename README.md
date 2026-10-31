# Hidden Story – escape room controller

Firmware for the *Hidden Story* escape room: one ESP32 **master** with the game logic, an ESP32
**WLED gateway**, an ESP32 **bridge for room 3** and eight **Arduino (ATmega328P) puzzle modules**.
Code comments and identifiers are in Czech; this file is the English map of the system.

## Topology

```
 Operator PC ── Ethernet (192.168.0.100, dashboard + REST API) ──┐      ┌── USB / Web Serial ── Operator PC
                                                                 │      │
                                                            ┌────┴──────┴────┐
                                                            │   ESP_Master   │ LCD 16x2 (0x27), mode button GPIO14
                                                            └─┬─────┬──────┬─┘
                      I2C (master) ───────────────────────────┘     │      └── ESP-NOW (ch 1) ──► ESP_Mistnost3 (room 3)
   11 Arduino_Tlacitka    12 Arduino_Kola    14 Arduino_Svetla      │                               I2C: 20 Arduino_Tukani
   13 Arduino_Laser ══ SoftwareSerial 9600 ══► Arduino_Lebka        │                                    21 Arduino_Audio_3
   16 Arduino_Audio_1,2                                             │
                                                                    └── Serial2 115200, text lines ──► ESP_WLED_svetla
                                                                                                        WiFi "Indy-Wifi" ──► WLED strips
                                                                                                        192.168.1.2 / .3 / .4 (UDP 21324)
```

Lebka is **not** on the I2C bus. The master addresses it as "15" but every command is forwarded
by the Laser Arduino over a UART bridge, and the Laser caches Lebka's status/telemetry/log for the master.

## Modules

| Folder | Board | Address | Role |
|---|---|---|---|
| `ESP_Master` | ESP32 | – | Game logic, I2C master, HTTP dashboard + API, USB telemetry, LCD, ESP-NOW to room 3 |
| `ESP_WLED_svetla` | ESP32 | – | Turns Serial2 text commands into WLED UDP JSON, pings the three strips |
| `ESP_Mistnost3` | ESP32 | – | Room 3 bridge between ESP-NOW and its local I2C bus |
| `Arduino_Tlacitka` | Uno/Nano | I2C 11 | Five-button code lock (code 1-4-2-3), servo lock, 30 s inactivity timer |
| `Arduino_Kola` | Uno/Nano | I2C 12 | Three analog "wheel" sensors, servo lock |
| `Arduino_Laser` | Uno/Nano | I2C 13 | Laser barrier with LDR, alarm with cooldown, UART bridge to Lebka |
| `Arduino_Svetla` | Uno/Nano | I2C 14 | Magnet-triggered LED cascade, software-PWM servo that dumps the ball |
| `Arduino_Lebka` | Uno/Nano | via Laser | Three crystal sensors, skull box (electromagnet + servo) |
| `Arduino_Audio_1,2` | Uno + VS1053 shield | I2C 16 | Music for rooms 1-2 (`0001`/`0002`/`0003.mp3`), door contact on D3 |
| `Arduino_Tukani` | Uno/Nano | I2C 20 (room 3) | Piezo tap-rhythm lock |
| `Arduino_Audio_3` | Uno + VS1053 shield | I2C 21 (room 3) | Room 3 sounds and hints |

## Game modes

| Mode | Meaning | How it is entered |
|---|---|---|
| 0 | Game | Default, mode button, web/USB `0`, automatic return from alarm |
| 1 | Laser alarm | Automatically when the Laser reports status 1; ends ~17 s later |
| 3 | Maintenance | Mode button, web/USB `3`. Resets puzzles, lights to service colour, laser always on. If game mode ran for ≥ 10 minutes, automatically opens all boxes (Lebka, Tlačítka, Kola, Tukani) |

Mode 2 ("finished") is accepted from the API but nothing sets it today.

### Game flow in short

1. Door contact on the Audio board opens → master enables the laser (`N`) after 3 s; closed door disables it (`F`).
2. Beam broken for ≥ 80 ms → Laser status 1 → master goes to mode 1: WLED strips go dark, audio ignores `1`.
   After the Laser's 17 s alert window the master returns to mode 0 and resets Světla and Tlačítka.
3. Ball on the Světla magnet → LED cascade (Světla status 1). Master forwards `S1` to WLED and `M` to Tlačítka,
   which starts the 30 s inactivity countdown.
4. Correct code on Tlačítka → status 5 → master sends `O` to Světla (cascade stops, no restarts until maintenance).
   Three wrong codes or 30 s of inactivity → status 4 → master sends `E` to Světla (servo dumps the ball).
5. Three crystals in Lebka → status 2 → master plays the victory track (`K`) and tells WLED `K2`.
   **Lebka opens its own box** after its 7 s countdown; the master does not send `A` automatically.
6. Kola opens its own servo when all three sensors match for 1.5 s. Room 3: Tukani rhythm → Audio 3 beeps/hints.

## Protocols

### I2C (master → Arduino)

Single-byte commands. A 1-byte read returns the module status. Writing `0x99` and reading back
returns the module's packed diag struct, `0x98` returns its 30-char last log line.

| Module | Commands | Status byte |
|---|---|---|
| all | `0` game mode / reset, `3` maintenance | |
| Tlačítka 11 | `B` open lock, `M` start 30 s inactivity countdown, `R` reset | 0 idle, 4 error (held 1 s), 5 opened (held 1 s) |
| Kola 12 | `C` open | 3 for 1 s when activated |
| Laser 13 | `N` enable, `F` disable; `A` `S` `D` `L` are bridged to Lebka | 0 ok, 1 alarm |
| Světla 14 | `E` dump ball, `O` box opened (stop + lock out), `R` reset | 1 while the cascade runs |
| Lebka (via 13) | `A` open box, `S` status, `D` telemetry CSV, `L` log | 2 = all crystals present |
| Audio 1/2 16 | `0` main loop, `1` ignored, `3` stop, `K` victory | 1 door open, 0 door closed |
| Tukani 20 | `O` force open | 1 beep request, 2 solved |
| Audio 3 21 | `0`/`3` mode, `A` alarm / play rhythm, `E` beep, `D` solved | |

### Master → WLED gateway (Serial2, one line per command)

`M<mode>`, `C<colorButton>`, `K<crystals>`, `S<cascadeRunning>`, `X<devMode>`.
The gateway answers `WLED:a,b,c` (1 = strip reachable) after each ping round (~10 s) and on every change.

### Master ↔ room 3 (ESP-NOW, WiFi channel 1)

Master → M3: `{target_i2c, command, sys_mode, sys_color, sys_crystals}`. `U` is a state update only,
`R` requests diagnostics, any other command is written to `target_i2c`. M3 → master: status/log struct every 500 ms.

### HTTP API (port 80) and USB

| Endpoint | Description |
|---|---|
| `GET /` | Dashboard (served from `ESP_Master/dashboard_html.h`) |
| `GET /api/state` | JSON: `mode`, per-module telemetry, `online{eth,laser,tlacitka,lebka,kola,svetla,audio,lcd,wled1-3,m3,m3_tukani,m3_audio3}`, `diag_text`, `diag_log` |
| `POST /api/command` | Body is the command: `0`…`3` mode, `A` Lebka, `B` Tlačítka, `C` Kola, `E` Tukani lock, `F` dump the ball, `R` play M3 rhythm, `X1`/`X0` dev mode, `D<n>` diag module 1–6, `I` I2C scan |

USB (115200 baud) accepts the same commands plus `GET_STATE`. The master prints `TELEMETRY:{json}`
on every change and at least every 2 s; the dashboard's "Připojit USB" button uses this via Web Serial.

## Building and uploading

Everything builds with the `arduino-cli` bundled in Arduino IDE 2 (see `.vscode/tasks.json` for the path).
Cores: `arduino:avr` 1.8.8 and `esp32:esp32` 3.3.x. Libraries: EthernetENC, LiquidCrystal I2C,
Adafruit VS1053, SD, Servo.

```bash
arduino-cli compile --fqbn arduino:avr:uno Arduino_Laser
```

```bash
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=huge_app ESP_Master
```

### Dashboard

The dashboard source is `Web_app/index.html`. The master serves a copy embedded in
`ESP_Master/dashboard_html.h`, which is **generated** – never edit it by hand. After changing
`index.html` run the generator (also available as the VS Code task *Dashboard: Generate ESP header*):

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File Web_app/build_dashboard.ps1
```

The generator writes UTF-8 without BOM, which is what the page declares and the ESP sends.
