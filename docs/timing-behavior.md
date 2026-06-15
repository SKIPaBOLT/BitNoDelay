# Timing behavior

## `noDelay(ID, value_ms, phase_ms, deadline_ms)`

BitNoDelay has one user-facing macro and four internally selected paths.

```cpp
noDelay(ID, value_ms, phase_ms, deadline_ms) {
  task();
}
```

## Bucket values

If `value_ms` is a power of two, BitNoDelay treats it as one bucket.

```text
value_ms = 64
bucket = adjusted_millis >> 6
```

So `64` does not become eight 8 ms ticks. It becomes one 64 ms bucket tick.

This is the fastest path.

## Arbitrary values

If `value_ms` is not a power of two, BitNoDelay uses the largest exact power-of-two tick quantum.

```text
1000 = 125 * 8 -> internal tick = 8 ms
20   = 5 * 4   -> internal tick = 4 ms
333  = 333 * 1 -> internal tick = 1 ms
```

The number of trailing zero bits in `value_ms` decides the internal shift. This is compile-time selected.

## Phase

Phase is used to spread tasks apart.

For bucket timers:

```cpp
noDelay(1, 64, 0, 0)  { taskA(); }
noDelay(2, 64, 16, 0) { taskB(); }
noDelay(3, 64, 32, 0) { taskC(); }
```

The tasks are placed in different slots of the same 64 ms bucket period.

For arbitrary timers, phase offsets the first scheduled firing after the timer is armed.

## Deadline 0: missable

Missable timers skip old events.

If a 20 ms timer is not checked for 500 ms, it fires once and resumes from now.

Use this for displays, sensors, UI refresh, logs, and tasks where old missed work is useless.

## Deadline > 0: catch-up window

Deadline timers catch up one event per loop call, but only while the oldest missed event is still inside the deadline window.

Example:

```cpp
noDelay(t_control, 20, 0, 40) {
  controlLoop();
}
```

If the timer is late by 12 ms, it runs. If it is late by 400 ms, the old event is skipped.

Use this for control loops or counters where small lateness matters but large stale backlogs should not explode into a burst.

## State and overflow

The library uses unsigned arithmetic around `millis()` wrap. As with most Arduino timing code, periods and deadlines should remain far below the half-range of `uint32_t` time.
