/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <limits>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <type_traits>

// Enable concepts-based API if supported by the compiler and not explicitly disabled.
#if defined(__cpp_concepts) && __cpp_concepts >= 201907L && !defined(BYTEPACK_DISABLE_CONCEPTS)
#  define BYTEPACK_CONCEPTS_ENABLED 1
#else
#  define BYTEPACK_CONCEPTS_ENABLED 0
#endif

// Error messages for static_asserts.
#define BYTEPACK_UNSUPPORTED_FIELD_MSG                                        \
  "BytePack: unsupported field type in io(). Supported: bool, integral "      \
  "types, enums, float, "                                                     \
  "double, Quant<>, Bytes<>, fixed C arrays of these, and structs providing " \
  "io()."

#define BYTEPACK_MISSING_ID_MSG                                                             \
  "BytePack: Msg must define 'static constexpr uint8_t ID' to use the WithHeader/dispatch " \
  "helpers."

#define BYTEPACK_MISSING_VERSION_MSG                                                             \
  "BytePack: Msg must define 'static constexpr uint8_t VERSION' to use the WithHeader/dispatch " \
  "helpers."

#define BYTEPACK_MISSING_IO_MSG \
  "BytePack: Msg must provide a 'template <typename Archive> void io(Archive&)' member function."

#define BYTEPACK_BAD_HANDLER_MSG \
  "BytePack: dispatch handler must be invocable with 'Msg&' for every listed message type."

namespace BytePack {

// Private implementation details (not part of the public API).
namespace detail {

// MARK: Detection traits (shared by SFINAE and Concepts)

// Generic probe used to detect io() without depending on a concrete archive
struct IoProbe {
  template <typename... Ts>
  constexpr void operator()(const Ts&...) const {}
};

template <typename T, typename = void>
struct HasIo : std::false_type {};
template <typename T>
struct HasIo<T, std::void_t<decltype(std::declval<T&>().io(std::declval<IoProbe&>()))>>
    : std::true_type {};

template <typename T>
constexpr bool has_io_v = HasIo<T>::value;

template <typename T>
constexpr bool is_packed_integer_v = std::is_integral_v<T> && !std::is_same_v<T, bool>;

template <typename T>
constexpr bool is_packed_length_v = std::is_unsigned_v<T> && !std::is_same_v<T, bool>;

// Detects the message-header convention: `static constexpr uint8_t ID` / `VERSION`
template <typename T, typename = void>
struct HasByteId : std::false_type {};
template <typename T>
struct HasByteId<T, std::void_t<decltype(T::ID)>>
    : std::is_same<std::remove_cv_t<decltype(T::ID)>, uint8_t> {};

template <typename T>
constexpr bool has_byte_id_v = HasByteId<T>::value;

template <typename T, typename = void>
struct HasByteVersion : std::false_type {};
template <typename T>
struct HasByteVersion<T, std::void_t<decltype(T::VERSION)>>
    : std::is_same<std::remove_cv_t<decltype(T::VERSION)>, uint8_t> {};

template <typename T>
constexpr bool has_byte_version_v = HasByteVersion<T>::value;

} // namespace detail

// MARK: Concepts

#if BYTEPACK_CONCEPTS_ENABLED
// Concepts for public API. Use them if you want to write generic code.
namespace Concepts {

/**
 * @brief Concept for integer type usable for packing (any integral except bool).
 * @tparam T The type to check.
 */
template <typename T>
concept PackedInteger = detail::is_packed_integer_v<T>;

/**
 * @brief Concept for unsigned integer usable as a length prefix.
 * @tparam T The type to check.
 */
template <typename T>
concept PackedLength = detail::is_packed_length_v<T>;

/**
 * @brief Concept for enum/enum-class type (serialized as its underlying type).
 * @tparam T The type to check.
 */
template <typename T>
concept PackedEnum = std::is_enum_v<T>;

/**
 * @brief Concept for a message/struct providing the io() member function.
 * @tparam T The type to check.
 */
template <typename T>
concept Serializable = detail::has_io_v<T>;

/**
 * @brief Concept for a Serializable message that also declares the wire-header convention: `static
 * constexpr uint8_t ID` and `static constexpr uint8_t VERSION`.
 * @tparam T The type to check.
 */
template <typename T>
concept SerializableWithHeader =
  Serializable<T> && detail::has_byte_id_v<T> && detail::has_byte_version_v<T>;

} // namespace Concepts
#endif

// MARK: Custom types

#if BYTEPACK_CONCEPTS_ENABLED && !defined(__INTELLISENSE__)
template <Concepts::PackedInteger T, int32_t Scale>
  requires (Scale > 0)
struct Quant {
#else
/**
 * @brief A wrapper for quantized physical values, stored as integers with a fixed scale factor.
 *
 * Provides convenient float-to-integer quantization with saturation, and introspection of the scale
 * factor and representable range. Useful for efficiently packing physical values like voltages,
 * angles, etc.
 *
 * Example:
 * ```cpp
 * Quant<uint16_t, 100> q; // represents a value in [0.00, 655.35] with 0.01 resolution
 * q = 3.14f;              // quantizes to 314
 * float v = q;            // dequantizes back to 3.14
 * ```
 *
 * @tparam T Storage type (any non-bool integer type).
 * @tparam Scale Positive integer scale factor (physical value = quantized value / Scale).
 */
template <typename T, int32_t Scale>
struct Quant {
#endif
  static_assert(detail::is_packed_integer_v<T>, "Quant: storage type must be a non-bool integer.");
  static_assert(Scale > 0, "Quant: Scale must be > 0.");

