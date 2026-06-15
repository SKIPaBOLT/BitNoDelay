#include <BitNoDelay.h>

// Shows deadline catch-up behavior.

enum Timers : uint32_t {
  t_skip = 1,
  t_catchup = 2,
  t_report = 3,
};

uint32_t skip_count = 0;
uint32_t catchup_count = 0;

void setup() {
  Serial.begin(115200);
}

void loop() {
  noDelay(t_skip, 20, 0, 0) {
    skip_count++;
  }

  noDelay(t_catchup, 20, 0, 100) {
    catchup_count++;
  }

  noDelay(t_report, 1024, 0, 0) {
    Serial.print("skip="); Serial.print(skip_count);
    Serial.print(" catchup="); Serial.println(catchup_count);
  }
}
