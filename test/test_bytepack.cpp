/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * Unit tests for BytePack.
 *
 * Each test group covers one feature area. Tests use Unity (available via PlatformIO).
 *
 * Coverage:
 * - Quant: constexpr introspection (resolution, min/max, fits), quantization and dequantization,
 *   saturation at both limits, NaN handling, setRaw/getRaw round-trip.
 * - Bytes: capacity, set() validation (valid and oversized), in-place fill via
 *   getBuffer()/setLength(), setLength() rejection beyond capacity, clear().
 * - Writer: initial state, byte-exact little-endian encoding of every supported field type,
 *   overflow (sticky error, no partial writes), reset semantics.
 * - Reader: initial state, byte-exact decoding, truncated input (sticky error, nothing consumed on
 *   failure), trailing bytes ignored, invalid/oversized Bytes length prefix, bool decoded from any
 *   non-zero byte, consumed/remaining size tracking.
 * - Helpers: serialize/deserialize (including overflow -> 0 and truncated -> false),
 *   serializeWithHeader/deserializeWithHeader (byte-exact [ID][VERSION][fields] layout, wrong ID,
 *   wrong VERSION, header-only and too-short input), peekId/peekVersion,
 *   getMaxPackedSize/getMaxPackedSizeWithHeader, dispatch (handler selection, unknown ID, wrong
 *   VERSION, truncated body, empty and header-only input).
 * - Round-trip: full-field messages, Quant + Bytes messages, C arrays, nested structs, and the
 *   complete header + peek + dispatch pipeline.
 * - Stress (10000 iterations each, deterministic xorshift32 PRNG): random garbage input (no crash,
 *   invariants hold), random value round-trips (floats/doubles compared bitwise, NaNs included),
 *   truncation of valid messages and single-byte corruption.
 */

#include <Arduino.h>

#define UNITY_INCLUDE_DOUBLE
#include <unity.h>

#include <BytePack.h>
using namespace BytePack;

/* ---------------------------------------------------------------------------------------------- */
/*                                 Types and messages for testing                                 */
/* ---------------------------------------------------------------------------------------------- */

// Float-integer union for testing float bit patterns
union FloatInt {
  float f;
  uint32_t u;
};

// Double-integer union for testing double bit patterns
union DoubleInt {
  double d;
  uint64_t u;
};

struct Msg1 {
  bool b       = false; // 1 byte
  uint8_t u8   = 0;     // 1 byte
  int8_t i8    = 0;     // 1 byte
  uint16_t u16 = 0;     // 2 bytes
  int16_t i16  = 0;     // 2 bytes
  uint32_t u32 = 0;     // 4 bytes
  int32_t i32  = 0;     // 4 bytes
  float f      = 0.0f;  // 4 bytes
  double d     = 0.0;   // 8 bytes

  enum class MyEnum : uint16_t { A = 1, B = 2, C = 3 };
  MyEnum e = MyEnum::A; // 2 bytes (same as underlying type)
  // Total: 29 bytes

  template <typename Archive>
  constexpr void io(Archive& ar) {
    ar(b, u8, i8, u16, i16, u32, i32, f, d, e);
  }
};

struct Msg2 {
  Quant<int16_t, 100> q; // 2 bytes
  Bytes<uint8_t, 5> b;   // 1 byte for length + up to 5 bytes for data

  template <typename Archive>
  constexpr void io(Archive& ar) {
    ar(q, b);
  }
};

// Messages with ID + VERSION header, for serializeWithHeader / deserializeWithHeader / dispatch
struct MsgPing {
  static constexpr uint8_t ID      = 0x10;
  static constexpr uint8_t VERSION = 1;

  uint8_t counter = 0;

  template <typename Archive>
  constexpr void io(Archive& ar) {
    ar(counter);
  }
};

struct MsgData {
  static constexpr uint8_t ID      = 0x20;
  static constexpr uint8_t VERSION = 3;

  uint16_t value = 0;
  float ratio    = 0.0f;

  template <typename Archive>
  constexpr void io(Archive& ar) {
    ar(value, ratio);
  }
};

// Message with a fixed C array
struct MsgArray {
  uint16_t values[3] = {};

  template <typename Archive>
  constexpr void io(Archive& ar) {
    ar(values);
  }
};

// Message nesting another serializable struct
struct MsgNested {
  MsgPing inner;
  uint8_t extra = 0;

  template <typename Archive>
  constexpr void io(Archive& ar) {
    ar(inner, extra);
  }
};

/* ---------------------------------------------------------------------------------------------- */
/*                                          Custom types                                          */
/* ---------------------------------------------------------------------------------------------- */