  /** @brief Zero-initialized (raw value 0, physical value 0.0). */
  constexpr Quant() = default;

  /**
   * @brief Construct from a physical value: quantizes and saturates immediately.
   * @param value Physical value to quantize.
   */
  explicit constexpr Quant(float value)
      : _raw(quantize(value)) {}

  /** @brief Read the physical value (dequantizes). */
  constexpr operator float() const { return dequantize(_raw); }

  /** @brief Read the physical value (dequantizes). Explicit alternative to operator `float()`. */
  constexpr float getFloat() const { return dequantize(_raw); }

  /**
   * @brief Read the stored quantized integer.
   * @return `constexpr T` Value of the quantized integer (no scaling applied).
   */
  constexpr T getRaw() const { return _raw; }

  /** @brief Sets the stored quantized integer directly (no scaling applied). */

  /**
   * @brief Set the stored quantized integer directly, without scaling. Use with care: this does not
   * perform any validation.
   * @param raw New raw quantized value to store.
   */
  constexpr void setRaw(const T raw) { _raw = raw; }

  /** @brief Assigns a physical value: quantizes and saturates HERE, not at serialization. */

  /**
   * @brief Assing a physical value: quantizes and saturates inmediately.
   * @param value Physical value to quantize and store.
   * @return `constexpr Quant&` Reference to this Quant.
   */
  constexpr Quant& operator=(const float value) {
    _raw = quantize(value);
    return *this;
  }

  /**
   * @brief Get the smallest representable physical step, which is `1 / Scale`.
   * @return `constexpr float` Smallest representable physical step.
   */
  static constexpr float getResolution() { return 1.0f / float(Scale); }

  /** @brief Lowest representable physical value. */

  /**
   * @brief Get the lowest representable physical value, which is `min(T) / Scale`.
   * @return `constexpr float` Lowest representable physical value.
   */
  static constexpr float getMinValue() {
    return float(std::numeric_limits<T>::min()) / float(Scale);
  }

  /**
   * @brief Get the highest representable physical value, which is `max(T) / Scale`.
   * @return `constexpr float` Highest representable physical value.
   */
  static constexpr float getMaxValue() {
    return float(std::numeric_limits<T>::max()) / float(Scale);
  }

  /** @brief True if v survives quantization without saturating (clipping detector). */

  /**
   * @brief Check if a physical value would survive quantization without saturating (useful as a
   * clipping detector before assignment). NaN is considered out of range.
   * @param value Physical value to check.
   * @return `true` if value would survive quantization without saturating, `false` otherwise.
   */
  static constexpr bool fits(const float value) {
    const float s = value * float(Scale);
    return s == s && s > float(std::numeric_limits<T>::min()) - 0.5f &&
           s < float(std::numeric_limits<T>::max()) + 0.5f;
  }

  /**
   * @brief Convert a physical value to its quantized integer: saturates at the range bounds, maps
   * NaN to 0 and rounds half away from zero.
   * @param value Physical value to quantize.
   * @return `constexpr T` Quantized integer value.
   */
  static constexpr T quantize(const float value) {
    const float scaled = value * float(Scale);
    if (scaled != scaled) return 0; // NaN -> 0
    if (scaled <= float(std::numeric_limits<T>::min())) return std::numeric_limits<T>::min();
    if (scaled >= float(std::numeric_limits<T>::max())) return std::numeric_limits<T>::max();
    return (scaled >= 0.0f) ? T(scaled + 0.5f) : T(scaled - 0.5f); // half away from zero
  }

  /**
   * @brief Convert a quantized integer back to its physical value.
   * @param raw Quantized integer to dequantize.
   * @return `constexpr float` Physical value corresponding to the quantized integer.
   */
  static constexpr float dequantize(const T raw) { return float(raw) / float(Scale); }

