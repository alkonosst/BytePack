<h1 align="center">
  <a><img src=".img/logo.svg" alt="Logo" width="300"></a>
  <br>
  BytePack
</h1>

<p align="center">
  <b>Header-only message serialization library for C++17 with zero dynamic memory allocation.</b>
</p>

<p align="center">
  <a href="https://www.ardu-badge.com/BytePack">
    <img src="https://www.ardu-badge.com/badge/BytePack.svg?" alt="Arduino Library Badge">
  </a>
  <a href="https://registry.platformio.org/libraries/alkonosst/BytePack">
    <img src="https://badges.registry.platformio.org/packages/alkonosst/library/BytePack.svg" alt="PlatformIO Registry">
  </a>
  <br><br>
  <a href="https://codecov.io/github/alkonosst/BytePack">
    <img src="https://img.shields.io/codecov/c/github/alkonosst/BytePack?style=for-the-badge&logo=codecov&logoColor=white&labelColor=F01F7A" alt="Coverage">
  </a>
  <a href="https://opensource.org/licenses/MIT">
    <img src="https://img.shields.io/badge/license-MIT-blue.svg?style=for-the-badge&color=blue" alt="License">
  </a>
  <br><br>
  <a href="https://ko-fi.com/alkonosst">
    <img src="https://ko-fi.com/img/githubbutton_sm.svg" alt="Ko-fi">
  </a>
</p>

---

# Table of contents <!-- omit in toc -->

