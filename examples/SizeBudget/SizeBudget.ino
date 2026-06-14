/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SizeBudget Example Overview:
 * - Computes message sizes at compile time with getMaxPackedSize() and getMaxPackedSizeWithHeader()
 *   (io() must be constexpr, as in these structs).
 * - Enforces a transport budget with static_assert: if a message outgrows the link MTU (e.g. a LoRa
 *   payload), the firmware stops compiling instead of failing in the field.
 * - Shows worst-case vs actually-written size: Bytes<> fields are counted at full capacity for the
 *   budget, but only the used part travels.
 */

#include <Arduino.h>

#include "BytePack.h"
using namespace BytePack;

// Example transport limit: LoRa payload at the slowest data rate
constexpr size_t LORA_MAX_PAYLOAD = 51;

struct StatusReport {
  static constexpr uint8_t ID      = 0x40;
  static constexpr uint8_t VERSION = 1;

  uint32_t uptime_s = 0;
  Quant<int16_t, 100> battery_v;
  Quant<int8_t, 1> rssi_dbm;
  Bytes<uint8_t, 24> log_tail; // variable-length: counted at full capacity

  template <typename Archive>
  constexpr void io(Archive& ar) {
    ar(uptime_s, battery_v, rssi_dbm, log_tail);
  }
};

// The budget is checked at compile time: growing StatusReport beyond the MTU
// becomes a build error, not a runtime surprise
static_assert(getMaxPackedSizeWithHeader<StatusReport>() <= LORA_MAX_PAYLOAD,
  "StatusReport does not fit in a LoRa payload");

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("-----------------------------");
  Serial.println("BytePack - SizeBudget Example");
  Serial.println("-----------------------------");

  Serial.print("Fields only (worst case): ");
  Serial.print(getMaxPackedSize<StatusReport>());
  Serial.println(" bytes");

  Serial.print("With [ID][VERSION] header: ");
  Serial.print(getMaxPackedSizeWithHeader<StatusReport>());
  Serial.println(" bytes");

  Serial.print("Transport budget: ");
  Serial.print(LORA_MAX_PAYLOAD);
  Serial.println(" bytes (checked by static_assert)");

  // Worst case vs reality: only the used part of log_tail travels
  StatusReport report;
  report.uptime_s  = 3600;
  report.battery_v = 3.87f;
  report.rssi_dbm  = -92.0f;

  const char* log_msg = "boot ok";
  report.log_tail.set(log_msg, strlen(log_msg));

  uint8_t buffer[getMaxPackedSizeWithHeader<StatusReport>()] = {};
  const size_t written = serializeWithHeader(report, buffer, sizeof(buffer));

  Serial.print("Actually written: ");
  Serial.print(written);
  Serial.print(" of ");
  Serial.print(sizeof(buffer));
  Serial.println(" bytes");
}

void loop() {}