void test_type_quant_int16_t() {
  // Signed 16-bit with 2 decimal places (0.01 resolution, range -327.68 to 327.67)
  constexpr Quant<int16_t, 100> q1(10.0f);
  constexpr float q1_res            = q1.getResolution();
  constexpr float q1_min            = q1.getMinValue();
  constexpr float q1_max            = q1.getMaxValue();
  constexpr bool q1_fits_10         = q1.fits(10.0f);
  constexpr bool q1_fits_327_67     = q1.fits(327.67f);
  constexpr bool q1_fits_327_68     = q1.fits(327.68f);
  constexpr bool q1_fits_neg_327_68 = q1.fits(-327.68f);
  constexpr bool q1_fits_neg_327_69 = q1.fits(-327.69f);
  constexpr float q1_float          = q1.getFloat();
  constexpr float q1_float_op       = q1;
  constexpr int16_t q1_raw          = q1.getRaw();
  constexpr int16_t q1_quantized    = q1.quantize(10.0f);
  constexpr float q1_dequantized    = q1.dequantize(q1_quantized);
  TEST_ASSERT_EQUAL_FLOAT(0.01f, q1_res);
  TEST_ASSERT_EQUAL_FLOAT(-327.68f, q1_min);
  TEST_ASSERT_EQUAL_FLOAT(327.67f, q1_max);
  TEST_ASSERT_TRUE(q1_fits_10);
  TEST_ASSERT_TRUE(q1_fits_327_67);
  TEST_ASSERT_FALSE(q1_fits_327_68);
  TEST_ASSERT_TRUE(q1_fits_neg_327_68);
  TEST_ASSERT_FALSE(q1_fits_neg_327_69);
  TEST_ASSERT_EQUAL_FLOAT(10.0f, q1_float);
  TEST_ASSERT_EQUAL_FLOAT(10.0f, q1_float_op);
  TEST_ASSERT_EQUAL_INT16(1000, q1_raw);
  TEST_ASSERT_EQUAL_INT16(1000, q1_quantized);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, q1_dequantized);
}

void test_type_quant_uint8_t() {
  // Unsigned 8-bit with 1 decimal place (0.1 resolution, range 0 to 25.5)
  constexpr Quant<uint8_t, 10> q2(12.3f);
  constexpr float q2_res         = q2.getResolution();
  constexpr float q2_min         = q2.getMinValue();
  constexpr float q2_max         = q2.getMaxValue();
  constexpr bool q2_fits_12_3    = q2.fits(12.3f);
  constexpr bool q2_fits_25_5    = q2.fits(25.5f);
  constexpr bool q2_fits_25_6    = q2.fits(25.6f);
  constexpr float q2_float       = q2.getFloat();
  constexpr float q2_float_op    = q2;
  constexpr uint8_t q2_raw       = q2.getRaw();
  constexpr uint8_t q2_quantized = q2.quantize(12.3f);
  constexpr float q2_dequantized = q2.dequantize(q2_quantized);
  TEST_ASSERT_EQUAL_FLOAT(0.1f, q2_res);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, q2_min);
  TEST_ASSERT_EQUAL_FLOAT(25.5f, q2_max);
  TEST_ASSERT_TRUE(q2_fits_12_3);
  TEST_ASSERT_TRUE(q2_fits_25_5);
  TEST_ASSERT_FALSE(q2_fits_25_6);
  TEST_ASSERT_EQUAL_FLOAT(12.3f, q2_float);
  TEST_ASSERT_EQUAL_FLOAT(12.3f, q2_float_op);
  TEST_ASSERT_EQUAL_UINT8(123, q2_raw);
  TEST_ASSERT_EQUAL_UINT8(123, q2_quantized);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 12.3f, q2_dequantized);
}

void test_type_quant_saturation() {
  Quant<int16_t, 100> q;

  // Above max -> saturates to max raw value
  q = 400.0f;
  TEST_ASSERT_EQUAL_INT16(32767, q.getRaw());

  // Below min -> saturates to min raw value
  q = -400.0f;
  TEST_ASSERT_EQUAL_INT16(-32768, q.getRaw());

  // NaN -> 0
  q = NAN;
  TEST_ASSERT_EQUAL_INT16(0, q.getRaw());

  // setRaw / getRaw round-trip
  q.setRaw(1234);
  TEST_ASSERT_EQUAL_INT16(1234, q.getRaw());
  TEST_ASSERT_EQUAL_FLOAT(12.34f, q.getFloat());
}

void test_type_bytes() {
  Bytes<uint8_t, 10> b;

  constexpr size_t capacity = b.getCapacity();
  TEST_ASSERT_EQUAL(10, capacity);

  // Initial state
  TEST_ASSERT_EQUAL(0, b.getLength());
  for (size_t i = 0; i < capacity; i++) {
    TEST_ASSERT_EQUAL(0, b.getData()[i]);
  }

  // Set with valid length
  uint8_t src[5]  = {1, 2, 3, 4, 5};
  bool set_result = b.set(src, 5);

  TEST_ASSERT_TRUE(set_result);
  TEST_ASSERT_EQUAL(5, b.getLength());

  for (size_t i = 0; i < 5; i++) {
    TEST_ASSERT_EQUAL(src[i], b.getData()[i]);
  }

  // Set with length exceeding capacity
  uint8_t src2[11] = {0};
  set_result       = b.set(src2, 11);
  TEST_ASSERT_FALSE(set_result);
  TEST_ASSERT_EQUAL(5, b.getLength()); // length should remain unchanged
  for (size_t i = 0; i < 5; i++) {
    TEST_ASSERT_EQUAL(src[i], b.getData()[i]); // data should remain unchanged
  }

  // In-place fill: write through getBuffer, then commit with setLength
  b.getBuffer()[0] = 0xEE;
  TEST_ASSERT_TRUE(b.setLength(1));
  TEST_ASSERT_EQUAL(1, b.getLength());
  TEST_ASSERT_EQUAL(0xEE, b.getData()[0]);

  // setLength beyond capacity -> rejected, length unchanged
  TEST_ASSERT_FALSE(b.setLength(11));
  TEST_ASSERT_EQUAL(1, b.getLength());

  // Clear resets the length (data content is irrelevant once cleared)
  b.clear();
  TEST_ASSERT_EQUAL(0, b.getLength());
}

/* ---------------------------------------------------------------------------------------------- */
/*                                             Writer                                             */
/* ---------------------------------------------------------------------------------------------- */