  private:
  T _raw = 0; // quantized value stored as an integer
};

#if BYTEPACK_CONCEPTS_ENABLED && !defined(__INTELLISENSE__)
template <Concepts::PackedLength LenT, size_t MaxLen>
  requires (MaxLen <= size_t(std::numeric_limits<LenT>::max()))
struct Bytes {
#else
/**
 * @brief A length-prefixed byte payload with a fixed maximum capacity.
 *
 * Pack format: `[length: sizeof(LenT) bytes, little-endian][data: length bytes]`. Only the used
 * part travels; size budgets (`getMaxPackedSize()`) count it at full capacity.
 *
 * Example:
 * ```cpp
 * Bytes<uint8_t, 16> b; // up to 16 bytes, 1-byte length prefix
 * b.set("hi", 2);       // payload is now 2 bytes: ['h', 'i'], length prefix is 2
 * ```
 *
 * @tparam LenT Unsigned integer type of the length prefix.
 * @tparam MaxLen Maximum payload size in bytes (must fit in LenT).
 */
template <typename LenT, size_t MaxLen>
struct Bytes {
#endif
  static_assert(detail::is_packed_length_v<LenT>, "Bytes: LenT must be unsigned.");
  static_assert(MaxLen <= size_t(std::numeric_limits<LenT>::max()),
    "Bytes: MaxLen does not fit in LenT.");

  /**
   * @brief Copy `n` bytes from `src` into the payload, setting the length prefix to `n`.
   * @param src Source bytes to copy into the payload.
   * @param n Number of bytes to copy from `src` into the payload.
   * @return `true` if `n` does not exceed the capacity and the payload was updated, `false`
   * otherwise (payload unchanged).
   */
  bool set(const void* src, const size_t n) {
    if (n > MaxLen) return false;
    memcpy(_data, src, n);
    _len = LenT(n);
    return true;
  }

  /** @brief Read-only pointer to the payload. */

  /**
   * @brief Get a read-only pointer to the payload data.
   * @return `const uint8_t*` Pointer to the payload data (not null-terminated). Valid up to
   * `getLength()` bytes.
   */
  const uint8_t* getData() const { return _data; }

  /** @brief Current payload length in bytes. */

  /**
   * @brief Get the current payload length in bytes (the value of the length prefix). Valid up to
   * `getCapacity()`.
   * @return `LenT` Current payload length in bytes.
   */
  LenT getLength() const { return _len; }

  /**
   * @brief Get the maximum payload length in bytes (capacity), which is the template parameter
   * `MaxLen`.
   * @return `size_t` Maximum payload length in bytes.
   */
  static constexpr size_t getCapacity() { return MaxLen; }

  /** @brief Empties the payload (length 0; the data bytes are not wiped). */

  /**
   * @brief Empty the payload (length 0; the data bytes are not wiped).
   */
  void clear() { _len = 0; }

  /**
   * @brief Get a writable pointer for in-place filling: write up to `getCapacity()` bytes, then
   * commit with `setLength()`. Bypasses validation; staying in bounds is the caller's
   * responsibility.
   * @return `uint8_t*` Writable pointer to the payload data (not null-terminated). Valid up to
   * `getCapacity()` bytes.
   */
  uint8_t* getBuffer() { return _data; }

  /**
   * @brief Commit the payload length after filling through getBuffer().
   * @param n New length in bytes.
   * @return `true` if `n` does not exceed the capacity and the length was updated, `false`
   * otherwise (length unchanged).
   */
  bool setLength(const size_t n) {
    if (n > MaxLen) return false;
    _len = LenT(n);
    return true;
  }

  private:
  friend class Reader; // deserializes straight into _len/_data
  LenT _len             = 0;
  uint8_t _data[MaxLen] = {};
};

namespace detail {

// MARK: Supported-field traits

template <typename T>
struct IsQuant : std::false_type {};
template <typename I, int32_t S>
struct IsQuant<Quant<I, S>> : std::true_type {};

template <typename T>
constexpr bool is_quant_v = IsQuant<T>::value;

template <typename T>
struct IsBytes : std::false_type {};
template <typename L, size_t N>
struct IsBytes<Bytes<L, N>> : std::true_type {};

template <typename T>
constexpr bool is_bytes_v = IsBytes<T>::value;

template <typename T>
struct IsSupportedField {
  static constexpr bool value = std::is_integral_v<T> || std::is_enum_v<T> ||
                                std::is_same_v<T, float> || std::is_same_v<T, double> ||
                                is_quant_v<T> || is_bytes_v<T> || has_io_v<T>;
};
template <typename T, size_t N>
struct IsSupportedField<T[N]> : IsSupportedField<T> {};

template <typename T>
constexpr bool is_supported_field_v = IsSupportedField<T>::value;

} // namespace detail

// MARK: Writer

/**
 * @brief Serializer that writes supported field values into a caller-provided buffer
 * (little-endian), validating bounds.
 *
 * Errors are sticky: the first value that does not fit puts the writer in a failed state, no
 * partial bytes of it are written and `isOk()` returns `false` until `reset()`. Usually driven
 * through the `serialize()` helpers; use it directly to pack loose values without defining a
 * struct.
 *
 * Example:
 * ```cpp
 * uint8_t buffer[16]{};
 * Writer w(buffer, sizeof(buffer));
 * w(true, uint8_t(0xAA), int16_t(-123), Quant<uint16_t, 100>(3.14f));
 * ```
 *
 * @note It is recommended to use `serialize()` instead of calling `Writer` directly, as it provides
 * better ergonomics and safety (e.g. compile-time size checks when the message struct is used).
 */
class Writer {
  public:
  /**
   * @brief Construct a Writer that wraps a destination buffer (not owned by the Writer).
   * @param buffer Destination bytes.
   * @param size Capacity of buffer in bytes.
   */
  Writer(uint8_t* buffer, size_t size)
      : _buf(buffer)
      , _size(size) {}

