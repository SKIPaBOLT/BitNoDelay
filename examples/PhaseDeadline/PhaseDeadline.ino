#include <BitNoDelay.h>

// Demonstrates phase offsets and deadline-limited catch-up.
// Open Serial Monitor at 115200 baud.

enum Timers : uint32_t {
  t_task_a = 1,
  t_task_b = 2,
  t_sensor = 3,
  t_control = 4,
  t_report = 99,
};

volatile uint32_t a_count = 0;
volatile uint32_t b_count = 0;
volatile uint32_t sensor_count = 0;
volatile uint32_t control_count = 0;

void setup() {
  Serial.begin(115200);
}

void loop() {
  // Same 64 ms bucket period, different phase slots.
  // This reduces overlap/stutter if both tasks do real work.
  noDelay(t_task_a, 64, 0, 0) {
    a_count++;
  }

  noDelay(t_task_b, 64, 16, 0) {
    b_count++;
  }

  // 1000 = 125 * 8, so arbitrary path uses 8 ms internal ticks.
  // deadline 0 means missed old periods are skipped.
  noDelay(t_sensor, 1000, 80, 0) {
    sensor_count++;
  }

  // 20 = 5 * 4, so arbitrary path uses 4 ms internal ticks.
  // deadline 40 lets missed control ticks catch up one per loop call
  // if the oldest missed event is not more than about 40 ms late.
  noDelay(t_control, 20, 0, 40) {
    control_count++;
  }

  noDelay(t_report, 1024, 0, 0) {
    Serial.print("A="); Serial.print(a_count);
    Serial.print(" B="); Serial.print(b_count);
    Serial.print(" sensor="); Serial.print(sensor_count);
    Serial.print(" control="); Serial.println(control_count);
  }
}
