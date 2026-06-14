/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * WriterReader Example Overview:
 * - Uses the Writer and Reader archives directly, without defining a message struct. Useful for
 *   ad-hoc protocols or when the field list is not fixed at compile time.
 * - Shows the sticky error model: once a write/read fails, the archive stays in error until reset,
 *   and nothing partial is committed.
 * - Shows position tracking: getSize() on the Writer, getConsumedSize() and getRemainingSize() on
 *   the Reader.
 */

#include <Arduino.h>

#include "BytePack.h"
using namespace BytePack;

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("-------------------------------");
  Serial.println("BytePack - WriterReader Example");
  Serial.println("-------------------------------");

  uint8_t buffer[16] = {};

  // --- Writing fields directly ---
  Serial.println("Writer:");
  Writer w(buffer, sizeof(buffer));

  const uint8_t command = 0x01;
  const uint16_t param  = 512;
  const float gain      = 1.5f;
  w(command, param, gain); // variadic: writes fields in order

  if (!w.isOk()) {
    Serial.println("- Write failed: buffer too small");
    return;
  }
  Serial.print("- Wrote ");
  Serial.print(w.getSize());
  Serial.print(" of ");
  Serial.print(w.getCapacity());
  Serial.println(" bytes");

  // --- Overflow: error is sticky and nothing partial is written ---
  uint8_t small[3] = {};
  Writer w2(small, sizeof(small));

  const double too_big = 3.141592653589793; // needs 8 bytes, only 3 available
  w2(too_big);
  Serial.print("- Overflow detected: ");
  Serial.println(w2.isOk() ? "no" : "yes");
  Serial.print("- Bytes written after overflow: ");
  Serial.println(w2.getSize()); // 0: failed fields are never half-written

  w2.reset(); // rewinds position and clears the error (buffer content untouched)
  Serial.print("- After reset, isOk: ");
  Serial.println(w2.isOk() ? "yes" : "no");

  // --- Reading fields back ---
  Serial.println("\nReader:");
  Reader r(buffer, w.getSize());

  uint8_t command_in = 0;
  uint16_t param_in  = 0;
  float gain_in      = 0.0f;
  r(command_in, param_in, gain_in);

  if (!r.isOk()) {
    Serial.println("- Read failed: truncated input");
    return;
  }
  Serial.print("- command: 0x");
  Serial.println(command_in, HEX);
  Serial.print("- param: ");
  Serial.println(param_in);
  Serial.print("- gain: ");
  Serial.println(gain_in, 2);

  Serial.print("- Consumed: ");
  Serial.print(r.getConsumedSize());
  Serial.print(" bytes, remaining: ");
  Serial.println(r.getRemainingSize());

  // --- Truncated read: the failing field is not consumed ---
  Reader r2(buffer, 2); // only 2 bytes available
  uint32_t big_field = 0;
  r2(big_field); // needs 4 bytes -> error
  Serial.print("- Truncated read detected: ");
  Serial.println(r2.isOk() ? "no" : "yes");
  Serial.print("- Consumed after failure: ");
  Serial.println(r2.getConsumedSize()); // 0
}

void loop() {}