  /**
   * @brief Serialize each argument in order. Returns *this for chaining.
   * @tparam Ts Parameter pack of argument types.
   * @param args Arguments to serialize.
   * @return `Writer&` Reference to this Writer (for chaining).
   */
  template <typename... Ts>
  Writer& operator()(const Ts&... args) {
    (write(args), ...);
    return *this;
  }

  /**
   * @brief Get the number of bytes written so far. Valid only if `isOk()` is `true`.
   * @return `size_t` Number of bytes written so far.
   */
  size_t getSize() const { return _pos; }

  /**
   * @brief Get the capacity of the wrapped buffer in bytes (the `size` parameter passed to the
   * constructor).
   * @return `size_t` Capacity of the wrapped buffer in bytes.
   */
  size_t getCapacity() const { return _size; }

  /**
   * @brief Check if all values serialized so far have fit in the buffer.
   * @return `true` if all values serialized so far have fit in the buffer, `false` if any value
   * failed to fit (sticky until `reset()`).
   */
  bool isOk() const { return !_error; }

  /**
   * @brief Rewind to the start and clear the error. Buffer contents are not erased.
   */
  void reset() {
    _pos   = 0;
    _error = false;
  }

  private:
  uint8_t* _buf;
  size_t _size;
  size_t _pos = 0;
  bool _error = false;

  void putBytesLE(const uint64_t value, const size_t n) {
    if (_error || _pos + n > _size) {
      _error = true;
      return;
    }

    for (size_t i = 0; i < n; ++i) {
      _buf[_pos++] = (value >> (8 * i)) & 0xFF;
    }
  }

  // Bools are stored as a single byte with value 0 or 1
  void write(bool v) { putBytesLE(v ? 1u : 0u, 1); }

#if BYTEPACK_CONCEPTS_ENABLED
  template <Concepts::PackedInteger T>
#else
  template <typename T, std::enable_if_t<detail::is_packed_integer_v<T>, int> = 0>
#endif
  void write(const T v) {
    using U = std::make_unsigned_t<T>;
    putBytesLE(static_cast<uint64_t>(static_cast<U>(v)), sizeof(T));
  }

#if BYTEPACK_CONCEPTS_ENABLED
  template <Concepts::PackedEnum T>
#else
  template <typename T, std::enable_if_t<std::is_enum<T>::value, int> = 0>
#endif
  void write(const T v) {
    write(static_cast<std::underlying_type_t<T>>(v));
  };

  void write(const float v) {
    uint32_t as_int;
    memcpy(&as_int, &v, 4);
    putBytesLE(as_int, 4);
  }

  void write(const double v) {
    uint64_t as_int;
    memcpy(&as_int, &v, 8);
    putBytesLE(as_int, 8);
  }

  template <typename T, int32_t Scale>
  void write(const Quant<T, Scale>& q) {
    write(q.getRaw());
  }

  template <typename LenT, size_t MaxLen>
  void write(const Bytes<LenT, MaxLen>& b) {
    const LenT len = b.getLength();
    write(len);
    if (len > 0) {
      if (_error || _pos + len > _size) {
        _error = true;
        return;
      }
      memcpy(_buf + _pos, b.getData(), len);
      _pos += len;
    }
  }

  template <typename T, size_t N>
  void write(const T (&arr)[N]) {
    for (size_t i = 0; i < N; ++i) {
      write(arr[i]);
    }
  }

#if BYTEPACK_CONCEPTS_ENABLED
  template <Concepts::Serializable T>
#else
  template <typename T, std::enable_if_t<detail::has_io_v<T>, int> = 0>
#endif
  // Safe: the write path only reads from v. io() is non-const because the same method is reused by
  // Reader, which does mutate. Hence the const_cast.
  void write(const T& v) {
    const_cast<T&>(v).io(*this);
  }