void test_writer_initial_state() {
  uint8_t buffer[10]{};
  Writer w(buffer, sizeof(buffer));

  TEST_ASSERT_TRUE(w.isOk());
  TEST_ASSERT_EQUAL(0, w.getSize());
  TEST_ASSERT_EQUAL(sizeof(buffer), w.getCapacity());
  for (size_t i = 0; i < sizeof(buffer); i++) {
    TEST_ASSERT_EQUAL(0, buffer[i]);
  }
}

void test_writer_common_types() {
  constexpr size_t packed_size = getMaxPackedSize<Msg1>();
  TEST_ASSERT_EQUAL(29, packed_size);

  uint8_t buffer[packed_size] = {};
  Writer w(buffer, sizeof(buffer));

  // Write values
  Msg1 msg{};
  msg.b   = true;
  msg.u8  = 0xAA;
  msg.i8  = 0xBB;
  msg.u16 = 0xAABB;
  msg.i16 = 0xBBCC;
  msg.u32 = 0xAABBCCDD;
  msg.i32 = 0xBBCCDDEE;
  msg.f   = 3.14f;
  msg.d   = 2.71828;
  msg.e   = Msg1::MyEnum::B;
  msg.io(w);

  TEST_ASSERT_TRUE(w.isOk());
  TEST_ASSERT_EQUAL(packed_size, w.getSize());

  // Expected byte values (little-endian)
  uint8_t expected[packed_size]{};
  expected[0] = 1; // b

  expected[1] = 0xAA; // u8

  expected[2] = 0xBB; // i8

  expected[3] = 0xBB; // u16 (LSB)
  expected[4] = 0xAA; // u16 (MSB)

  expected[5] = 0xCC; // i16 (LSB)
  expected[6] = 0xBB; // i16 (MSB)

  expected[7]  = 0xDD; // u32 (LSB)
  expected[8]  = 0xCC; // u32
  expected[9]  = 0xBB; // u32
  expected[10] = 0xAA; // u32 (MSB)

  expected[11] = 0xEE; // i32 (LSB)
  expected[12] = 0xDD; // i32
  expected[13] = 0xCC; // i32
  expected[14] = 0xBB; // i32 (MSB)

  // For float and double, we use the union helpers
  FloatInt f_union;
  f_union.f = 3.14f;
  DoubleInt d_union;
  d_union.d = 2.71828;

  expected[15] = f_union.u & 0xFF;         // f (LSB)
  expected[16] = (f_union.u >> 8) & 0xFF;  // f
  expected[17] = (f_union.u >> 16) & 0xFF; // f
  expected[18] = (f_union.u >> 24) & 0xFF; // f (MSB)

  expected[19] = d_union.u & 0xFF;         // d (LSB)
  expected[20] = (d_union.u >> 8) & 0xFF;  // d
  expected[21] = (d_union.u >> 16) & 0xFF; // d
  expected[22] = (d_union.u >> 24) & 0xFF; // d
  expected[23] = (d_union.u >> 32) & 0xFF; // d
  expected[24] = (d_union.u >> 40) & 0xFF; // d
  expected[25] = (d_union.u >> 48) & 0xFF; // d
  expected[26] = (d_union.u >> 56) & 0xFF; // d (MSB)

  expected[27] = 0x02; // e (LSB)
  expected[28] = 0x00; // e (MSB)

  for (size_t i = 0; i < sizeof(buffer); i++) {
    TEST_ASSERT_EQUAL(expected[i], buffer[i]);
  }

  // Reset (rewinds the position, the buffer content is not cleared)
  w.reset();
  TEST_ASSERT_TRUE(w.isOk());
  TEST_ASSERT_EQUAL(0, w.getSize());
}

void test_writer_custom_types() {
  constexpr size_t packed_size = getMaxPackedSize<Msg2>();
  TEST_ASSERT_EQUAL(8, packed_size);

  uint8_t buffer[packed_size] = {};
  Writer w(buffer, sizeof(buffer));

  // Write values
  Msg2 msg{};
  msg.q          = 12.34f;
  uint8_t src[3] = {0xDE, 0xAD, 0xBE}; // 2 bytes left empty on purpose
  msg.b.set(src, sizeof(src));
  msg.io(w);

  TEST_ASSERT_TRUE(w.isOk());
  TEST_ASSERT_EQUAL(packed_size - 2, w.getSize()); // 2 bytes left empty on purpose

  // Expected byte values (little-endian)
  uint8_t expected[packed_size]{};
  int16_t q_raw = msg.q.getRaw(); // Quantized value for q

  expected[0] = q_raw & 0xFF;        // q (LSB)
  expected[1] = (q_raw >> 8) & 0xFF; // q (MSB)

  expected[2] = msg.b.getLength(); // b length
  expected[3] = src[0];            // b data[0]
  expected[4] = src[1];            // b data[1]
  expected[5] = src[2];            // b data[2]
  expected[6] = 0;                 // b empty data
  expected[7] = 0;                 // b empty data

  for (size_t i = 0; i < sizeof(buffer); i++) {
    TEST_ASSERT_EQUAL(expected[i], buffer[i]);
  }

  // Reset (rewinds the position, the buffer content is not cleared)
  w.reset();
  TEST_ASSERT_TRUE(w.isOk());
  TEST_ASSERT_EQUAL(0, w.getSize());
}

