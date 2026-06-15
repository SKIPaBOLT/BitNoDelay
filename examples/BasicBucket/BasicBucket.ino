#include <BitNoDelay.h>

// Basic power-of-two bucket timers.
// 512 and 8 are powers of two, so BitNoDelay selects the bucket path.

enum Timers : uint32_t {
  t_blink = 1,
  t_fast_housekeeping = 2,
};

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
  // Runs about every 512 ms.
  // deadline 0 means missed periods are skipped instead of piling up.
  noDelay(t_blink, 512, 0, 0) {
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  }

  // Runs about every 8 ms.
  // Useful for lightweight background jobs such as button scans.
  noDelay(t_fast_housekeeping, 8, 0, 0) {
    // scanButtons();
  }
}
