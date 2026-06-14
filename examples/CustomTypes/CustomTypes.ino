/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * CustomTypes Example Overview:
 * - Quant<T, Scale>: stores a physical value as a scaled integer, so a float travels as 1/2/4 bytes
 *   instead of 4. Shows resolution/range introspection, fits() clipping detection and saturation
 *   behavior.
 * - Bytes<LenT, MaxLen>: variable-length byte payload with a length prefix. Shows set(), in-place
 *   fill via getBuffer()/setLength(), and clear().
 * - Serializes a message combining both custom types.
 */

#include <Arduino.h>

#include "BytePack.h"
using namespace BytePack;

struct SensorReading {
  // 0.01 V resolution, range -327.68 to 327.67 V, travels as 2 bytes
  Quant<int16_t, 100> voltage;

  // Up to 16 payload bytes + 1 length-prefix byte
  Bytes<uint8_t, 16> note;

  template <typename Archive>
  constexpr void io(Archive& ar) {
    ar(voltage, note);
  }
};

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("------------------------------");
  Serial.println("BytePack - CustomTypes Example");
  Serial.println("------------------------------");

  // --- Quant introspection (all constexpr, usable in static_assert) ---
  using Voltage = Quant<int16_t, 100>;

  Serial.println("Quant type:");
  Serial.print("- Quant resolution: ");
  Serial.println(Voltage::getResolution(), 2);
  Serial.print("- Quant min: ");
  Serial.println(Voltage::getMinValue(), 2);
  Serial.print("- Quant max: ");
  Serial.println(Voltage::getMaxValue(), 2);

  // fits() tells whether a value survives quantization without clipping
  Serial.print("- fits(12.34): ");
  Serial.println(Voltage::fits(12.34f) ? "yes" : "no");
  Serial.print("- fits(400.0): ");
  Serial.println(Voltage::fits(400.0f) ? "yes" : "no");

  // Assignment quantizes (and saturates) immediately, not at serialization
  Voltage v;
  v = 12.34f;
  Serial.print("- 12.34 stored as raw ");
  Serial.print(v.getRaw());
  Serial.print(" -> reads back as ");
  Serial.println(v.getFloat(), 2);

  v = 400.0f; // out of range: saturates to max instead of overflowing
  Serial.print("- 400.0 saturates to ");
  Serial.println(v.getFloat(), 2);
  Serial.println();

  // --- Bytes usage ---
  SensorReading reading;
  reading.voltage = 3.31f;

  Serial.println("Bytes type:");
  Serial.print("- Capacity: ");
  Serial.print(reading.note.getCapacity());
  Serial.print(" bytes, current length: ");
  Serial.println(reading.note.getLength());

  // Option A: copy an existing buffer (validated, fails if it does not fit)
  const uint8_t raw[3] = {0xDE, 0xAD, 0xBE};
  if (reading.note.set(raw, sizeof(raw))) {
    Serial.print("- note set with ");
    Serial.print(reading.note.getLength());
    Serial.println(" bytes");
  } else {
    Serial.println("- note.set() failed: payload larger than capacity");
  }

  // Option B: fill in place, then commit the length (setLength validates)
  const int n = snprintf(reinterpret_cast<char*>(reading.note.getBuffer()),
    reading.note.getCapacity(),
    "v=%d",
    int(reading.voltage.getRaw()));
  if (n > 0 && reading.note.setLength(size_t(n))) {
    Serial.print("- note filled in place with ");
    Serial.print(reading.note.getLength());
    Serial.println(" bytes");
  } else {
    Serial.println("- note.setLength() failed: payload larger than capacity");
  }

  Serial.println();

  // --- Serialize the combined message ---
  // Worst case: 2 (quant) + 1 (length prefix) + 16 (note capacity) = 19 bytes
  uint8_t buffer[getMaxPackedSize<SensorReading>()] = {};
  const size_t written                              = serialize(reading, buffer, sizeof(buffer));

  Serial.println("Serialized SensorReading:");

  // Only the used part of note travels: 2 + 1 + getLength() bytes
  Serial.print("- Worst-case size: ");
  Serial.print(sizeof(buffer));
  Serial.print(" bytes, actually written: ");
  Serial.println(written);

  // clear() drops the payload (length 0), e.g. to reuse the message
  reading.note.clear();
  Serial.print("- After clear(), note length: ");
  Serial.println(reading.note.getLength());
}

void loop() {}