void test_writer_overflow() {
  uint8_t buffer[3]{};
  Writer w(buffer, sizeof(buffer));

  // First byte fits
  uint8_t u8 = 0x11;
  w(u8);
  TEST_ASSERT_TRUE(w.isOk());
  TEST_ASSERT_EQUAL(1, w.getSize());

  // 4 bytes do not fit in the 2 remaining -> error, nothing partial written
  uint32_t u32 = 0xAABBCCDD;
  w(u32);
  TEST_ASSERT_FALSE(w.isOk());
  TEST_ASSERT_EQUAL(1, w.getSize());

  // Error is sticky until reset
  w(u8);
  TEST_ASSERT_FALSE(w.isOk());
  TEST_ASSERT_EQUAL(1, w.getSize());

  w.reset();
  TEST_ASSERT_TRUE(w.isOk());
  TEST_ASSERT_EQUAL(0, w.getSize());
}

/* ---------------------------------------------------------------------------------------------- */
/*                                             Reader                                             */
/* ---------------------------------------------------------------------------------------------- */

void test_reader_initial_state() {
  uint8_t buffer[10]{};
  Reader r(buffer, sizeof(buffer));

  TEST_ASSERT_TRUE(r.isOk());
  TEST_ASSERT_EQUAL(0, r.getConsumedSize());
  TEST_ASSERT_EQUAL(sizeof(buffer), r.getRemainingSize());
  for (size_t i = 0; i < sizeof(buffer); i++) {
    TEST_ASSERT_EQUAL(0, buffer[i]);
  }
}

void test_reader_common_types() {
  FloatInt f_union;
  f_union.f = 3.14f;
  DoubleInt d_union;
  d_union.d = 2.71828;

  constexpr size_t packed_size = getMaxPackedSize<Msg1>();
  TEST_ASSERT_EQUAL(29, packed_size);

  // Prepare buffer with known values (same as in test_writer_common_types)
  uint8_t buffer[packed_size] = {
    1,    // b
    0xAA, // u8
    0xBB, // i8
    0xBB, // u16
    0xAA,
    0xCC, // i16
    0xBB,
    0xDD, // u32
    0xCC,
    0xBB,
    0xAA,
    0xEE, // i32
    0xDD,
    0xCC,
    0xBB,
    uint8_t(f_union.u & 0xFF), // f
    uint8_t((f_union.u >> 8) & 0xFF),
    uint8_t((f_union.u >> 16) & 0xFF),
    uint8_t((f_union.u >> 24) & 0xFF),
    uint8_t(d_union.u & 0xFF), // d
    uint8_t((d_union.u >> 8) & 0xFF),
    uint8_t((d_union.u >> 16) & 0xFF),
    uint8_t((d_union.u >> 24) & 0xFF),
    uint8_t((d_union.u >> 32) & 0xFF),
    uint8_t((d_union.u >> 40) & 0xFF),
    uint8_t((d_union.u >> 48) & 0xFF),
    uint8_t((d_union.u >> 56) & 0xFF),
    0x02, // e
    0x00,
  };

  Reader r(buffer, sizeof(buffer));
  Msg1 msg{};
  msg.io(r);

  TEST_ASSERT_TRUE(r.isOk());
  TEST_ASSERT_EQUAL(true, msg.b);
  TEST_ASSERT_EQUAL_UINT8(0xAA, msg.u8);
  TEST_ASSERT_EQUAL_INT8(0xBB, msg.i8);
  TEST_ASSERT_EQUAL_UINT16(0xAABB, msg.u16);
  TEST_ASSERT_EQUAL_INT16(0xBBCC, msg.i16);
  TEST_ASSERT_EQUAL_UINT32(0xAABBCCDD, msg.u32);
  TEST_ASSERT_EQUAL_INT32(0xBBCCDDEE, msg.i32);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.14f, msg.f);
  TEST_ASSERT_DOUBLE_WITHIN(0.0001, 2.71828, msg.d);
  TEST_ASSERT_EQUAL(Msg1::MyEnum::B, msg.e);
}

void test_reader_custom_types() {
  Quant<int16_t, 100> q(12.3f);

  // Prepare buffer with known values (same as in test_writer_custom_types)
  uint8_t buffer[8] = {
    uint8_t(q.getRaw() & 0xFF),        // q (LSB)
    uint8_t((q.getRaw() >> 8) & 0xFF), // q (MSB)
    3,                                 // b length
    0xDE,
    0xAD,
    0xBE,
    0, // b empty data
    0, // b empty data
  };

  Reader r(buffer, sizeof(buffer));
  Msg2 msg{};
  msg.io(r);

  TEST_ASSERT_TRUE(r.isOk());
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 12.3f, msg.q.getFloat());
  TEST_ASSERT_EQUAL_UINT8(3, msg.b.getLength());
  TEST_ASSERT_EQUAL(0xDE, msg.b.getData()[0]);
  TEST_ASSERT_EQUAL(0xAD, msg.b.getData()[1]);
  TEST_ASSERT_EQUAL(0xBE, msg.b.getData()[2]);
}

void test_reader_truncated_input() {
  uint8_t buffer[3] = {0x11, 0x22, 0x33};
  Reader r(buffer, sizeof(buffer));

  // First byte is available
  uint8_t u8 = 0;
  r(u8);
  TEST_ASSERT_TRUE(r.isOk());
  TEST_ASSERT_EQUAL_UINT8(0x11, u8);
  TEST_ASSERT_EQUAL(1, r.getConsumedSize());

  // 4 bytes are not available in the 2 remaining -> error, nothing consumed
  uint32_t u32 = 0;
  r(u32);
  TEST_ASSERT_FALSE(r.isOk());
  TEST_ASSERT_EQUAL(1, r.getConsumedSize());

  // Error is sticky
  r(u8);
  TEST_ASSERT_FALSE(r.isOk());
  TEST_ASSERT_EQUAL(1, r.getConsumedSize());
}

