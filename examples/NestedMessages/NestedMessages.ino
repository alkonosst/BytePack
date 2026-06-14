/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * NestedMessages Example Overview:
 * - Composes messages from reusable sub-structs: any struct with io() can be a field of another
 *   message, including fixed C arrays of structs.
 * - Nested structs are serialized inline (fields only, no header), even if they declare ID/VERSION
 *   themselves. The header exists only at the outermost level, written by the WithHeader helpers.
 * - Consequence for versioning: if a shared sub-struct changes its layout, bump the VERSION of
 *   every top-level message that embeds it.
 */

#include <Arduino.h>

#include "BytePack.h"
using namespace BytePack;

// Reusable sub-struct: no header members needed, it always travels inline
struct Position {
  Quant<int16_t, 100> x; // meters, 1 cm resolution
  Quant<int16_t, 100> y;

  template <typename Archive>
  constexpr void io(Archive& ar) {
    ar(x, y);
  }
};

struct Waypoint {
  Position position;
  uint16_t speed = 0; // mm/s

  template <typename Archive>
  constexpr void io(Archive& ar) {
    ar(position, speed);
  }
};

// Top-level message: owns the header and embeds an array of sub-structs
struct Route {
  static constexpr uint8_t ID      = 0x30;
  static constexpr uint8_t VERSION = 1; // covers Route AND its nested structs

  uint8_t count = 0;
  Waypoint waypoints[3];

  template <typename Archive>
  constexpr void io(Archive& ar) {
    ar(count, waypoints);
  }
};

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("---------------------------------");
  Serial.println("BytePack - NestedMessages Example");
  Serial.println("---------------------------------");

  // Size: 2 (header) + 1 (count) + 3 * (2 + 2 (position) + 2 (speed)) = 21 bytes
  uint8_t buffer[getMaxPackedSizeWithHeader<Route>()] = {};
  Serial.print("Frame size: ");
  Serial.println(sizeof(buffer));

  Route tx;
  tx.count                   = 2;
  tx.waypoints[0].position.x = 1.25f;
  tx.waypoints[0].position.y = -3.5f;
  tx.waypoints[0].speed      = 500;
  tx.waypoints[1].position.x = 7.75f;
  tx.waypoints[1].position.y = 0.1f;
  tx.waypoints[1].speed      = 350;

  const size_t written = serializeWithHeader(tx, buffer, sizeof(buffer));
  if (written == 0) {
    Serial.println("Serialization failed");
    return;
  }

  Route rx;
  if (!deserializeWithHeader(rx, buffer, written)) {
    Serial.println("Deserialization failed");
    return;
  }

  Serial.print("Waypoints: ");
  Serial.println(rx.count);
  for (uint8_t i = 0; i < rx.count; i++) {
    const Waypoint& wp = rx.waypoints[i];
    Serial.print("  [");
    Serial.print(i);
    Serial.print("] x=");
    Serial.print(wp.position.x.getFloat(), 2);
    Serial.print(" y=");
    Serial.print(wp.position.y.getFloat(), 2);
    Serial.print(" speed=");
    Serial.println(wp.speed);
  }
}

void loop() {}
