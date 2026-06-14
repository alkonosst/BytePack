/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * MessageHeader Example Overview:
 * - Adds the wire-header convention to a message: `static constexpr uint8_t ID` and `static
 *   constexpr uint8_t VERSION`. Both are enforced at compile time by the WithHeader helpers.
 * - serializeWithHeader() produces [ID][VERSION][fields]; deserializeWithHeader() rejects any frame
 *   whose ID or VERSION does not match (strict equality).
 * - peekId()/peekVersion() inspect the header without consuming, e.g. to log unknown frames.
 */

#include <Arduino.h>

#include "BytePack.h"
using namespace BytePack;

struct SetPoint {
  static constexpr uint8_t ID      = 0x21;
  static constexpr uint8_t VERSION = 2; // bump this whenever the field layout changes

  uint16_t target_rpm = 0;
  bool enabled        = false;

  template <typename Archive>
  constexpr void io(Archive& ar) {
    ar(target_rpm, enabled);
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

  Serial.println("--------------------------------");
  Serial.println("BytePack - MessageHeader Example");
  Serial.println("--------------------------------");

  // Buffer sized for header + fields: 2 + (2 + 1) = 5 bytes
  uint8_t buffer[getMaxPackedSizeWithHeader<SetPoint>()] = {};

  SetPoint tx;
  tx.target_rpm = 1500;
  tx.enabled    = true;

  const size_t written = serializeWithHeader(tx, buffer, sizeof(buffer));
  Serial.print("Frame (");
  Serial.print(written);
  Serial.print(" bytes, [ID][VERSION][fields]): ");
  printHex(buffer, written);

  // --- Inspect the header without consuming (useful for logging/routing) ---
  uint8_t id      = 0;
  uint8_t version = 0;
  if (peekId(buffer, written, id) && peekVersion(buffer, written, version)) {
    Serial.print("Peeked ID: 0x");
    Serial.print(id, HEX);
    Serial.print(", VERSION: ");
    Serial.println(version);
  }

  // --- Matching header: deserializes ---
  SetPoint rx;
  if (deserializeWithHeader(rx, buffer, written)) {
    Serial.print("Accepted: target_rpm=");
    Serial.print(rx.target_rpm);
    Serial.print(", enabled=");
    Serial.println(rx.enabled ? "true" : "false");
  }

  // --- Wrong VERSION: rejected (e.g. a device running older firmware) ---
  buffer[1] = SetPoint::VERSION + 1;
  if (!deserializeWithHeader(rx, buffer, written)) {
    Serial.println("Rejected: VERSION mismatch (older/newer sender)");
  }
  buffer[1] = SetPoint::VERSION;

  // --- Wrong ID: rejected ---
  buffer[0] = 0xFF;
  if (!deserializeWithHeader(rx, buffer, written)) {
    Serial.println("Rejected: ID mismatch (frame belongs to another message)");
  }
}

void loop() {}
