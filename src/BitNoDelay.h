#pragma once

#include <Arduino.h>

/*
  BitNoDelay
  ==========

  A small Arduino timing helper for sketches that need non-blocking timers with
  predictable state, low RAM use, and fast paths for power-of-two intervals.

  Public syntax:

      noDelay(ID, value_ms, phase_ms, deadline_ms) {
        task();
      }

  Parameter meanings:

    ID
      Compile-time unique timer identifier. Each ID owns exactly one hidden
      timer state. Reusing the same ID for different timers intentionally shares
      that state and will produce bad timing.

    value_ms
      Timer period in milliseconds. Must be a compile-time constant.

      If value_ms is a power of two, BitNoDelay selects the bucket path:
        value_ms = 64 -> one bucket tick every 64 ms.
        The fast path uses shift/XOR/OR style logic, no divide/modulo.

      If value_ms is not a power of two, BitNoDelay selects the arbitrary path:
        value_ms = 1000 -> 1000 = 125 * 8, so the internal tick is 8 ms.
        value_ms = 20   -> 20   = 5 * 4,   so the internal tick is 4 ms.
        value_ms = 333  -> odd value, so the internal tick is 1 ms.
      The divisor is found at compile time from the number of trailing zero bits.
      Runtime code still avoids division and modulo.

    phase_ms
      Runtime phase offset in milliseconds. It is only used to shift timer
      placement so many tasks do not fire in the same loop pass. It does not
      change the period and it does not change deadline behavior.

      For bucket timers, a positive phase delays the bucket edge:
        noDelay(idA, 64, 0, 0)  -> approximately 0, 64, 128, ...
        noDelay(idB, 64, 16, 0) -> approximately 16, 80, 144, ...

      For arbitrary timers, phase offsets the first scheduled firing after the
      timer is armed. This is normally what you want when all timers are first
      armed from setup()/loop() at startup.

    deadline_ms
      Compile-time catch-up window in milliseconds.

      deadline_ms == 0:
        Missable timer. If the loop is late, old missed periods are skipped.
        This prevents backlog bursts and stutter.

      deadline_ms > 0:
        Catch-up timer. If a period was missed but is not older than the
        deadline window, BitNoDelay returns true once and advances one period.
        If it is too late, the old event is skipped and timing resumes from now.
        Catch-up is intentionally one event per loop call.

  Memory model:

    One uint32_t plus one uint8_t per used ID.
    No heap allocation.
    No timer array.
    Unused IDs instantiate no storage.

  Important limitations:

    - ID, value_ms, and deadline_ms must be compile-time constants.
    - phase_ms may be a runtime variable.
    - This is cooperative timing. Your loop must keep running.
    - Like millis(), it has millisecond granularity.
*/

namespace nd {

// Pick between two uint32_t values using a mask instead of a user-visible branch.
// cond may be any non-zero value. Non-zero picks new_value; zero keeps old_value.
static inline uint32_t nd_pick_u32(uint32_t old_value, uint32_t new_value, uint32_t cond) {
  const uint32_t mask = 0U - (uint32_t)(cond != 0U);
  return (old_value & ~mask) | (new_value & mask);
}

// Compile-time power-of-two test.
template<uint32_t X>
struct IsPow2 {
  enum : uint8_t { value = (X != 0U) && ((X & (X - 1U)) == 0U) };
};

// Compile-time count-trailing-zero helper for uint32_t constants.
// Used to select the largest exact power-of-two tick step.
template<uint32_t X>
struct Ctz32 {
  enum : uint8_t { value = (X & 1U) ? 0U : (uint8_t)(1U + Ctz32<(X >> 1U)>::value) };
};

template<>
struct Ctz32<0U> {
  enum : uint8_t { value = 0U };
};

// Runtime ceil(x / 2^SHIFT), where SHIFT is compile-time.
// For SHIFT=0 this compiles to just x.
template<uint8_t SHIFT>
struct ShiftMath {
  static inline uint32_t ceil(uint32_t x) {
    return (x + ((1UL << SHIFT) - 1UL)) >> SHIFT;
  }
};

template<>
struct ShiftMath<0U> {
  static inline uint32_t ceil(uint32_t x) {
    return x;
  }
};

// Hidden state for a timer ID.
// v stores either the last bucket or next deadline depending on selected path.
// armed prevents first-call firing and makes first call prime the timer.
template<uint32_t ID>
struct State {
  static uint32_t v;
  static uint8_t armed;

  static inline void reset() {
    v = 0U;
    armed = 0U;
  }
};

template<uint32_t ID>
uint32_t State<ID>::v = 0U;

template<uint32_t ID>
uint8_t State<ID>::armed = 0U;

// -----------------------------------------------------------------------------
// Bucket path: value_ms is power-of-two.
// -----------------------------------------------------------------------------

template<uint32_t ID, uint32_t VALUE_MS, uint32_t DEADLINE_MS, bool CATCH>
struct BucketTimer;

// Bucket path, missable: fastest path.
template<uint32_t ID, uint32_t VALUE_MS, uint32_t DEADLINE_MS>
struct BucketTimer<ID, VALUE_MS, DEADLINE_MS, false> {
  static_assert(VALUE_MS > 0U, "value_ms must be > 0");
  static_assert(IsPow2<VALUE_MS>::value, "bucket path requires power-of-two value_ms");

