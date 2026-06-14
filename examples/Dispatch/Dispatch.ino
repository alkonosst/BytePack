/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * Dispatch Example Overview:
 * - Routes incoming frames to per-message handlers with dispatch<Msgs...>(): it reads the
 *   [ID][VERSION] header, deserializes the matching message and invokes the right handler.
 * - Handlers are grouped with the Overloaded helper (one lambda per message type). The handler set
 *   is checked at compile time: forgetting a lambda is a build error.
 * - Returns false for unknown IDs, mismatched VERSIONs or truncated frames, so the caller can
 *   count/log bad traffic.
 */

#include <Arduino.h>

#include "BytePack.h"
using namespace BytePack;

struct Ping {
  static constexpr uint8_t ID      = 0x01;
  static constexpr uint8_t VERSION = 1;

  uint8_t sequence = 0;

  template <typename Archive>
  constexpr void io(Archive& ar) {
    ar(sequence);
  }
};

struct SetRelay {
  static constexpr uint8_t ID      = 0x02;
  static constexpr uint8_t VERSION = 1;

  uint8_t channel = 0;
  bool on         = false;

  template <typename Archive>
  constexpr void io(Archive& ar) {
    ar(channel, on);
  }
};

struct ReportTemp {
  static constexpr uint8_t ID      = 0x03;
  static constexpr uint8_t VERSION = 1;

  Quant<int16_t, 100> celsius;

  template <typename Archive>
  constexpr void io(Archive& ar) {
    ar(celsius);
  }
};

// Single entry point for every received frame (e.g. from UART, radio, CAN...)
void handleFrame(const uint8_t* data, const size_t len) {
  const bool handled = dispatch<Ping, SetRelay, ReportTemp>(data,
    len,
    Overloaded{
      [](const Ping& msg) {
        Serial.print("Ping, sequence ");
        Serial.println(msg.sequence);
      },
      [](const SetRelay& msg) {
        Serial.print("SetRelay, channel ");
        Serial.print(msg.channel);
        Serial.print(" -> ");
        Serial.println(msg.on ? "ON" : "OFF");
      },
      [](const ReportTemp& msg) {
        Serial.print("ReportTemp, ");
        Serial.print(msg.celsius.getFloat(), 2);
        Serial.println(" C");
      },
    });

  if (!handled) {
    uint8_t id = 0;
    peekId(data, len, id);
    Serial.print("Unhandled frame, ID 0x");
    Serial.println(id, HEX);
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("---------------------------");
  Serial.println("BytePack - Dispatch Example");
  Serial.println("---------------------------");

  uint8_t buffer[16] = {};

  // Simulate a stream of received frames
  Ping ping;
  ping.sequence = 7;
  handleFrame(buffer, serializeWithHeader(ping, buffer, sizeof(buffer)));

  SetRelay relay;
  relay.channel = 2;
  relay.on      = true;
  handleFrame(buffer, serializeWithHeader(relay, buffer, sizeof(buffer)));

  ReportTemp temp;
  temp.celsius = 24.75f;
  handleFrame(buffer, serializeWithHeader(temp, buffer, sizeof(buffer)));

  // Unknown ID: dispatch returns false and the caller decides what to do
  const uint8_t unknown[3] = {0x7F, 0x01, 0x00};
  handleFrame(unknown, sizeof(unknown));
}

void loop() {}