  // Anything else: readable error instead of an overload-resolution dump
  template <typename T, std::enable_if_t<!detail::is_supported_field_v<T>, int> = 0>
  void write(const T&) {
    static_assert(detail::is_supported_field_v<T>, BYTEPACK_UNSUPPORTED_FIELD_MSG);
  }
};

// MARK: Reader

/**
 * @brief Deserializes field values from a byte buffer (little-endian), validating bounds.
 *
 * Errors are sticky: the first truncated/invalid read puts the reader in a failed state and the
 * remaining reads leave their targets untouched. Usually driven through the deserialize()
 * helpers; use it directly to unpack loose values without defining a struct.
 */

/**
 * @brief Deserializer that reads supported field values from a byte buffer (little-endian),
 * validating bounds.
 *
 * Errors are sticky: the first truncated/invalid read puts the reader in a failed state and the
 * remaining reads leave their targets untouched. Usually driven through the deserialize() helpers;
 * use it directly to unpack loose values without defining a struct.
 *
 * Example:
 * ```cpp
 * uint8_t buffer[] = {0x01, 0xAA, 0x22, 0x11}; // bool=true, uint8_t=0xAA, int16_t=0x1122
 * Reader r(buffer, sizeof(buffer));
 * bool b;
 * uint8_t u8;
 * int16_t i16;
 * r(b, u8, i16);
 * ```
 *
 * @note It is recommended to use `deserialize()` instead of calling `Reader` directly, as it
 * provides better ergonomics and safety (e.g. compile-time size checks when the message struct is
 * used).
 */
class Reader {
  public:
  /**
   * @brief Construct a Reader that wraps a source buffer (not owned by the Reader).
   * @param buffer Source bytes.
   * @param size Number of bytes available.
   */
  Reader(const uint8_t* buffer, size_t size)
      : _buf(buffer)
      , _size(size) {}

  /**
   * @brief Deserialize into each argument in order. Returns *this for chaining.
   * @tparam Ts Parameter pack of argument types.
   * @param args References to variables where the deserialized values will be stored.
   * @return `Reader&` Reference to this Reader (for chaining).
   */
  template <typename... Ts>
  Reader& operator()(Ts&... args) {
    (read(args), ...);
    return *this;
  }

  /**
   * @brief Check if all values deserialized so far have been valid and fit in the buffer.
   * @return `true` if all values deserialized so far have been valid and fit in the buffer, `false`
   * if any value was truncated/invalid (sticky until `reset()`).
   */
  bool isOk() const { return !_error; }

  /**
   * @brief Get the number of bytes consumed so far. Valid only if `isOk()` is `true`.
   * @return `size_t` Number of bytes consumed so far.
   */
  size_t getConsumedSize() const { return _pos; }

  /**
   * @brief Get the number of bytes remaining in the buffer (capacity - consumed). Valid only if
   * `isOk()` is `true`.
   * @return `size_t` Number of bytes remaining in the buffer.
   */
  size_t getRemainingSize() const { return _size - _pos; }

  private:
  const uint8_t* _buf;
  size_t _size;
  size_t _pos = 0;
  bool _error = false;

  uint64_t getBytesLE(const size_t n) {
    if (_error || _pos + n > _size) {
      _error = true;
      return 0;
    }

    uint64_t value = 0;
    for (size_t i = 0; i < n; ++i) {
      value |= uint64_t(_buf[_pos++]) << (8 * i);
    }
    return value;
  };

  void read(bool& v) { v = getBytesLE(1) != 0; }

#if BYTEPACK_CONCEPTS_ENABLED
  template <Concepts::PackedInteger T>
#else
  template <typename T, std::enable_if_t<detail::is_packed_integer_v<T>, int> = 0>
#endif
  void read(T& v) {
    using U     = std::make_unsigned_t<T>;
    const U raw = U(getBytesLE(sizeof(T)));
    memcpy(&v, &raw, sizeof(T)); // well-defined signed round-trip
  }

#if BYTEPACK_CONCEPTS_ENABLED
  template <Concepts::PackedEnum T>
#else
  template <typename T, std::enable_if_t<std::is_enum<T>::value, int> = 0>
#endif
  void read(T& v) {
    std::underlying_type_t<T> raw{};
    read(raw);
    v = static_cast<T>(raw);
  }

  void read(float& v) {
    uint32_t as_int = uint32_t(getBytesLE(4));
    memcpy(&v, &as_int, 4);
  }

  void read(double& v) {
    uint64_t as_int = uint64_t(getBytesLE(8));
    memcpy(&v, &as_int, 8);
  }

  template <typename T, int32_t Scale>
  void read(Quant<T, Scale>& q) {
    T raw;
    read(raw);
    q.setRaw(raw);
  }

  template <typename LenT, size_t MaxLen>
  void read(Bytes<LenT, MaxLen>& b) {
    read(b._len);
    if (_error) return;
    if (static_cast<size_t>(b._len) > MaxLen || _pos + b._len > _size) {
      _error = true;
      b._len = 0;
      return;
    }
    memcpy(b._data, _buf + _pos, b._len);
    _pos += b._len;
  }

  template <typename T, size_t N>
  void read(T (&arr)[N]) {
    for (size_t i = 0; i < N; ++i) {
      read(arr[i]);
    }
  };

#if BYTEPACK_CONCEPTS_ENABLED
  template <Concepts::Serializable T>
#else
  template <typename T, std::enable_if_t<detail::has_io_v<T>, int> = 0>
#endif
  void read(T& v) {
    v.io(*this);
  }