void test_reader_trailing_bytes_ignored() {
  uint8_t buffer[8] = {42}; // 1 message byte + 7 trailing bytes
  Reader r(buffer, sizeof(buffer));

  MsgPing msg{};
  msg.io(r);

  TEST_ASSERT_TRUE(r.isOk());
  TEST_ASSERT_EQUAL_UINT8(42, msg.counter);
  TEST_ASSERT_EQUAL(1, r.getConsumedSize());
  TEST_ASSERT_EQUAL(sizeof(buffer) - 1, r.getRemainingSize());
}

void test_reader_bytes_invalid_len() {
  // Malicious/corrupted input: Bytes length prefix exceeds MaxLen (5)
  uint8_t buffer[10] = {0, 0, 10, 1, 2, 3, 4, 5, 6, 7}; // quant(2B) + len=10 + data
  Reader r(buffer, sizeof(buffer));
  Msg2 msg{};
  msg.io(r);

  TEST_ASSERT_FALSE(r.isOk());
  TEST_ASSERT_EQUAL(0, msg.b.getLength());

  // Length prefix valid but data truncated
  uint8_t buffer2[4] = {0, 0, 5, 1}; // len=5 but only 1 data byte present
  Reader r2(buffer2, sizeof(buffer2));
  Msg2 msg2{};
  msg2.io(r2);

  TEST_ASSERT_FALSE(r2.isOk());
}

void test_reader_bool_any_nonzero_is_true() {
  uint8_t buffer[1] = {2}; // any non-zero byte reads as true
  Reader r(buffer, sizeof(buffer));

  bool v = false;
  r(v);

  TEST_ASSERT_TRUE(r.isOk());
  TEST_ASSERT_TRUE(v);
}

/* ---------------------------------------------------------------------------------------------- */
/*                                       Helpers / dispatch                                       */
/* ---------------------------------------------------------------------------------------------- */

void test_serialize() {
  MsgPing msg{};
  msg.counter = 42;

  // Returns bytes written and produces the expected bytes
  uint8_t buffer[4]{};
  TEST_ASSERT_EQUAL(1, serialize(msg, buffer, sizeof(buffer)));
  TEST_ASSERT_EQUAL_UINT8(42, buffer[0]);

  // Buffer too small -> returns 0
  Msg1 big{};
  uint8_t small[8]{}; // too small for the 29-byte message
  TEST_ASSERT_EQUAL(0, serialize(big, small, sizeof(small)));
}

void test_deserialize() {
  const uint8_t buffer[1] = {77};

  MsgPing out{};
  TEST_ASSERT_TRUE(deserialize(out, buffer, sizeof(buffer)));
  TEST_ASSERT_EQUAL_UINT8(77, out.counter);

  // Truncated input -> rejected (MsgData needs 6 bytes)
  MsgData out_data{};
  TEST_ASSERT_FALSE(deserialize(out_data, buffer, sizeof(buffer)));
}

void test_serialize_with_header() {
  constexpr size_t packed_size = getMaxPackedSizeWithHeader<MsgData>();
  TEST_ASSERT_EQUAL(8, packed_size); // 1 (ID) + 1 (VERSION) + 2 (value) + 4 (ratio)

  MsgData in{};
  in.value = 0x1234;
  in.ratio = 0.5f;

  uint8_t buffer[packed_size]{};
  const size_t written = serializeWithHeader(in, buffer, sizeof(buffer));
  TEST_ASSERT_EQUAL(packed_size, written);

  FloatInt f_union;
  f_union.f = 0.5f;

  const uint8_t expected[8] = {
    MsgData::ID,
    MsgData::VERSION,
    0x34,                      // value (LSB)
    0x12,                      // value (MSB)
    uint8_t(f_union.u & 0xFF), // ratio (LSB)
    uint8_t((f_union.u >> 8) & 0xFF),
    uint8_t((f_union.u >> 16) & 0xFF),
    uint8_t((f_union.u >> 24) & 0xFF), // ratio (MSB)
  };
  for (size_t i = 0; i < written; i++) {
    TEST_ASSERT_EQUAL(expected[i], buffer[i]);
  }

  // Buffer too small for header + fields -> returns 0
  TEST_ASSERT_EQUAL(0, serializeWithHeader(in, buffer, 3));
}

void test_deserialize_with_header() {
  // [ID][VERSION][counter]
  uint8_t buffer[3] = {MsgPing::ID, MsgPing::VERSION, 42};

  MsgPing out{};
  TEST_ASSERT_TRUE(deserializeWithHeader(out, buffer, sizeof(buffer)));
  TEST_ASSERT_EQUAL_UINT8(42, out.counter);

  // Wrong ID -> rejected
  buffer[0] = 0xFF;
  TEST_ASSERT_FALSE(deserializeWithHeader(out, buffer, sizeof(buffer)));
  buffer[0] = MsgPing::ID;

  // Wrong VERSION -> rejected
  buffer[1] = MsgPing::VERSION + 1;
  TEST_ASSERT_FALSE(deserializeWithHeader(out, buffer, sizeof(buffer)));
  buffer[1] = MsgPing::VERSION;

  // Header alone (no fields) -> rejected
  TEST_ASSERT_FALSE(deserializeWithHeader(out, buffer, 2));

  // Too short for the header -> rejected
  TEST_ASSERT_FALSE(deserializeWithHeader(out, buffer, 1));
  TEST_ASSERT_FALSE(deserializeWithHeader(out, buffer, 0));
}

