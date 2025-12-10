# Arduino Battleship

Simple Battleship-style game for Arduino using an LED matrix (MAX7219) and buttons.

- **Description:** Two-player, turn-based game. Place ships, take turns firing; LEDs show hits and misses.
- **Hardware:** `Arduino Uno` (or compatible), 8x8 LED matrix with `MAX7219` (uses `LedControl`), buttons for input, optional I2C LCD (`LiquidCrystal_I2C`).
- **Key files:** `arduino-battleship.ino`, `diagram.json`, `sketch.yaml`.
- **Wiring:** See `diagram.json` for wiring/layout; match pins in `arduino-battleship.ino` to your wiring.

## Build & Upload

Use the Arduino IDE or `arduino-cli`:

```bash
arduino-cli compile --fqbn arduino:avr:uno .
arduino-cli upload -p /dev/ttyACM0 --fqbn arduino:avr:uno .
```

## Usage

1. Wire the LED matrix and buttons per `diagram.json`.
2. Upload the sketch to the board.
3. Follow on-device prompts: place ships, press button to fire, view hits/misses on the LED matrix.