  enum : uint8_t { SHIFT = Ctz32<VALUE_MS>::value };
  enum : uint32_t { MASK = VALUE_MS - 1U };

  static inline bool ready(uint32_t phase_ms) {
    const uint32_t now_ms = millis();

    // Positive phase delays bucket firing.
    // For VALUE_MS=64, phase=16:
    //   adjusted = millis() + 48, so bucket edge appears at 16, 80, 144...
    // phase is reduced to bucket range because adding one full period is the
    // same timing slot again.
    const uint32_t phase_mod = phase_ms & MASK;
    const uint32_t phase_advance = (0U - phase_mod) & MASK;
    const uint32_t adjusted = now_ms + phase_advance;
    const uint32_t bucket = adjusted >> SHIFT;

    const uint32_t old_bucket = State<ID>::v;
    const uint32_t was_armed = State<ID>::armed;
    const uint32_t changed = (bucket ^ old_bucket) != 0U;
    const uint32_t fire = was_armed & changed;

    // Missable mode jumps directly to current bucket. If loop was late, old
    // bucket changes are intentionally forgotten.
    State<ID>::v = bucket;
    State<ID>::armed = 1U;

    return fire != 0U;
  }
};

// Bucket path, deadline catch-up.
template<uint32_t ID, uint32_t VALUE_MS, uint32_t DEADLINE_MS>
struct BucketTimer<ID, VALUE_MS, DEADLINE_MS, true> {
  static_assert(VALUE_MS > 0U, "value_ms must be > 0");
  static_assert(DEADLINE_MS > 0U, "deadline catch path requires deadline_ms > 0");
  static_assert(IsPow2<VALUE_MS>::value, "bucket path requires power-of-two value_ms");

  enum : uint8_t { SHIFT = Ctz32<VALUE_MS>::value };
  enum : uint32_t { MASK = VALUE_MS - 1U };
  enum : uint32_t { MAX_CATCH_DELTA = (DEADLINE_MS >> SHIFT) + 1U };

  static inline bool ready(uint32_t phase_ms) {
    const uint32_t now_ms = millis();

    const uint32_t phase_mod = phase_ms & MASK;
    const uint32_t phase_advance = (0U - phase_mod) & MASK;
    const uint32_t adjusted = now_ms + phase_advance;

    const uint32_t bucket = adjusted >> SHIFT;
    const uint32_t low_ms = adjusted & MASK;

    const uint32_t old_bucket = State<ID>::v;
    const uint32_t was_armed = State<ID>::armed;
    const uint32_t delta = bucket - old_bucket;
    const uint32_t due = was_armed & (delta != 0U);

    // Approximate age of the oldest queued bucket event in real milliseconds.
    // For a pure bucket timer, this is still shift/OR, no multiply/divide.
    const uint32_t plausible = due & (delta <= MAX_CATCH_DELTA);
    const uint32_t late_ms = ((delta - 1U) << SHIFT) | low_ms;
    const uint32_t in_deadline = plausible & (late_ms <= DEADLINE_MS);
    const uint32_t too_late = due & (in_deadline ^ 1U);

    uint32_t next_bucket = old_bucket;
    next_bucket = nd_pick_u32(next_bucket, old_bucket + 1U, in_deadline); // consume one queued tick
    next_bucket = nd_pick_u32(next_bucket, bucket, too_late);             // skip old pile-up
    next_bucket = nd_pick_u32(next_bucket, bucket, was_armed ^ 1U);       // first call primes

    State<ID>::v = next_bucket;
    State<ID>::armed = 1U;

    return in_deadline != 0U;
  }
};

// -----------------------------------------------------------------------------
// Arbitrary path: value_ms is not power-of-two.
// The period is represented in the largest exact power-of-two tick quantum.
// -----------------------------------------------------------------------------

template<uint32_t ID, uint32_t VALUE_MS, uint32_t DEADLINE_MS, bool CATCH>
struct ArbitraryTimer;

// Arbitrary path, missable.
template<uint32_t ID, uint32_t VALUE_MS, uint32_t DEADLINE_MS>
struct ArbitraryTimer<ID, VALUE_MS, DEADLINE_MS, false> {
  static_assert(VALUE_MS > 0U, "value_ms must be > 0");

  enum : uint8_t { SHIFT = Ctz32<VALUE_MS>::value };
  enum : uint32_t { PERIOD_TICKS = VALUE_MS >> SHIFT };