void test_peek_id_and_version() {
  const uint8_t buffer[3] = {MsgData::ID, MsgData::VERSION, 0};

  uint8_t id      = 0;
  uint8_t version = 0;
  TEST_ASSERT_TRUE(peekId(buffer, sizeof(buffer), id));
  TEST_ASSERT_EQUAL_UINT8(MsgData::ID, id);
  TEST_ASSERT_TRUE(peekVersion(buffer, sizeof(buffer), version));
  TEST_ASSERT_EQUAL_UINT8(MsgData::VERSION, version);

  // Not enough bytes
  TEST_ASSERT_FALSE(peekId(buffer, 0, id));
  TEST_ASSERT_FALSE(peekVersion(buffer, 1, version));
}

void test_dispatch() {
  FloatInt f_union;
  f_union.f = 0.5f;

  // [ID][VERSION][value LE][ratio LE]
  uint8_t buffer[8] = {
    MsgData::ID,
    MsgData::VERSION,
    0x09, // value = 777 (LSB)
    0x03, // value (MSB)
    uint8_t(f_union.u & 0xFF),
    uint8_t((f_union.u >> 8) & 0xFF),
    uint8_t((f_union.u >> 16) & 0xFF),
    uint8_t((f_union.u >> 24) & 0xFF),
  };

  bool ping_handled = false;
  bool data_handled = false;

  auto handler = Overloaded{
    [&](const MsgPing&) { ping_handled = true; },
    [&](const MsgData& m) { data_handled = (m.value == 777); },
  };

  // Matching ID and VERSION -> the right handler runs
  TEST_ASSERT_TRUE((dispatch<MsgPing, MsgData>(buffer, sizeof(buffer), handler)));
  TEST_ASSERT_FALSE(ping_handled);
  TEST_ASSERT_TRUE(data_handled);

  // Unknown ID -> no handler runs
  data_handled = false;
  buffer[0]    = 0xFF;
  TEST_ASSERT_FALSE((dispatch<MsgPing, MsgData>(buffer, sizeof(buffer), handler)));
  TEST_ASSERT_FALSE(ping_handled);
  TEST_ASSERT_FALSE(data_handled);
  buffer[0] = MsgData::ID;

  // Wrong VERSION -> no handler runs
  buffer[1] = MsgData::VERSION + 1;
  TEST_ASSERT_FALSE((dispatch<MsgPing, MsgData>(buffer, sizeof(buffer), handler)));
  buffer[1] = MsgData::VERSION;

  // Known header but truncated body -> rejected
  TEST_ASSERT_FALSE((dispatch<MsgPing, MsgData>(buffer, sizeof(buffer) - 1, handler)));

  // Empty / header-only input -> rejected
  TEST_ASSERT_FALSE((dispatch<MsgPing, MsgData>(buffer, 0, handler)));
  TEST_ASSERT_FALSE((dispatch<MsgPing, MsgData>(buffer, 2, handler)));
}

/* ---------------------------------------------------------------------------------------------- */
/*                                           Round-trip                                           */
/* ---------------------------------------------------------------------------------------------- */

void test_round_trip_common_types() {
  Msg1 in{};
  in.b   = true;
  in.u8  = 0xAA;
  in.i8  = -5;
  in.u16 = 0xAABB;
  in.i16 = -1234;
  in.u32 = 0xAABBCCDD;
  in.i32 = -123456;
  in.f   = 3.14f;
  in.d   = 2.71828;
  in.e   = Msg1::MyEnum::C;

  uint8_t buffer[getMaxPackedSize<Msg1>()]{};
  const size_t written = serialize(in, buffer, sizeof(buffer));
  TEST_ASSERT_EQUAL(sizeof(buffer), written);

  Msg1 out{};
  TEST_ASSERT_TRUE(deserialize(out, buffer, written));
  TEST_ASSERT_EQUAL(in.b, out.b);
  TEST_ASSERT_EQUAL_UINT8(in.u8, out.u8);
  TEST_ASSERT_EQUAL_INT8(in.i8, out.i8);
  TEST_ASSERT_EQUAL_UINT16(in.u16, out.u16);
  TEST_ASSERT_EQUAL_INT16(in.i16, out.i16);
  TEST_ASSERT_EQUAL_UINT32(in.u32, out.u32);
  TEST_ASSERT_EQUAL_INT32(in.i32, out.i32);
  TEST_ASSERT_EQUAL_FLOAT(in.f, out.f);
  TEST_ASSERT_EQUAL_DOUBLE(in.d, out.d);
  TEST_ASSERT_EQUAL(int(in.e), int(out.e));
}

void test_round_trip_custom_types() {
  Msg2 in{};
  in.q           = 12.34f;
  uint8_t src[3] = {0xDE, 0xAD, 0xBE};
  in.b.set(src, sizeof(src));

  uint8_t buffer[getMaxPackedSize<Msg2>()]{};
  const size_t written = serialize(in, buffer, sizeof(buffer));
  TEST_ASSERT_EQUAL(6, written); // 2 (quant) + 1 (len) + 3 (data)

  Msg2 out{};
  TEST_ASSERT_TRUE(deserialize(out, buffer, written));
  TEST_ASSERT_EQUAL_INT16(in.q.getRaw(), out.q.getRaw());
  TEST_ASSERT_EQUAL_UINT8(in.b.getLength(), out.b.getLength());
  for (size_t i = 0; i < in.b.getLength(); i++) {
    TEST_ASSERT_EQUAL(in.b.getData()[i], out.b.getData()[i]);
  }
}

