/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * Basic Native Example Overview:
 * - Mirrors the Basic Arduino example, swapping Serial for printf. Useful for native builds.
 * - Build and run locally:
 *   PowerShell: $env:EXAMPLE="examples/BasicNative"; pio run -e native-example -t exec
 *   bash/WSL  : export EXAMPLE="examples/BasicNative"; pio run -e native-example -t exec
 */

#include <cstdio>

#include "BytePack.h"
using namespace BytePack;

// A message is any struct that lists its fields in io()
struct Telemetry {
  uint32_t uptime_ms  = 0;
  int16_t temperature = 0; // centi-degrees Celsius
  bool relay_on       = false;

  template <typename Archive>
  constexpr void io(Archive& ar) {
    ar(uptime_ms, temperature, relay_on);
  }
};

static void printHex(const uint8_t* data, const size_t len) {
  for (size_t i = 0; i < len; ++i)
    printf("%02X ", data[i]);
  printf("\n");
}

int main() {
  printf("-------------------------------\n");
  printf("BytePack - Basic Native Example\n");
  printf("-------------------------------\n");

  // Buffer sized at compile time: 4 (uptime) + 2 (temperature) + 1 (relay) = 7 bytes
  uint8_t buffer[getMaxPackedSize<Telemetry>()] = {};

  // Fill and serialize (e.g. on the transmitting device)
  Telemetry tx;
  tx.uptime_ms   = 123456;
  tx.temperature = 2350; // 23.50 C
  tx.relay_on    = true;

  const size_t written = serialize(tx, buffer, sizeof(buffer));
  if (written == 0) {
    printf("Serialization failed: buffer too small\n");
    return 1;
  }

  printf("Serialized %zu bytes (little-endian):\n", written);
  printHex(buffer, written);
  printf("\n");

  printf("Tx:\n");
  printf("- uptime_ms: %u\n", tx.uptime_ms);
  printf("- temperature: %d\n", tx.temperature);
  printf("- relay_on: %s\n", tx.relay_on ? "true" : "false");
  printf("\n");

  // Deserialize into a fresh struct (e.g. on the receiving device)
  Telemetry rx;
  if (!deserialize(rx, buffer, written)) {
    printf("Deserialization failed: truncated or invalid input\n");
    return 1;
  }

  printf("Rx:\n");
  printf("- uptime_ms: %u\n", rx.uptime_ms);
  printf("- temperature: %d\n", rx.temperature);
  printf("- relay_on: %s\n", rx.relay_on ? "true" : "false");

  return 0;
}