  static inline bool ready(uint32_t phase_ms) {
    const uint32_t now_tick = millis() >> SHIFT;
    const uint32_t old_due = State<ID>::v;
    const uint32_t was_armed = State<ID>::armed;

    // Phase is only a first-firing offset here. After that, the timer period is
    // stable in PERIOD_TICKS. phase_ms may be a runtime variable.
    const uint32_t phase_ticks = ShiftMath<SHIFT>::ceil(phase_ms);
    const uint32_t first_due = now_tick + phase_ticks + PERIOD_TICKS;

    const uint32_t due = was_armed & ((int32_t)(now_tick - old_due) >= 0);
    const uint32_t next_skip = now_tick + PERIOD_TICKS;

    uint32_t next_due = old_due;
    next_due = nd_pick_u32(next_due, next_skip, due);              // skip backlog, fire once
    next_due = nd_pick_u32(next_due, first_due, was_armed ^ 1U);   // first call primes

    State<ID>::v = next_due;
    State<ID>::armed = 1U;

    return due != 0U;
  }
};

// Arbitrary path, deadline catch-up.
template<uint32_t ID, uint32_t VALUE_MS, uint32_t DEADLINE_MS>
struct ArbitraryTimer<ID, VALUE_MS, DEADLINE_MS, true> {
  static_assert(VALUE_MS > 0U, "value_ms must be > 0");
  static_assert(DEADLINE_MS > 0U, "deadline catch path requires deadline_ms > 0");

  enum : uint8_t { SHIFT = Ctz32<VALUE_MS>::value };
  enum : uint32_t { PERIOD_TICKS = VALUE_MS >> SHIFT };
  enum : uint32_t { DEADLINE_TICKS = (DEADLINE_MS + ((1UL << SHIFT) - 1UL)) >> SHIFT };

  static inline bool ready(uint32_t phase_ms) {
    const uint32_t now_tick = millis() >> SHIFT;
    const uint32_t old_due = State<ID>::v;
    const uint32_t was_armed = State<ID>::armed;

    const uint32_t phase_ticks = ShiftMath<SHIFT>::ceil(phase_ms);
    const uint32_t first_due = now_tick + phase_ticks + PERIOD_TICKS;

    const uint32_t late_ticks = now_tick - old_due;
    const uint32_t due = was_armed & ((int32_t)late_ticks >= 0);
    const uint32_t in_deadline = due & (late_ticks <= DEADLINE_TICKS);
    const uint32_t too_late = due & (in_deadline ^ 1U);

    const uint32_t next_catch = old_due + PERIOD_TICKS; // consume one queued tick
    const uint32_t next_skip = now_tick + PERIOD_TICKS; // skip old pile-up

    uint32_t next_due = old_due;
    next_due = nd_pick_u32(next_due, next_catch, in_deadline);
    next_due = nd_pick_u32(next_due, next_skip, too_late);
    next_due = nd_pick_u32(next_due, first_due, was_armed ^ 1U);

    State<ID>::v = next_due;
    State<ID>::armed = 1U;

    return in_deadline != 0U;
  }
};

// -----------------------------------------------------------------------------
// Compile-time router.
// -----------------------------------------------------------------------------

template<uint32_t ID, uint32_t VALUE_MS, uint32_t DEADLINE_MS, bool BUCKET>
struct Router;

template<uint32_t ID, uint32_t VALUE_MS, uint32_t DEADLINE_MS>
struct Router<ID, VALUE_MS, DEADLINE_MS, true> {
  static inline bool ready(uint32_t phase_ms) {
    return BucketTimer<ID, VALUE_MS, DEADLINE_MS, (DEADLINE_MS != 0U)>::ready(phase_ms);
  }
};

template<uint32_t ID, uint32_t VALUE_MS, uint32_t DEADLINE_MS>
struct Router<ID, VALUE_MS, DEADLINE_MS, false> {
  static inline bool ready(uint32_t phase_ms) {
    return ArbitraryTimer<ID, VALUE_MS, DEADLINE_MS, (DEADLINE_MS != 0U)>::ready(phase_ms);
  }
};

template<uint32_t ID, uint32_t VALUE_MS, uint32_t DEADLINE_MS>
struct NoDelay {
  static_assert(VALUE_MS > 0U, "value_ms must be > 0");

  static inline bool ready(uint32_t phase_ms = 0U) {
    return Router<ID, VALUE_MS, DEADLINE_MS, IsPow2<VALUE_MS>::value>::ready(phase_ms);
  }

  static inline void reset() {
    State<ID>::reset();
  }
};

} // namespace nd

// Main user-facing macro.
// It deliberately expands to an if statement so normal Arduino syntax works:
//   noDelay(1, 1000, 0, 0) { task(); }
// The path selection itself happens at compile time inside nd::NoDelay.
#define noDelay(ID, VALUE_MS, PHASE_MS, DEADLINE_MS) \
  if (::nd::NoDelay<ID, VALUE_MS, DEADLINE_MS>::ready((uint32_t)(PHASE_MS)))

// Reset one timer ID. Useful after changing application state, entering a menu,
// waking from sleep, or intentionally re-aligning phased timers.
#define noDelayReset(ID) \
  (::nd::State<ID>::reset())
