# Optimization roadmap

This document lists planned optimization directions for BitNoDelay. The current library is intentionally universal Arduino code. Future versions can add optional global optimizations and controller-specific branches while keeping the same public API:

```cpp
noDelay(ID, value_ms, phase_ms, deadline_ms) {
  task();
}
```

## Current implementation

Current BitNoDelay uses only portable Arduino/C++ features:

```text
Arduino.h
millis()
uint32_t / uint8_t
templates
macros
```

This makes it usable on AVR, STM32, ESP32, RP2040, SAMD, Teensy, and most Arduino-compatible cores.

## Optimization rule

The public API should stay stable. Optimizations should be hidden behind compile-time branching.

Preferred structure:

```text
src/BitNoDelay.h                    universal public API
src/BitNoDelayConfig.h              feature switches
src/backends/BitNoDelayGeneric.h    portable fallback
src/backends/BitNoDelayAVR.h        AVR-specific timing/backend helpers
src/backends/BitNoDelaySTM32.h      STM32-specific timing/backend helpers
src/backends/BitNoDelayESP32.h      ESP32-specific timing/backend helpers
src/backends/BitNoDelayRP2040.h     RP2040-specific timing/backend helpers
```

Selection should use existing Arduino core macros where possible:

```cpp
#if defined(ARDUINO_ARCH_AVR)
  // AVR path
#elif defined(ARDUINO_ARCH_STM32)
  // STM32 path
#elif defined(ARDUINO_ARCH_ESP32)
  // ESP32 path
#elif defined(ARDUINO_ARCH_RP2040)
  // RP2040 path
#else
  // portable fallback
#endif
```

## Planned global optimizations

These optimizations are useful on all controllers.

### 1. Compile-time mode pruning

Already partly implemented.

Goal: keep all path decisions compile-time where parameters are constants:

```text
power-of-two value + deadline 0     -> bucket missable path only
power-of-two value + deadline > 0   -> bucket catch-up path only
arbitrary value + deadline 0        -> arbitrary missable path only
arbitrary value + deadline > 0      -> arbitrary catch-up path only
```

No runtime mode switch should be needed for normal use.

### 2. Optional 16-bit state mode

For small boards, add a mode that stores tick/bucket state in `uint16_t` when periods and expected uptime allow it.

Possible config:

```cpp
#define BITNODELAY_STATE_16BIT 1
```

Tradeoff: less RAM, shorter wrap range.

### 3. Packed state mode

Current state is simple and readable:

```text
uint32_t state
uint8_t armed
```

Possible packed mode:

```text
high bit = armed flag
low 31 bits = bucket/deadline state
```

Benefit: one `uint32_t` per timer ID.

Cost: slightly trickier code and shorter usable range.

### 4. Compile-time phase mode

Current `phase_ms` may be runtime variable. That is flexible, but runtime phase conversion is still needed in arbitrary mode.

Add optional macro for compile-time phase:

```cpp
noDelayConstPhase(ID, value_ms, phase_ms, deadline_ms) {
  task();
}
```

Benefit: lets the compiler fold phase math completely.

### 5. User-selectable time source

Allow replacing `millis()` with a custom faster or higher-resolution clock.

Possible config:

```cpp
#define BITNODELAY_NOW_MS() millis()
```

User could override:

```cpp
#define BITNODELAY_NOW_MS() fastMillis()
```

or for microsecond timing:

```cpp
#define BITNODELAY_NOW_US() micros()
```

A separate microsecond variant should be explicit to avoid confusing ms/us behavior.

### 6. Optional profiling hooks

Add hooks that can count firings, skips, and catch-ups without changing the API.

Possible config:

```cpp
#define BITNODELAY_ENABLE_STATS 1
```

Stats should be disabled by default so normal builds pay no cost.

## Controller-specific branches

These are possible future backends. BitNoDelay itself does not yet need direct GPIO or timer registers, but a larger timing/control library could share the same style.

### AVR: Uno, Nano, Mega

Possible optimizations:

```text
- use direct timer0 tick reads if faster than millis() on target core
- optional 8-bit/16-bit state modes for RAM saving
- PROGMEM documentation/examples for tiny devices
- direct PORTx helpers for companion FastGPIO module
- hardware timer compare helpers for companion FastPWM/Tone module
```

Likely macros:

```cpp
#if defined(ARDUINO_ARCH_AVR)
```

Best use case: tiny RAM boards where packed/16-bit state matters.

### STM32 Arduino core

Possible optimizations:

```text
- use HAL_GetTick() or core millisecond tick directly where available
- optional DWT cycle counter backend for high-resolution variants on Cortex-M3/M4/M7
- LL/HAL timer helpers for companion FastPWM/Tone module
- GPIOx->BSRR helpers for companion FastGPIO/LCD modules
```

Likely macros:

```cpp
#if defined(ARDUINO_ARCH_STM32)
```

Best use case: precise timers, fast GPIO, PWM, ADC, and robust LCD backends.

### ESP32

Possible optimizations:

```text
- use esp_timer_get_time() for microsecond variant
- use FreeRTOS tick only where appropriate
- LEDC integration for companion FastPWM/Tone module
- hardware timer backend for scheduled callbacks if a non-cooperative mode is added
```

Likely macros:

```cpp
#if defined(ARDUINO_ARCH_ESP32)
```

Best use case: high-resolution timing, PWM/tone, and task-aware timing.

### RP2040

Possible optimizations:

```text
- use time_us_32() for microsecond variant
- use hardware alarms for optional scheduled callback backend
- use PWM slices for companion FastPWM/Tone module
- use PIO for precise waveform/output modules
```

Likely macros:

```cpp
#if defined(ARDUINO_ARCH_RP2040)
```

Best use case: precise waveforms, PWM, PIO-timed outputs.

### SAMD / ARM Cortex-M0+

Possible optimizations:

```text
- direct SysTick read helpers where useful
- TC/TCC timer support for companion PWM/tone modules
- compact RAM mode for smaller boards
```

Likely macros:

```cpp
#if defined(ARDUINO_ARCH_SAMD)
```

## Companion modules that could share the same style

BitNoDelay can remain a timing core while optional sister modules handle hardware-specific speed.

Possible future modules:

```text
FastPin       direct GPIO writes with fallback to digitalWrite
FastPWM       known frequency/resolution PWM setup
FastADC       configured ADC read path
FastTone      timer/PWM tone generation
RobustLCD     non-blocking HD44780 with parallel/I2C backends
```

Each should follow the same rule:

```text
normal Arduino-style API outside
controller-specific optimized backend inside
portable fallback always available
```

## Priority order

Recommended development order:

1. Add `BitNoDelayConfig.h`.
2. Add packed-state optional mode.
3. Add compile-time phase macro.
4. Add custom time-source macro.
5. Add AVR compact mode.
6. Add STM32 high-resolution/time-source backend.
7. Add RP2040/ESP32 microsecond variants.
8. Add companion FastGPIO/FastPWM modules if needed.

## Non-goals

BitNoDelay should not become a heavy scheduler.

Avoid:

```text
- heap allocation
- dynamic timer registration
- large global timer tables
- virtual classes
- RTOS dependency
- hidden interrupts by default
```

The library should stay small, deterministic, and friendly to normal Arduino sketches.