  // Anything else: readable error instead of an overload-resolution dump
  template <typename T, std::enable_if_t<!detail::is_supported_field_v<T>, int> = 0>
  void read(T&) {
    static_assert(detail::is_supported_field_v<T>, BYTEPACK_UNSUPPORTED_FIELD_MSG);
  }
};

// MARK: SizeCounter

/**
 * @brief Archive that measures worst-case serialized size instead of producing bytes.
 *
 * Fully constexpr: it is what drives `getMaxPackedSize()` and `getMaxPackedSizeWithHeader()` at
 * compile time. `Bytes<>` fields are counted at full capacity (length prefix + MaxLen).
 *
 * Example:
 * ```cpp
 * constexpr size_t size = SizeCounter()(true, uint8_t(0xAA), int16_t(-123)).getSize();
 * // size is 1 (bool) + 1 (uint8_t) + 2 (int16_t) = 4 bytes
 * ```
 *
 * @note It is recommended to use `getMaxPackedSize()` and `getMaxPackedSizeWithHeader()` instead of
 * calling `SizeCounter` directly, as it provides better ergonomics and safety (e.g. compile-time
 * checks that the message struct is used, and clearer intent).
 */
class SizeCounter {
  public:
  /**
   * @brief Default constructor. Initializes the counter to zero.
   */
  constexpr SizeCounter() = default;

  /**
   * @brief Adds the worst-case size of each argument to the counter. Returns *this for chaining.
   * @tparam Ts Parameter pack of argument types.
   * @param vals Arguments whose worst-case size will be added to the counter.
   * @return `constexpr SizeCounter&` Reference to this SizeCounter (for chaining).
   */
  template <typename... Ts>
  constexpr SizeCounter& operator()(const Ts&... vals) {
    (count(vals), ...);
    return *this;
  }

  /**
   * @brief Get the total size counted so far in bytes.
   * @return `constexpr size_t` Total size in bytes.
   */
  constexpr size_t getSize() const { return _size; }

  private:
  size_t _size = 0;

  constexpr void count(bool) { _size += 1; }

#if BYTEPACK_CONCEPTS_ENABLED
  template <Concepts::PackedInteger T>
#else
  template <typename T, std::enable_if_t<detail::is_packed_integer_v<T>, int> = 0>
#endif
  constexpr void count(T) {
    _size += sizeof(T);
  }

#if BYTEPACK_CONCEPTS_ENABLED
  template <Concepts::PackedEnum T>
#else
  template <typename T, std::enable_if_t<std::is_enum<T>::value, int> = 0>
#endif
  constexpr void count(T) {
    _size += sizeof(std::underlying_type_t<T>);
  }

  constexpr void count(float) { _size += 4; }
  constexpr void count(double) { _size += 8; }

  template <typename I, int32_t S>
  constexpr void count(const Quant<I, S>&) {
    _size += sizeof(I);
  }

  template <typename L, size_t N>
  constexpr void count(const Bytes<L, N>&) {
    _size += sizeof(L) + N; // worst case
  }

  template <typename T, size_t N>
  constexpr void count(const T (&arr)[N]) {
    for (size_t i = 0; i < N; i++)
      count(arr[i]);
  }

#if BYTEPACK_CONCEPTS_ENABLED
  template <Concepts::Serializable T>
#else
  template <typename T, std::enable_if_t<detail::has_io_v<T>, int> = 0>
#endif
  constexpr void count(const T& v) {
    const_cast<T&>(v).io(*this);
  }

