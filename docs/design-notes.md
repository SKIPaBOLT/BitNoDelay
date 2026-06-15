# Design notes

BitNoDelay is deliberately split into small compile-time paths.

## Goals

- Keep sketch code readable.
- Avoid `delay()`.
- Avoid user-declared timer objects for simple cases.
- Avoid heap allocation.
- Use bit-oriented bucket logic when the period allows it.
- Keep arbitrary exact periods usable without runtime division or modulo.
- Allow phase offsets to reduce task overlap.
- Provide both missable and deadline-limited catch-up behavior.

## Compile-time router

The public macro calls:

```cpp
nd::NoDelay<ID, VALUE_MS, DEADLINE_MS>::ready(phase_ms)
```

`VALUE_MS` and `DEADLINE_MS` are template arguments, so the compiler can remove unused paths.

Routing table:

```text
VALUE_MS power-of-two + deadline 0   -> BucketTimer missable
VALUE_MS power-of-two + deadline >0  -> BucketTimer catch-up
VALUE_MS arbitrary + deadline 0      -> ArbitraryTimer missable
VALUE_MS arbitrary + deadline >0     -> ArbitraryTimer catch-up
```

## Why bucket mode exists

For values like `8`, `64`, `512`, checking a timer can be done by observing when a shifted time bucket changes.

```text
64 ms bucket -> millis() >> 6
```

This avoids the normal deadline comparison style and gives deterministic, very low-overhead behavior.

## Why arbitrary mode still exists

Exact values such as `1000` are not bucket values, but they can still be represented efficiently:

```text
1000 = 125 * 8
```

So arbitrary mode uses `millis() >> 3` and counts 125 ticks. The factorization is compile-time only.

## Why state is one ID

The state is indexed by template ID:

```cpp
nd::State<ID>
```

This means every unique ID creates one hidden static state. There is no global array and no lookup.

The downside is simple: do not reuse one ID for unrelated timers.

## Why the macro expands to `if`

The public API wants this normal Arduino syntax:

```cpp
noDelay(1, 1000, 0, 0) {
  task();
}
```

That requires the macro to expand to an `if`. The internal path selection and most state changes are still compile-time routed and lightweight.

## Practical tuning

Use bucket values for background jobs where exact timing is not important:

```cpp
noDelay(t_lcd, 64, 16, 0) { updateLcd(); }
```

Use arbitrary values when exact human-readable timing matters:

```cpp
noDelay(t_sensor, 1000, 80, 0) { readSensor(); }
```

Use deadlines for tasks where small catch-up is useful:

```cpp
noDelay(t_control, 20, 0, 40) { controlLoop(); }
```