void test_round_trip_array_and_nested() {
  MsgArray in_arr{};
  in_arr.values[0] = 0x1111;
  in_arr.values[1] = 0x2222;
  in_arr.values[2] = 0x3333;

  uint8_t buffer[16]{};
  size_t written = serialize(in_arr, buffer, sizeof(buffer));
  TEST_ASSERT_EQUAL(6, written);

  MsgArray out_arr{};
  TEST_ASSERT_TRUE(deserialize(out_arr, buffer, written));
  for (size_t i = 0; i < 3; i++) {
    TEST_ASSERT_EQUAL_UINT16(in_arr.values[i], out_arr.values[i]);
  }

  MsgNested in_nested{};
  in_nested.inner.counter = 7;
  in_nested.extra         = 9;

  written = serialize(in_nested, buffer, sizeof(buffer));
  TEST_ASSERT_EQUAL(2, written);

  MsgNested out_nested{};
  TEST_ASSERT_TRUE(deserialize(out_nested, buffer, written));
  TEST_ASSERT_EQUAL_UINT8(7, out_nested.inner.counter);
  TEST_ASSERT_EQUAL_UINT8(9, out_nested.extra);
}

void test_round_trip_with_header_and_dispatch() {
  MsgData in{};
  in.value = 777;
  in.ratio = 0.25f;

  uint8_t buffer[getMaxPackedSizeWithHeader<MsgData>()]{};
  const size_t written = serializeWithHeader(in, buffer, sizeof(buffer));
  TEST_ASSERT_EQUAL(sizeof(buffer), written);

  // Header inspection
  uint8_t id      = 0;
  uint8_t version = 0;
  TEST_ASSERT_TRUE(peekId(buffer, written, id));
  TEST_ASSERT_TRUE(peekVersion(buffer, written, version));
  TEST_ASSERT_EQUAL_UINT8(MsgData::ID, id);
  TEST_ASSERT_EQUAL_UINT8(MsgData::VERSION, version);

  // Direct deserialization
  MsgData out{};
  TEST_ASSERT_TRUE(deserializeWithHeader(out, buffer, written));
  TEST_ASSERT_EQUAL_UINT16(in.value, out.value);
  TEST_ASSERT_EQUAL_FLOAT(in.ratio, out.ratio);

  // Dispatch to the right handler
  bool handled = false;
  TEST_ASSERT_TRUE((dispatch<MsgPing, MsgData>(buffer,
    written,
    Overloaded{
      [&](const MsgPing&) {},
      [&](const MsgData& m) { handled = (m.value == 777); },
    })));
  TEST_ASSERT_TRUE(handled);
}

/* ---------------------------------------------------------------------------------------------- */
/*                                             Stress                                             */
/* ---------------------------------------------------------------------------------------------- */

constexpr uint32_t STRESS_ITERATIONS = 10000;

// Deterministic PRNG (xorshift32): failures are reproducible from the seed
static uint32_t stress_state = 1;

uint32_t nextRandom() {
  stress_state ^= stress_state << 13;
  stress_state ^= stress_state >> 17;
  stress_state ^= stress_state << 5;
  return stress_state;
}

void test_stress_garbage_input() {
  stress_state = 0xA5A5A5A5;

  uint8_t buffer[24];

  for (uint32_t i = 0; i < STRESS_ITERATIONS; i++) {
    const size_t len = nextRandom() % (sizeof(buffer) + 1); // 0..24
    for (size_t j = 0; j < len; j++) {
      buffer[j] = uint8_t(nextRandom());
    }

    // Random garbage may decode as a valid message; the property under test is "no crash and
    // invariants hold", not "rejects everything"
    Msg2 msg{};
    deserialize(msg, buffer, len);
    TEST_ASSERT_TRUE(msg.b.getLength() <= msg.b.getCapacity());

    dispatch<MsgPing, MsgData>(buffer,
      len,
      Overloaded{
        [](const MsgPing&) {},
        [](const MsgData&) {},
      });
  }
}

