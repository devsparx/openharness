# HANDOFF — Harness dial setup on ai-rtx (2026-11-11)

Status document for an agent picking this up cold. Read this fully before touching
anything. Everything below was learned the hard way; the traps section is the important part.

## What this is

Matthew's desk machine (`ai-rtx`, Ubuntu 26.04, AMD 7950XX + RTX 5070 12GB) runs the
**Harness** agent stack (https://github.com/autonomous-ai/openharness) with two physical
"dial" devices:

1. **Waveshare ESP32-S3-Touch-AMOLED-1.75** (466×466 AMOLED, CST9217 touch, AXP2101 PMIC,
   dual mics + ES7210 ADC, ES8311 DAC + NS4150B amp, battery) — the retail board, the daily
   driver. Currently flashed with a **custom firmware build reporting version 9.9.9**.
2. **LilyGo T-RGB 2.1"** (480×480 ST7701S RGB TFT, CST820 touch, no audio, no PMIC) — a
   third-party port done here, works as a spare. Flashed with a custom build also at 9.9.9.

## Verified working (evidence-backed)

- Waveshare dial: display, touch, USB cable session to the daemon, agent cards, question
  delivery, brightness sync, OTA self-update (the daemon pushed official 0.1.0 over USB
  successfully on Nov 11), **voice input** (16 kHz clips reach the daemon's cloud STT and
  transcribe — the mic fix below is log-proven in `~/.harness/cli/data/harness.log`,
  `voice 6.0s (188 KB) → stt` followed by `msg recv`).
- T-RGB port: boots, renders the habitat face centered on the 480 glass, touch works
  (custom CST820 driver), cable protocol works. TFT text is coarse — that's the panel, not
  a bug. Do not chase it.

## Verified facts & resolutions (disproving prior misconceptions)

1. **Dial Pairing Myth (DISPROVED)**:
   - Prior assumption claimed the dial needs "Approve a device" with spoken 8-digit codes.
   - **Verified**: "Approve a device" in Desktop/CLI (`harness login --code`) is exclusively for IoT lamps (`lamp_login.dart`) and headless remote CLI machines. The USB dial is a companion peripheral over USB-CDC ACM; it connects directly to `harnessd-devices`, requires NO code pairing, and auto-identifies via `hello.hw`.

2. **Dial Settings & Mute Control (RESOLVED)**:
   - Prior assumption claimed "the app has no dial settings UI" and unmute path was unknown.
   - **Verified**: The Desktop UI contains device settings in `desktop/lib/devices/device_settings.dart:174` under `Swarm ▸ Manage ▸ Devices`, communicating via the daemon cable protocol (`settings.set { muted: bool, brightness: int, pet: str }`).

3. **MCLK Hardware Routing & Dynamic Board Abstraction (IMPLEMENTED & VERIFIED)**:
   - Prior code had a hardcoded `BSP_I2S_MCLK 42` hack in `pins_dial.h` that broke the autonomous custom PCB.
   - **Verified**: Hardware schematics (`SCH_SCH_Harness_1.75.pdf`) and BOM confirm Board B (`cst816s`) routes MCLK to GPIO 16, whereas retail Waveshare dial (`cst9217+axp2101`) routes MCLK to GPIO 42.
   - **Implemented**: Added `mclk_gpio` to `board_t` in `board.h`, populated dynamically from I2C bus probing in `board_table.c` (42 for Waveshare, 16 for autonomous PCB, -1 for TRGB). Referenced as `board()->mclk_gpio` in `audio_capture.c`.
   - Verified 100% of firmware unit tests pass (`make device-test`). Clean image built and flashed to `/dev/ttyACM0`.

4. **Terminal Question Safety Injection**:
   - Spoken answers showing `[inject] not typed · engine=claude · question_open` is an intentional safety guard in `cli/src/lib/sessionInput.ts:675` and `cli/src/core/input.ts:207`. When a CLI question prompt is active on an agent's terminal, untargeted voice input is held so accidental keystrokes don't trigger unintended option selections. Spoken answers must target the active question token.

## Exact build & flash commands

Firmware tree: `/home/matthew/Developer/openharness/devices/harness-device/firmware`
(ESP-IDF at `~/esp/esp-idf-5.5.4`, activate with `. ~/esp/esp-idf-5.5.4/export.sh`).

**Waveshare dial build (the mic-fixed custom image):**
```bash
idf.py -B build-dial-mic -DPROJECT_VER=9.9.9 build
```
**T-RGB build:**
```bash
idf.py -B build-trgb -DTRGB_BOARD=1 \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.defaults.trgb" build
```
**Flash either board** (daemon holds the serial port — stop it first; port may be
ttyACM0 *or* ttyACM1 after a re-enumeration, detect with `ls /dev/ttyACM*`):
```bash
harness stop
~/.harness/flasher/esptool-v5.3.1-linux-amd64/esptool-linux-amd64/esptool \
  --chip esp32s3 -p /dev/ttyACM0 -b 460800 --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m \
  0x0 build-dial-mic/bootloader/bootloader.bin \
  0x8000 build-dial-mic/partition_table/partition-table.bin \
  0xf000 build-dial-mic/ota_data_initial.bin \
  0x20000 build-dial-mic/interns_commander.bin
harness start
```

## Changes made to the firmware tree (all uncommitted)

- `main/audio_capture.c` — **the mic fix**: `pins_dial.h`'s `BSP_I2S_MCLK` changed 16→42
  (the autonomous dial PCB uses 16; the retail Waveshare routes MCLK from GPIO42 per its
  hardware reference). Without it the ES7210 never clocks → full-length digital silence
  that STT reads as "Didn't catch that". NOTE: this breaks the autonomous original board
  if it ever shows up — make it per-board if both matter.
- `main/board/pins_trgb.h`, `xl9555.c/h`, `touch_cst820.c/h`, `board_table.c`, `board.c`,
  `board_pins.h`, `ui/habitat/panel_trgb.c/h`, `display_habitat.c`, `touch_habitat.c`,
  `app_main.c`, `main/CMakeLists.txt`, root `CMakeLists.txt`, `sdkconfig.defaults.trgb` —
  the T-RGB port (build-time `-DTRGB_BOARD=1`; gates audio off, adds ST7701S RGB bring-up
  over the XL9555 expander, CST820/FT3267 runtime touch detect).
- Root `CMakeLists.txt` sets `PROJECT_VER 9.9.9` for TRGB builds. The dial build passes
  `-DPROJECT_VER=9.9.9` on the command line.

## Traps (each one cost real time)

- **The version guard is load-bearing.** Anything below the published version gets
  auto-pushed official firmware over USB (seen happening). Custom builds must report
  higher (9.9.9). Re-flashing `harness flash` (published 0.1.0) writes the app at 0x20000
  while its own partition table expects factory at 0x200000 → unbootable. Recovery:
  flash a repo build by hand (commands above), then let the daemon OTA-push 0.1.0 cleanly.
- **`BOARD_TRGB` is a CMake `-D` compile flag, NOT Kconfig.** `sdkconfig.defaults`
  entries for unknown Kconfig symbols are warned-and-dropped, which silently produced a
  dial image once. Don't reintroduce a Kconfig path.
- **Ubuntu 26.04 AppArmor blocks the AppImage runtime's FUSE mount** (userns restriction;
  the runtime re-execs from a memfd so path-based AppArmor profiles can't attach). The
  desktop app runs extracted: `~/Applications/harness-1.4.8-extracted/AppRun` via
  `~/Applications/harness-run.sh` (pinned to 1.4.8; `.desktop` entry points there).
- **The desktop black-window saga was VRAM exhaustion**, not Flutter/GPU config: the GPU
  hosts the desktop (7680×2160), ML services, and LLMs on 12 GB. `sky-indextts.service`
  pins ~6.4 GB. Kernel sign: `nvidia-drm: Failed to allocate NVKMS memory for GEM object`
  → black surfaces; recovers when pressure eases. Full notes:
  `~/Developer/voice-trainer/GPU-SERVICES.md`.
- **Serial port ownership**: `harnessd-devices` holds `/dev/ttyACM*`. `harness stop`
  before esptool/serial capture. The battery-powered board drops USB when it sleeps → the
  port number can change; always re-detect. `esptool --before usb-reset` reboots into
  *download mode* and needs a follow-up `--after hard_reset` to run the app.
- **Observability**: `~/.harness/cli/data/harness.log` logs the useful *inbound* dial
  events (voice clips, STT results, scroll/drag, agent pushes). `harness start
  --device-dump=<absolute path>` logs only *outbound* frames and does NOT expand `~`
  (a stray `/home/matthew/~` directory exists because of this — safe to delete). The
  daemon's collected "non-frame text" boot log truncates before the audio init lines.
- **Question flow on the dial** (from `ui/habitat/ui_habitat.c`, verified): question card →
  tap ✓ (`[next]`) → choices show ONE option at a time → **drag vertically ~40px** to flip
  options ("drag for choices") → tap `[select]` → `[review]` → confirm. Agents answerable
  by voice too (spoken answers get injected as `not typed`). Swipe L/R moves between cards
  on the ring; vertical swipe on a face is a no-op by spec.
- **Agents on the dial**: only daemon-engine-owned sessions (`harness new …`) reliably
  appear; hand-rolled tmux CLIs got listed as "available" but pushed to the dial as 0 of N.
- `run.sh` in voice-trainer mentions a second GPU (3080 Ti) — stale, single-GPU machine.

## System state right now

- Daemon: running (version auto-updates; was v0.6.7-dev.nogit). Machine `ai-rtx`
  · `93C2·E190·4A21·EECB`. Account also has `MacBook-Air.local` (linked but not
  remote-password-linked).
- Waveshare dial attached on USB (check `ls /dev/ttyACM*`), running the 9.9.9 mic-fixed
  image, `hw: cst9217+axp2101`.
- Desktop app 1.4.8 running via the wrapper (see `~/Applications/harness-run.sh`).
- Stray files from this session: `/home/matthew/~/` (mistaken dump path), `/tmp/dial-boot*.txt`,
  `/tmp/trgb_*`, `~/harness-appimage.profile` (already installed to /etc/apparmor.d —
  harmless either way).

## Suggested first moves for the next agent

1. Read `~/.harness/cli/data/harness.log` tail — it's the ground truth of what the dial
   does.
2. Pairing: search `cli.js` for how `Approve a device` is wired and whether a
   cable-connected dial can be approved without the 8-digit code; if it needs firmware,
   the hook belongs in `cable_client.c`/`config_store.c` (the `pair` namespace already
   exists and the daemon trusts the cable channel).
3. Speaker: with pairing done (or bypassed), find the mute toggle surface; then trigger a
   turn-end beep with a trivial `harness new claude --new <name> -- 'Reply exactly: ok.
   Then stop.'` and listen. If no beep, capture the boot log (stop daemon, read serial at
   115200, `esptool --before usb-reset --after hard_reset chip-id`, catch the app boot)
   and check for `speaker (ES8311) ready` vs an init failure.