- [Description](#description)
- [Key Features](#key-features)
- [Quick Example](#quick-example)
- [Installation](#installation)
  - [PlatformIO](#platformio)
  - [Arduino IDE](#arduino-ide)
  - [CMake](#cmake)
- [Usage](#usage)
  - [Including the library](#including-the-library)
  - [Namespace](#namespace)
  - [Defining a Message](#defining-a-message)
  - [Supported Field Types](#supported-field-types)
  - [Serializing and Deserializing](#serializing-and-deserializing)
  - [Compile-Time Size Budgets](#compile-time-size-budgets)
  - [Quantized Values](#quantized-values)
  - [Variable-Length Payloads](#variable-length-payloads)
  - [Message Headers](#message-headers)
  - [Dispatching Frames](#dispatching-frames)
  - [Nested Messages](#nested-messages)
  - [Writer and Reader](#writer-and-reader)
  - [Wire Format](#wire-format)
  - [C++ Concepts](#c-concepts)
  - [Using with ByteFrame](#using-with-byteframe)
- [Release Status](#release-status)
- [License](#license)

---

# Description

**BytePack** is a header-only C++17 Arduino/Native library for serializing plain C++ structs into compact, portable byte buffers. A message is any struct that lists its fields in a single `io()` member function; that one function drives serialization, deserialization and compile-time size counting, so the field list is written once and can never get out of sync.

The wire format is explicit little-endian with no padding, making it safe to exchange between different architectures (ESP32, ARM, a PC on the other end of a link, etc.). All buffers are caller-provided and statically sized; there is no dynamic memory allocation.

# Key Features

- **Header-only** - A single `#include <BytePack.h>`; no source files, no dependencies.
- **One `io()` function** - The same field list drives `serialize()`, `deserialize()` and size counting. No duplicated schemas.
- **Zero dynamic allocation** - All buffers are caller-provided; no `new`, `malloc`, or `String` anywhere.
- **Portable wire format** - Explicit little-endian byte order and no struct padding; independent of the compiler and target architecture.
- **Compile-time size budgets** - `getMaxPackedSize()` is `constexpr`: size buffers exactly and enforce transport limits (e.g. a LoRa MTU) with `static_assert`.
- **Quantized values** - `Quant<T, Scale>` packs physical values (voltages, temperatures, angles) as scaled integers with saturation, so a float travels as 1 or 2 bytes.
- **Variable-length payloads** - `Bytes<LenT, MaxLen>` carries length-prefixed byte payloads; only the used part travels.
- **Message headers and dispatch** - Optional `[ID][VERSION]` frame header with strict matching, plus a `dispatch()` helper that routes incoming frames to per-message handlers.
- **Sticky error model** - The first value that does not fit puts the archive in a failed state; nothing partial is ever written or consumed.
- **Readable compile errors** - Unsupported field types and missing conventions fail with a clear `static_assert` message instead of an overload-resolution dump.
- **C++17 with optional C++20 concepts** - Uses concepts for cleaner errors when available, falling back to SFINAE on C++17 compilers.
- **Pairs with ByteFrame** - BytePack produces the payload; [ByteFrame](https://github.com/alkonosst/ByteFrame) delimits it over a raw stream with COBS, a CRC and a `0x00` delimiter, so messages keep their boundaries (see [Using with ByteFrame](#using-with-byteframe)).

# Quick Example

```cpp
#include <Arduino.h>

#include <BytePack.h>
using namespace BytePack;

// A message is any struct that lists its fields in io()
struct Telemetry {
  uint32_t uptime_ms  = 0;
  int16_t temperature = 0; // °C
  bool relay_on       = false;

  template <typename Archive>
  constexpr void io(Archive& ar) {
    ar(uptime_ms, temperature, relay_on);
  }
};

void setup() {
  Serial.begin(115200);

  // Buffer sized at compile time: 4 + 2 + 1 = 7 bytes
  uint8_t buffer[getMaxPackedSize<Telemetry>()] = {};

  // Serialize (e.g. on the transmitting device)
  Telemetry tx;
  tx.uptime_ms   = millis();
  tx.temperature = 2350; // 23.50 °C
  tx.relay_on    = true;

  const size_t written = serialize(tx, buffer, sizeof(buffer));
  if (written == 0) {
    Serial.println("Serialization failed: buffer too small");
    return;
  }

  // Deserialize into a fresh struct (e.g. on the receiving device)
  Telemetry rx;
  if (!deserialize(rx, buffer, written)) {
    Serial.println("Deserialization failed: truncated or invalid input");
    return;
  }

  Serial.print("temperature: ");
  Serial.println(rx.temperature);
}
```

# Installation

## PlatformIO

Add to your `platformio.ini`:

```ini
[env:your_env]
; Most recent changes
lib_deps =
  https://github.com/alkonosst/BytePack.git

; Pinned release (recommended for production)
lib_deps =
  https://github.com/alkonosst/BytePack.git#vx.y.z
```

## Arduino IDE

1. Open Arduino IDE.
2. Go to **Sketch > Manage Libraries...**
3. Search for **"BytePack"**.
4. Click **Install**.

## CMake

For desktop C++ projects, pull the library with `FetchContent` and link the `alkonosst::BytePack`
target:

```cmake
include(FetchContent)
FetchContent_Declare(
  BytePack
  GIT_REPOSITORY https://github.com/alkonosst/BytePack.git
  GIT_TAG        vx.y.z # pin a release tag (recommended), or a branch/commit
)
FetchContent_MakeAvailable(BytePack)

target_link_libraries(your_app PRIVATE alkonosst::BytePack)
```

# Usage

## Including the library

A single header includes everything:

```cpp
#include <BytePack.h>
```

## Namespace

All public types live in the `BytePack` namespace:

```cpp
using namespace BytePack;
```

## Defining a Message

A message is a plain struct with a templated `io()` member function that passes every field, in wire order, to the archive:

```cpp
struct Telemetry {
  uint32_t uptime_ms  = 0;
  int16_t temperature = 0;
  bool relay_on       = false;

  template <typename Archive>
  constexpr void io(Archive& ar) {
    ar(uptime_ms, temperature, relay_on);
  }
};
```

The same `io()` is reused by every archive:

- `Writer` serializes the fields into a buffer.
- `Reader` deserializes a buffer back into the fields.
- `SizeCounter` measures the worst-case packed size at compile time.

Declaring `io()` as `constexpr` is optional, but required if you want to use `getMaxPackedSize()` in `static_assert` or to size arrays.

> [!IMPORTANT]
> The field order in `io()` **is** the wire format. Reordering, adding or removing fields changes the layout, so both ends of the link must agree on it (see [Message Headers](#message-headers) for a versioning convention).

## Supported Field Types

| Type                             | Packed as                                                     |
| -------------------------------- | ------------------------------------------------------------- |
| `bool`                           | 1 byte (`0` or `1`)                                           |
| Integers (`uint8_t`...`int64_t`) | `sizeof(T)` bytes, little-endian                              |
| `enum` / `enum class`            | Its underlying integer type                                   |
| `float`                          | 4 bytes (IEEE-754 bit pattern)                                |
| `double`                         | 8 bytes (IEEE-754 bit pattern)                                |
| `Quant<T, Scale>`                | `sizeof(T)` bytes (the raw quantized integer)                 |
| `Bytes<LenT, MaxLen>`            | `sizeof(LenT)` length prefix + the used payload bytes         |
| Fixed C arrays (`T[N]`)          | Each element in order, no length prefix                       |
| Structs providing `io()`         | Their fields inline (see [Nested Messages](#nested-messages)) |

Any other field type fails to compile with a descriptive `static_assert` message.

## Serializing and Deserializing

`serialize()` writes the fields of a message into a caller-provided buffer and returns the number of bytes written, or `0` if the message did not fit:

```cpp
uint8_t buffer[getMaxPackedSize<Telemetry>()] = {};

const size_t written = serialize(msg, buffer, sizeof(buffer));
if (written == 0) {
  // Buffer too small: nothing partial was written
}
```

`deserialize()` fills a message from a buffer and returns `false` on truncated or invalid input:

```cpp
Telemetry msg;
if (!deserialize(msg, data, data_len)) {
  // Truncated/invalid input: remaining fields were left untouched
}
```

## Compile-Time Size Budgets

`getMaxPackedSize<Msg>()` returns the worst-case packed size of a message, computed at compile time (requires `constexpr io()`). It is exact for messages without `Bytes<>` fields; `Bytes<>` fields are counted at full capacity.

Use it to size buffers exactly:

```cpp
uint8_t buffer[getMaxPackedSize<Telemetry>()] = {};
```

And to enforce transport budgets at compile time. If a message outgrows the link MTU, the firmware stops compiling instead of failing in the field:

```cpp
// Example transport limit: LoRa payload at the slowest data rate
constexpr size_t LORA_MAX_PAYLOAD = 51;

static_assert(getMaxPackedSizeWithHeader<StatusReport>() <= LORA_MAX_PAYLOAD,
  "StatusReport does not fit in a LoRa payload");
```

`getMaxPackedSizeWithHeader<Msg>()` is the same but adds the 2-byte `[ID][VERSION]` header (see [Message Headers](#message-headers)).

## Quantized Values

`Quant<T, Scale>` stores a physical value as a scaled integer (`physical = raw / Scale`), so a float travels as 1, 2 or 4 bytes instead of a full `float`. Assignment quantizes and saturates immediately; reading dequantizes:

```cpp
// 0.01 V resolution, range -327.68 to 327.67 V, travels as 2 bytes
Quant<int16_t, 100> voltage;

voltage = 3.31f;                  // quantizes to raw 331
float v = voltage;                // reads back as 3.31
float v2 = voltage.getFloat();    // explicit alternative
int16_t raw = voltage.getRaw();   // the integer that actually travels
```

> [!NOTE]
> The `float` constructor is `explicit`, so a `Quant` field is initialized with **braces**, not `=`: write `Quant<int16_t, 100> voltage{3.31f};` for a default member initializer (or `.voltage{3.31f}` in a designated initializer). `voltage = 3.31f` does not compile in those positions, because copy-initialization cannot use an explicit constructor; assigning to an existing value (`voltage = 3.31f;`, as shown above) works through `operator=`.

Out-of-range values saturate at the bounds instead of overflowing, and NaN maps to 0. The type is fully introspectable at compile time:

```cpp
using Voltage = Quant<int16_t, 100>;

Voltage::getResolution(); // 0.01  (smallest representable step)
Voltage::getMinValue();   // -327.68
Voltage::getMaxValue();   // 327.67
Voltage::fits(400.0f);    // false (would saturate; useful as a clipping detector)
```

## Variable-Length Payloads

`Bytes<LenT, MaxLen>` is a length-prefixed byte payload with a fixed maximum capacity. On the wire it travels as `[length: sizeof(LenT) bytes][data: length bytes]` - only the used part is transmitted, but size budgets count it at full capacity:

```cpp
Bytes<uint8_t, 16> note; // up to 16 bytes, 1-byte length prefix

// Option A: copy an existing buffer (validated, fails if it does not fit)
const uint8_t raw[3] = {0xDE, 0xAD, 0xBE};
note.set(raw, sizeof(raw));

// Option B: fill in place, then commit the length (setLength validates)
snprintf(reinterpret_cast<char*>(note.getBuffer()), note.getCapacity(), "v=%d", 42);
note.setLength(4);

note.getData();     // read-only pointer to the payload
note.getLength();   // current payload length
note.getCapacity(); // MaxLen
note.clear();       // length 0 (data bytes are not wiped)
```

On deserialization, a length prefix larger than `MaxLen` or pointing past the end of the buffer is rejected as an error.

## Message Headers

For frames traveling over a real link, BytePack offers a 2-byte header convention: declare `static constexpr uint8_t ID` (which message is this) and `static constexpr uint8_t VERSION` (which layout revision) on the struct. Both are enforced at compile time by the `WithHeader` helpers:

```cpp
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
```

`serializeWithHeader()` produces a `[ID][VERSION][fields]` frame; `deserializeWithHeader()` checks the header with strict equality and rejects any frame whose `ID` or `VERSION` does not match, without touching the message:

```cpp
uint8_t buffer[getMaxPackedSizeWithHeader<SetPoint>()] = {};

const size_t written = serializeWithHeader(tx, buffer, sizeof(buffer));

SetPoint rx;
if (!deserializeWithHeader(rx, buffer, written)) {
  // Wrong ID, wrong VERSION, or truncated frame
}
```

`peekId()` and `peekVersion()` inspect the header of any frame without consuming it, e.g. to log unknown traffic:

```cpp
uint8_t id = 0;
if (peekId(data, data_len, id)) {
  Serial.print("Frame ID: 0x");
  Serial.println(id, HEX);
}
```

## Dispatching Frames

`dispatch<Msgs...>()` routes an incoming `[ID][VERSION][fields]` frame to the handler of the matching message type: it reads the header, deserializes the matching message and invokes the right handler. Handlers are grouped with the `Overloaded` helper, one lambda per message type, and the set is checked at compile time - forgetting a lambda is a build error:

```cpp
// Single entry point for every received frame (e.g. from UART, radio, CAN...)
void handleFrame(const uint8_t* data, const size_t len) {
  const bool handled = dispatch<Ping, SetRelay, ReportTemp>(data, len,
    Overloaded{
      [](const Ping& msg) { /* reply with Pong */ },
      [](const SetRelay& msg) { digitalWrite(msg.channel, msg.on); },
      [](const ReportTemp& msg) { Serial.println(msg.celsius.getFloat(), 2); },
    });

  if (!handled) {
    // Unknown ID, mismatched VERSION or truncated frame: count/log it
  }
}
```

`dispatch()` returns `true` if a message matched, deserialized correctly and was handled.

## Nested Messages

Any struct with `io()` can be a field of another message, including fixed C arrays of structs. This lets you compose messages from reusable sub-structs:

```cpp
// Reusable sub-struct: no header members needed, it always travels inline
struct Position {
  Quant<int16_t, 100> x; // meters, 1 cm resolution
  Quant<int16_t, 100> y;

  template <typename Archive>
  constexpr void io(Archive& ar) {
    ar(x, y);
  }
};

// Top-level message: owns the header and embeds an array of sub-structs
struct Route {
  static constexpr uint8_t ID      = 0x30;
  static constexpr uint8_t VERSION = 1; // covers Route AND its nested structs

  uint8_t count = 0;
  Position waypoints[3];

  template <typename Archive>
  constexpr void io(Archive& ar) {
    ar(count, waypoints);
  }
};
```

Nested structs are serialized inline (fields only, no header), even if they declare `ID`/`VERSION` themselves. The header exists only at the outermost level.

> [!NOTE]
> Consequence for versioning: if a shared sub-struct changes its layout, bump the `VERSION` of every top-level message that embeds it.

## Writer and Reader

The `Writer` and `Reader` archives can be used directly, without defining a message struct. Useful for ad-hoc protocols or when the field list is not fixed at compile time:

```cpp
uint8_t buffer[16] = {};

// Writing fields directly (variadic: writes fields in order)
Writer w(buffer, sizeof(buffer));
w(uint8_t(0x01), uint16_t(512), 1.5f);

if (w.isOk()) {
  send(buffer, w.getSize());
}

// Reading fields back
Reader r(buffer, w.getSize());

uint8_t command = 0;
uint16_t param  = 0;
float gain      = 0.0f;
r(command, param, gain);

if (!r.isOk()) {
  // Truncated input
}
```

Errors are sticky: the first value that does not fit puts the archive in a failed state, nothing partial is written or consumed, and `isOk()` returns `false` for all subsequent operations. `Writer::reset()` rewinds and clears the error.

| Method               | Archive | Description                                  |
| -------------------- | ------- | -------------------------------------------- |
| `isOk()`             | Both    | `false` if any value failed to fit (sticky). |
| `getSize()`          | Writer  | Bytes written so far.                        |
| `getCapacity()`      | Writer  | Capacity of the wrapped buffer.              |
| `reset()`            | Writer  | Rewind to the start and clear the error.     |
| `getConsumedSize()`  | Reader  | Bytes consumed so far.                       |
| `getRemainingSize()` | Reader  | Bytes remaining in the buffer.               |

## Wire Format

The format is deliberately simple and has no magic bytes, padding or alignment:

- All multi-byte values are **little-endian**, regardless of the host architecture.
- `bool` is 1 byte (`0` or `1`); any nonzero byte reads back as `true`.
- `float`/`double` travel as their IEEE-754 bit patterns (4/8 bytes).
- Enums travel as their underlying integer type.
- `Bytes<>` is a length prefix followed by exactly that many payload bytes.
- Arrays and nested structs are flattened in field order, with no extra bytes.
- The optional header is exactly 2 bytes: `[ID][VERSION]`, before the fields.

This makes frames easy to parse from non-C++ peers (Python scripts, PC tools, etc.) and keeps the packed size predictable byte by byte.

## C++ Concepts

On C++20 compilers, the API is constrained with concepts (`Concepts::Serializable`, `Concepts::SerializableWithHeader`, `Concepts::PackedInteger`, etc.), which produce cleaner error messages and can be used in your own generic code:

```cpp
template <BytePack::Concepts::Serializable Msg>
bool sendMessage(const Msg& msg) { /* ... */ }
```

On C++17 compilers the library automatically falls back to an equivalent SFINAE implementation - same API, same behavior. To force the fallback on a C++20 compiler, define `BYTEPACK_DISABLE_CONCEPTS` before including the library or in `build_flags`:

```ini
[env:your_env]
build_flags = -DBYTEPACK_DISABLE_CONCEPTS
```

## Using with ByteFrame

BytePack turns a struct into a compact byte buffer, but on a raw stream (UART, RS-485, a radio, raw TCP) the receiver still needs to know where each message starts and ends, and whether it arrived intact. That framing is exactly what [ByteFrame](https://github.com/alkonosst/ByteFrame) adds: it wraps any payload in [COBS](https://en.wikipedia.org/wiki/Consistent_Overhead_Byte_Stuffing) encoding, a selectable CRC and a single `0x00` delimiter. The two libraries compose cleanly - BytePack gives the payload a type-safe layout, ByteFrame delimits and protects it - and their compile-time size helpers nest, so every buffer is sized exactly:

```cpp
#include <BytePack.h>
#include <ByteFrame.h>

// Define a message as a BytePack struct with Header fields for dispatching
struct Telemetry {
  static constexpr uint8_t ID      = 0x01;
  static constexpr uint8_t VERSION = 1;

  uint32_t uptime_ms  = 0;
  int16_t temperature = 0;

  template <typename Archive>
  constexpr void io(Archive& ar) {
    ar(uptime_ms, temperature);
  }
};

// Worst-case sizes, fully computed at compile time
constexpr size_t MAX_PAYLOAD = BytePack::getMaxPackedSizeWithHeader<Telemetry>();
constexpr size_t MAX_FRAME   = ByteFrame::getMaxEncodedSize(MAX_PAYLOAD);

// Transmit: serialize, then frame
void sendTelemetry(const Telemetry& msg) {
  uint8_t payload[MAX_PAYLOAD] = {};
  uint8_t frame[MAX_FRAME]     = {};

  const size_t payload_size = BytePack::serializeWithHeader(msg, payload, sizeof(payload));
  const size_t frame_size   = ByteFrame::encode(payload, payload_size, frame, sizeof(frame));
  if (frame_size > 0) {
    Serial1.write(frame, frame_size);
  }
}

// Receive: deframe, then dispatch
ByteFrame::Decoder<MAX_PAYLOAD> decoder;

void loop() {
  while (Serial1.available()) {
    uint8_t b = Serial1.read();
    if (decoder.feed(b)) {
      BytePack::dispatch<Telemetry>(decoder.getPayload(), decoder.getPayloadSize(),
        BytePack::Overloaded{
          [](const Telemetry& msg) { /* handle it */ },
        });
    }
  }
}
```

Sent back to back on a stream, raw BytePack buffers cannot be told apart; ByteFrame solves that boundary problem while preserving BytePack's exact, allocation-free size budgets.

# Release Status

This project is in active development. Until reaching version **v1.0.0**, consider it **beta software**. APIs may change in future releases, and some features may be incomplete or unstable. Please report any issues on the [GitHub Issues](https://github.com/alkonosst/BytePack/issues) page.

# License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
