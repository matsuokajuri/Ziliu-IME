#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ziliu::ui {

// Resource safety limits, unrelated to a skin's layout or display DPI.
struct BitmapDecodeLimits {
  std::size_t maximum_input_bytes = 64U * 1024U * 1024U;
  std::uint32_t maximum_dimension = 16384U;
  std::uint64_t maximum_pixels = 16U * 1024U * 1024U;
};

struct RgbaBitmap {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t stride = 0;
  // Owned, tightly packed, top-down R/G/B/A bytes with straight alpha.
  std::vector<std::uint8_t> pixels;
};

enum class BitmapDecodeError {
  kNone,
  kInvalidInput,
  kUnsupportedFormat,
  kInvalidImage,
  kResourceLimit,
  kPlatformFailure,
  kOutOfMemory,
};

struct BitmapDecodeResult {
  RgbaBitmap bitmap;
  BitmapDecodeError error = BitmapDecodeError::kNone;
  std::int32_t platform_error = 0;

  [[nodiscard]] bool ok() const noexcept { return error == BitmapDecodeError::kNone; }
};

// Decodes one static PNG using the Windows PNG codec, without a window or files.
// No scaling, alpha premultiplication, orientation or color-profile transform is
// applied. APNG is unsupported. Input is borrowed only for this synchronous call;
// output has no dependency on input storage. Works on uninitialized, STA and MTA
// threads without changing the caller's COM apartment. Calls share no state.
[[nodiscard]] BitmapDecodeResult DecodePngBitmap(
    std::span<const std::uint8_t> bytes,
    const BitmapDecodeLimits& limits = {});

}  // namespace ziliu::ui