  // Anything else: readable error instead of an overload-resolution dump
  template <typename T, std::enable_if_t<!detail::is_supported_field_v<T>, int> = 0>
  constexpr void count(const T&) {
    static_assert(detail::is_supported_field_v<T>, BYTEPACK_UNSUPPORTED_FIELD_MSG);
  }
};

// MARK: Helpers

#if BYTEPACK_CONCEPTS_ENABLED && !defined(__INTELLISENSE__)
template <Concepts::Serializable Msg>
constexpr size_t getMaxPackedSize() {
#else
/**
 * @brief Get maximum serialized size of `Msg` fields, computed at compile time.
 *
 * Exact for messages without `Bytes<>` fields; `Bytes<>` are counted at full capacity. Usable in
 * `static_assert` if `Msg::io()` is declared `constexpr`. Pairs with `serialize()`.
 *
 * @tparam Msg Message type providing `io()`.
 * @return Worst-case serialized size in bytes.
 */
template <typename Msg>
constexpr size_t getMaxPackedSize() {
#endif
  static_assert(detail::has_io_v<Msg>, BYTEPACK_MISSING_IO_MSG);
  Msg m{};
  SizeCounter c;
  m.io(c);
  return c.getSize();
}

#if BYTEPACK_CONCEPTS_ENABLED && !defined(__INTELLISENSE__)
template <Concepts::SerializableWithHeader Msg>
constexpr size_t getMaxPackedSizeWithHeader() {
#else
/**
 * @brief Get maximum serialized size of `[Msg::ID][Msg::VERSION][fields]`, computed at compile
 * time.
 *
 * Pairs with `serializeWithHeader()`, as `getMaxPackedSize()` pairs with `serialize()`.
 *
 * @tparam Msg Message type providing `io()`, `ID` and `VERSION`.
 * @return `size_t` Worst-case frame size in bytes (header included).
 */
template <typename Msg>
constexpr size_t getMaxPackedSizeWithHeader() {
#endif
  static_assert(detail::has_byte_id_v<Msg>, BYTEPACK_MISSING_ID_MSG);
  static_assert(detail::has_byte_version_v<Msg>, BYTEPACK_MISSING_VERSION_MSG);
  return 2 + getMaxPackedSize<Msg>();
}

#if BYTEPACK_CONCEPTS_ENABLED && !defined(__INTELLISENSE__)
template <Concepts::Serializable Msg>
inline size_t serialize(const Msg& msg, uint8_t* buffer, size_t buffer_size) {
#else
/**
 * @brief Serialize msg's fields into buffer (little-endian, no header).
 *
 * @tparam Msg Message type providing `io()`.
 * @param msg Message to serialize.
 * @param buffer Destination buffer.
 * @param buffer_size Capacity of buffer in bytes.
 * @return `size_t` Bytes written, or 0 if the message did not fit.
 */
template <typename Msg>
inline size_t serialize(const Msg& msg, uint8_t* buffer, size_t buffer_size) {
#endif
  static_assert(detail::has_io_v<Msg>, BYTEPACK_MISSING_IO_MSG);
  Writer w(buffer, buffer_size);
  w(msg);
  return w.isOk() ? w.getSize() : 0;
}

#if BYTEPACK_CONCEPTS_ENABLED && !defined(__INTELLISENSE__)
template <Concepts::Serializable Msg>
inline bool deserialize(Msg& msg, const uint8_t* data, size_t data_len) {
#else
/**
 * @brief Deserialize msg's fields from data.
 *
 * @tparam Msg Message type providing `io()`.
 * @param msg Message to fill.
 * @param data Source bytes.
 * @param data_len Number of bytes available.
 * @return `true` on success, `false` on truncated/invalid input.
 */
template <typename Msg>
inline bool deserialize(Msg& msg, const uint8_t* data, size_t data_len) {
#endif
  static_assert(detail::has_io_v<Msg>, BYTEPACK_MISSING_IO_MSG);
  Reader r(data, data_len);
  msg.io(r);
  return r.isOk();
}

#if BYTEPACK_CONCEPTS_ENABLED && !defined(__INTELLISENSE__)
template <Concepts::SerializableWithHeader Msg>

inline size_t serializeWithHeader(const Msg& msg, uint8_t* buffer, size_t buffer_size) {
#else
/**
 * @brief Serialize a `[Msg::ID][Msg::VERSION][fields]` frame into buffer.
 *
 * Requires (enforced at compile time) `static constexpr uint8_t ID` and `VERSION` on Msg.
 *
 * @tparam Msg Message type providing `io()`, `ID` and `VERSION`.
 * @param msg Message to serialize.
 * @param buffer Destination buffer.
 * @param buffer_size Capacity of buffer in bytes.
 * @return `size_t` Bytes written (header included), or 0 if the frame did not fit.
 */
template <typename Msg>
inline size_t serializeWithHeader(const Msg& msg, uint8_t* buffer, size_t buffer_size) {
#endif
  static_assert(detail::has_byte_id_v<Msg>, BYTEPACK_MISSING_ID_MSG);
  static_assert(detail::has_byte_version_v<Msg>, BYTEPACK_MISSING_VERSION_MSG);
  Writer w(buffer, buffer_size);
  w(Msg::ID, Msg::VERSION, msg);
  return w.isOk() ? w.getSize() : 0;
}

#if BYTEPACK_CONCEPTS_ENABLED && !defined(__INTELLISENSE__)
template <Concepts::SerializableWithHeader Msg>
inline bool deserializeWithHeader(Msg& msg, const uint8_t* data, size_t data_len) {
#else
/**
 * @brief Deserialize a `[Msg::ID][Msg::VERSION][fields]` frame.
 *
 * The header is checked with strict equality: a frame with a different ID or VERSION is rejected
 * without touching msg. Use `peekId()`/`peekVersion()` to inspect rejected frames.
 *
 * @tparam Msg Message type providing `io()`, `ID` and `VERSION`.
 * @param msg Message to fill.
 * @param data Frame bytes starting at the header.
 * @param data_len Frame length.
 * @return `true` on header match and successful deserialization, `false` otherwise.
 */
template <typename Msg>
inline bool deserializeWithHeader(Msg& msg, const uint8_t* data, size_t data_len) {
#endif
  static_assert(detail::has_byte_id_v<Msg>, BYTEPACK_MISSING_ID_MSG);
  static_assert(detail::has_byte_version_v<Msg>, BYTEPACK_MISSING_VERSION_MSG);
  if (data_len < 2 || data[0] != Msg::ID || data[1] != Msg::VERSION) return false;
  return deserialize(msg, data + 2, data_len - 2);
}

/**
 * @brief Peek the message `ID` (first header byte) without consuming anything.
 * @param data Frame bytes.
 * @param data_len Frame length.
 * @param id Output: the leading `ID` byte.
 * @return `false` if the frame is empty (`id` untouched).
 */
inline bool peekId(const uint8_t* data, size_t data_len, uint8_t& id) {
  if (data_len == 0) return false;
  id = data[0];
  return true;
}

/**
 * @brief Peek the message `VERSION` (second header byte) without consuming anything.
 * @param data Frame bytes.
 * @param data_len Frame length.
 * @param version Output: the `VERSION` byte.
 * @return `false` if the frame is shorter than 2 bytes (`version` untouched).
 */
inline bool peekVersion(const uint8_t* data, size_t data_len, uint8_t& version) {
  if (data_len < 2) return false;
  version = data[1];
  return true;
}

/**
 * @brief Builds a dispatch handler out of a set of lambdas, one per message type.
 *
 * Example:
 * ```cpp
 * dispatch<Ping, SetRelay>(data, data_len, Overloaded{
 *   [](Ping& msg) { ... },
 *   [](SetRelay& msg) { ... },
 * });
 * ```
 */

/**
 * @brief Build a dispatch handler out of a set of lambdas, one per message type.
 *
 * Each lambda must be invocable with `Msg&` for exactly one of the Msg types listed in the dispatch
 * call (checked at compile time). The resulting object can be passed as the handler to
 * `dispatch()`.
 *
 * Example:
 * ```cpp
 * dispatch<Ping, SetRelay>(data, data_len, Overloaded{
 *  [](const Ping& msg) { // do something with Ping },
 *  [](const SetRelay& msg) { // do something with SetRelay },
 * });
 * ```
 *
 * @tparam Fs Parameter pack of lambda types.
 */
template <class... Fs>
struct Overloaded : Fs... {
  using Fs::operator()...;
};
template <class... Fs>
Overloaded(Fs...) -> Overloaded<Fs...>;

namespace detail {

template <typename Msg, typename Handler>
inline bool tryDispatch(const uint8_t id, const uint8_t version, const uint8_t* body,
  const size_t len, Handler& h) {
  static_assert(has_byte_id_v<Msg>, BYTEPACK_MISSING_ID_MSG);
  static_assert(has_byte_version_v<Msg>, BYTEPACK_MISSING_VERSION_MSG);
  static_assert(std::is_invocable_v<Handler&, Msg&>, BYTEPACK_BAD_HANDLER_MSG);
  if (id != Msg::ID || version != Msg::VERSION) return false;
  Msg msg{};
  if (!deserialize(msg, body, len)) return false;
  h(msg);
  return true;
}

} // namespace detail

#if BYTEPACK_CONCEPTS_ENABLED && !defined(__INTELLISENSE__)
template <Concepts::SerializableWithHeader... Msgs, typename Handler>
  requires (std::is_invocable_v<Handler&, Msgs&> && ...)
inline bool dispatch(const uint8_t* data, const size_t data_len, Handler&& handler) {
#else
/**
 * @brief Route a `[ID][VERSION][fields]` frame to the handler of the matching message type.
 *
 * Tries each `Msg` in order: on an exact `ID` + `VERSION` match, the body is deserialized and the
 * handler is invoked with the message. The handler must be invocable with `Msg&` for every listed
 * type (checked at compile time).
 *
 * Example:
 * ```cpp
 * bool handled = dispatch<Ping, SetRelay>(data, data_len, Overloaded{
 *  [](const Ping& msg) { // do something with Ping },
 *  [](const SetRelay& msg) { // do something with SetRelay },
 * });
 * ```
 *
 * @tparam Msgs Candidate message types (each with `io()`, `ID` and `VERSION`).
 * @tparam Handler A callable type that must be invocable with `Msg&` for every `Msg` in `Msgs`.
 * @param data Frame bytes starting at the header.
 * @param data_len Frame length.
 * @param handler Handler: a callable or an Overloaded set of lambdas.
 * @return `true` if a message matched, deserialized correctly and was handled.
 */
template <typename... Msgs, typename Handler>
inline bool dispatch(const uint8_t* data, const size_t data_len, Handler&& handler) {
#endif
  if (data_len < 2) return false;
  const uint8_t id      = data[0];
  const uint8_t version = data[1];
  return (detail::tryDispatch<Msgs>(id, version, data + 2, data_len - 2, handler) || ...);
}

} // namespace BytePack