# Batcal

Battery calibration and charge-threshold gliding utility for Omarchy (ThinkPad `charge_behaviour` & `charge_control_*_threshold`).

## Build & Install

```bash
./bin/build
./bin/test
./bin/install
```

## CLI / SSH Usage

```bash
batcal --status
batcal --status --json
batcal --threshold 75 80
batcal --abort
```

## Keyboard Shortcuts

| Key | Action |
|-----|--------|
| `Space` | Start / Abort calibration |
| `T` | Open battery gliding (charge thresholds) dialog |
| `?` | Toggle keyboard shortcuts overlay |
| `Q` | Quit (prompts if calibration is active) |
