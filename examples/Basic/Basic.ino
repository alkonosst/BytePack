/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * Basic Example Overview:
 * - Defines a message as a plain struct with an io() member function. The same io() drives
 *   serialization, deserialization and size counting.
 * - Sizes the buffer at compile time with getMaxPackedSize().
 * - Serializes the message with serialize() (little-endian wire format) and prints the bytes.
 * - Deserializes the bytes back into a fresh struct with deserialize(), checking for errors.
 */

#include <Arduino.h>

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

void printHex(const uint8_t* data, const size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (data[i] < 0x10) Serial.print('0');
    Serial.print(data[i], HEX);
    Serial.print(' ');
  }
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("------------------------");
  Serial.println("BytePack - Basic Example");
  Serial.println("------------------------");

  // Buffers sized at compile time: 4 (uptime) + 2 (temperature) + 1 (relay) = 7 bytes
  uint8_t buffer[getMaxPackedSize<Telemetry>()] = {};

  // Fill and serialize (e.g. on the transmitting device)
  Telemetry tx;
  tx.uptime_ms   = millis();
  tx.temperature = 2350; // 23.50 C
  tx.relay_on    = true;

  const size_t written = serialize(tx, buffer, sizeof(buffer));
  if (written == 0) {
    Serial.println("Serialization failed: buffer too small");
    return;
  }

  Serial.print("Serialized ");
  Serial.print(written);
  Serial.println(" bytes (little-endian):");
  printHex(buffer, written);
  Serial.println();

  Serial.println("Tx:");
  Serial.print("- uptime_ms: ");
  Serial.println(tx.uptime_ms);
  Serial.print("- temperature: ");
  Serial.println(tx.temperature);
  Serial.print("- relay_on: ");
  Serial.println(tx.relay_on ? "true" : "false");
  Serial.println();

  // Deserialize into a fresh struct (e.g. on the receiving device)
  Telemetry rx;
  if (!deserialize(rx, buffer, written)) {
    Serial.println("Deserialization failed: truncated or invalid input");
    return;
  }

  Serial.println("Rx:");
  Serial.print("- uptime_ms: ");
  Serial.println(rx.uptime_ms);
  Serial.print("- temperature: ");
  Serial.println(rx.temperature);
  Serial.print("- relay_on: ");
  Serial.println(rx.relay_on ? "true" : "false");
}

void loop() {}
