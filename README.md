# BitNoDelay

**Arduino non-blocking delay replacement, millis timer helper, periodic task scheduler, and compile-time optimized timing macro.**

Keywords: `Arduino non-blocking delay`, `millis timer`, `delay replacement`, `non-blocking timer`, `Arduino scheduler`, `periodic task`, `embedded timing`, `AVR`, `STM32`, `ESP32`, `RP2040`, `SAMD`, `Teensy`, `bucket timer`, `deadline timer`, `phase offset timer`.

BitNoDelay is a small Arduino IDE timing library for writing non-blocking code without declaring timer objects everywhere. It is meant for sketches that would normally use `delay()`, hand-written `millis()` checks, or repeated timer variables.

It gives you one macro:

```cpp
noDelay(ID, value_ms, phase_ms, deadline_ms) {
  task();
}
```

## Current status

The current library is universal Arduino code. It uses only `Arduino.h`, `millis()`, fixed-width integer types, templates, and macros.

Planned global optimizations and controller-specific branches are documented here:

- [Optimization roadmap](docs/optimization-roadmap.md)
- [Timing behavior](docs/timing-behavior.md)
- [Design notes](docs/design-notes.md)
- [Search keywords](docs/search-keywords.md)

The roadmap covers possible AVR, STM32, ESP32, RP2040, SAMD, packed-state, custom time-source, and future FastGPIO/FastPWM/FastADC directions.

The library is optimized around compile-time path selection:

| Input | Internal path |
|---|---|
| `value_ms` is power-of-two, for example `8`, `64`, `512` | bucket timer, shift/XOR style logic |
| `value_ms` is arbitrary, for example `20`, `1000`, `333` | exact-value timer using the largest exact power-of-two tick quantum |
| `phase_ms = 0` | no offset |
| `phase_ms > 0` | shifts the timer slot to reduce task overlap |
| `deadline_ms = 0` | missable; old missed periods are skipped |
| `deadline_ms > 0` | catch-up allowed one event per loop call if lateness is within the deadline window |

## Why

Arduino `delay()` blocks the whole sketch. `millis()` state machines are better, but repeated hand-written timing code gets noisy.

BitNoDelay keeps the sketch readable while pushing the timer state and bit-oriented path selection behind the curtain.

Useful search terms this project targets:

```text
Arduino delay replacement
Arduino non-blocking delay
Arduino millis timer
Arduino non-blocking timer
Arduino periodic task scheduler
Arduino cooperative scheduler
embedded C++ timer helper
STM32 Arduino timer
ESP32 Arduino timer
RP2040 Arduino timer
AVR Arduino timer
```

## Install

### Arduino IDE ZIP install

1. Download the repository as a ZIP.
2. Arduino IDE: **Sketch -> Include Library -> Add .ZIP Library...**
3. Select the ZIP.
4. Use:

```cpp
#include <BitNoDelay.h>
```

### Manual install

Copy the folder into:

- Windows: `Documents/Arduino/libraries/BitNoDelay`
- Linux: `~/Arduino/libraries/BitNoDelay`
- macOS: `~/Documents/Arduino/libraries/BitNoDelay`

Restart Arduino IDE.

## Basic example

```cpp
#include <BitNoDelay.h>

enum Timers : uint32_t {
  t_blink = 1,
  t_lcd = 2,
  t_sensor = 3,
};

void loop() {
  noDelay(t_blink, 512, 0, 0) {
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  }

  noDelay(t_lcd, 64, 16, 0) {
    updateDisplay();
  }

  noDelay(t_sensor, 1000, 80, 0) {
    readSensor();
  }
}
```

## Parameters

### `ID`

Unique compile-time timer ID.

Every independent timer needs its own ID:

```cpp
enum Timers : uint32_t {
  t_buttons = 1,
  t_lcd = 2,
  t_sensor = 3,
};
```

Do not reuse the same ID for separate timers unless you intentionally want them to share state.

### `value_ms`

The timer period in milliseconds. Must be a compile-time constant.

Power-of-two values use the bucket path:

```cpp
noDelay(1, 64, 0, 0) {
  // one bucket tick every 64 ms
}
```

Arbitrary values use the largest exact power-of-two tick step:

```text
1000 = 125 * 8    -> 8 ms internal tick
20   = 5 * 4      -> 4 ms internal tick
333  = 333 * 1    -> 1 ms internal tick
```

No runtime division or modulo is used for these conversions.

### `phase_ms`

Manual timing offset used to spread tasks apart.

```cpp
noDelay(1, 64, 0, 0)  { taskA(); } // roughly 0, 64, 128...
noDelay(2, 64, 16, 0) { taskB(); } // roughly 16, 80, 144...
noDelay(3, 64, 32, 0) { taskC(); } // roughly 32, 96, 160...
```

For bucket timers, phase shifts the bucket edge. For arbitrary timers, phase offsets the first scheduled firing after the timer is armed.

### `deadline_ms`

`deadline_ms = 0` means missable / skip pile-up:

```cpp
noDelay(t_sensor, 1000, 0, 0) {
  readSensor();
}
```

If the loop is blocked for 5 seconds, it fires once and resumes from now. It does not run five times.

`deadline_ms > 0` means catch up only inside the deadline window:

```cpp
noDelay(t_control, 20, 0, 40) {
  controlLoop();
}
```

If the event is 10 ms late, it runs. If it is 500 ms late, old backlog is skipped.

## Memory use

Each used timer ID creates:

```text
uint32_t state
uint8_t armed flag
```

No heap, no dynamic allocation, no global timer array.

## Compile-time routing

These are selected by the compiler:

```text
power-of-two value + deadline 0     -> bucket missable path
power-of-two value + deadline > 0   -> bucket catch-up path
arbitrary value + deadline 0        -> arbitrary missable path
arbitrary value + deadline > 0      -> arbitrary catch-up path
```

Unused paths do not execute at runtime.

## Resetting a timer

```cpp
noDelayReset(t_sensor);
```

Use this after mode changes, entering menus, waking from sleep, or intentionally re-aligning a timer.

## GitHub topics to add

For better discovery, add these repository topics in GitHub's **About** panel:

```text
arduino
arduino-library
non-blocking
delay-replacement
millis
timer
scheduler
embedded
avr
stm32
esp32
rp2040
teensy
```

## Notes

- `ID`, `value_ms`, and `deadline_ms` must be compile-time constants.
- `phase_ms` may be a variable.
- This is cooperative scheduling; the main loop must keep running.
- The macro expands to a normal C++ `if` so braces work naturally.
- Internal conversions avoid runtime division/modulo.