void test_stress_round_trip() {
  stress_state = 0xC0FFEE11;

  uint8_t buffer1[getMaxPackedSize<Msg1>()]{};
  uint8_t buffer2[getMaxPackedSize<Msg2>()]{};

  for (uint32_t i = 0; i < STRESS_ITERATIONS; i++) {
    // Msg1 with random values (floats get fully random bit patterns, NaNs included)
    Msg1 in1{};
    FloatInt f_in;
    DoubleInt d_in;
    f_in.u  = nextRandom();
    d_in.u  = (uint64_t(nextRandom()) << 32) | nextRandom();
    in1.b   = (nextRandom() & 1) != 0;
    in1.u8  = uint8_t(nextRandom());
    in1.i8  = int8_t(nextRandom());
    in1.u16 = uint16_t(nextRandom());
    in1.i16 = int16_t(nextRandom());
    in1.u32 = nextRandom();
    in1.i32 = int32_t(nextRandom());
    in1.f   = f_in.f;
    in1.d   = d_in.d;
    in1.e   = static_cast<Msg1::MyEnum>(1 + (nextRandom() % 3));

    TEST_ASSERT_EQUAL(sizeof(buffer1), serialize(in1, buffer1, sizeof(buffer1)));

    Msg1 out1{};
    TEST_ASSERT_TRUE(deserialize(out1, buffer1, sizeof(buffer1)));
    TEST_ASSERT_EQUAL(in1.b, out1.b);
    TEST_ASSERT_EQUAL_UINT8(in1.u8, out1.u8);
    TEST_ASSERT_EQUAL_INT8(in1.i8, out1.i8);
    TEST_ASSERT_EQUAL_UINT16(in1.u16, out1.u16);
    TEST_ASSERT_EQUAL_INT16(in1.i16, out1.i16);
    TEST_ASSERT_EQUAL_UINT32(in1.u32, out1.u32);
    TEST_ASSERT_EQUAL_INT32(in1.i32, out1.i32);
    TEST_ASSERT_EQUAL(int(in1.e), int(out1.e));

    // Floats compared bitwise: NaN != NaN, but serialization must be bit-exact
    FloatInt f_out;
    DoubleInt d_out;
    f_out.f = out1.f;
    d_out.d = out1.d;
    TEST_ASSERT_EQUAL_UINT32(f_in.u, f_out.u);
    TEST_ASSERT_TRUE(d_in.u == d_out.u);

    // Msg2 with random raw quant and random payload
    Msg2 in2{};
    in2.q.setRaw(int16_t(nextRandom()));

    uint8_t payload[5];
    const size_t payload_len = nextRandom() % (sizeof(payload) + 1); // 0..5
    for (size_t j = 0; j < payload_len; j++) {
      payload[j] = uint8_t(nextRandom());
    }
    TEST_ASSERT_TRUE(in2.b.set(payload, payload_len));

    const size_t written = serialize(in2, buffer2, sizeof(buffer2));
    TEST_ASSERT_EQUAL(3 + payload_len, written); // 2 (quant) + 1 (len) + payload

    Msg2 out2{};
    TEST_ASSERT_TRUE(deserialize(out2, buffer2, written));
    TEST_ASSERT_EQUAL_INT16(in2.q.getRaw(), out2.q.getRaw());
    TEST_ASSERT_EQUAL_UINT8(payload_len, out2.b.getLength());
    for (size_t j = 0; j < payload_len; j++) {
      TEST_ASSERT_EQUAL(payload[j], out2.b.getData()[j]);
    }
  }
}

void test_stress_truncate_and_corrupt() {
  stress_state = 0xDEADBEEF;

  uint8_t buffer[getMaxPackedSize<Msg2>()]{};

  for (uint32_t i = 0; i < STRESS_ITERATIONS; i++) {
    // Build a valid serialized message
    Msg2 in{};
    in.q.setRaw(int16_t(nextRandom()));

    uint8_t payload[5];
    const size_t payload_len = nextRandom() % (sizeof(payload) + 1);
    for (size_t j = 0; j < payload_len; j++) {
      payload[j] = uint8_t(nextRandom());
    }
    in.b.set(payload, payload_len);

    const size_t written = serialize(in, buffer, sizeof(buffer));
    TEST_ASSERT_TRUE(written > 0);

    // Any strict prefix must be rejected (the message consumes all its bytes)
    const size_t truncated_len = nextRandom() % written;
    Msg2 out{};
    TEST_ASSERT_FALSE(deserialize(out, buffer, truncated_len));

    // Flip at least one bit of a random byte: must fail or keep invariants
    const size_t flip_pos = nextRandom() % written;
    buffer[flip_pos] ^= uint8_t(nextRandom() | 1);

    Msg2 corrupted{};
    deserialize(corrupted, buffer, written);
    TEST_ASSERT_TRUE(corrupted.b.getLength() <= corrupted.b.getCapacity());
  }
}

/* ---------------------------------------------------------------------------------------------- */
/*                                          setup / loop                                          */
/* ---------------------------------------------------------------------------------------------- */

void setup() {
  Serial.begin(115200);
  delay(2000);

  UNITY_BEGIN();

  // Custom types
  RUN_TEST(test_type_quant_int16_t);
  RUN_TEST(test_type_quant_uint8_t);
  RUN_TEST(test_type_quant_saturation);
  RUN_TEST(test_type_bytes);

  // Writer
  RUN_TEST(test_writer_initial_state);
  RUN_TEST(test_writer_common_types);
  RUN_TEST(test_writer_custom_types);
  RUN_TEST(test_writer_overflow);

  // Reader
  RUN_TEST(test_reader_initial_state);
  RUN_TEST(test_reader_common_types);
  RUN_TEST(test_reader_custom_types);
  RUN_TEST(test_reader_truncated_input);
  RUN_TEST(test_reader_trailing_bytes_ignored);
  RUN_TEST(test_reader_bytes_invalid_len);
  RUN_TEST(test_reader_bool_any_nonzero_is_true);

  // Helpers / dispatch
  RUN_TEST(test_serialize);
  RUN_TEST(test_deserialize);
  RUN_TEST(test_serialize_with_header);
  RUN_TEST(test_deserialize_with_header);
  RUN_TEST(test_peek_id_and_version);
  RUN_TEST(test_dispatch);

  // Round-trip
  RUN_TEST(test_round_trip_common_types);
  RUN_TEST(test_round_trip_custom_types);
  RUN_TEST(test_round_trip_array_and_nested);
  RUN_TEST(test_round_trip_with_header_and_dispatch);

  // Stress
  RUN_TEST(test_stress_garbage_input);
  RUN_TEST(test_stress_round_trip);
  RUN_TEST(test_stress_truncate_and_corrupt);

  UNITY_END();
}

void loop() {}