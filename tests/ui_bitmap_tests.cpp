#include "ziliu/ui/bitmap.h"
#include "ziliu/core/sogou_theme.h"

#include <windows.h>
#include <objbase.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <future>
#include <iostream>
#include <span>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>

namespace {

// Synthetic PNGs: uncompressed zlib scanlines with PNG chunk CRCs. These are
// format fixtures with known pixels, not screenshots or Sogou reference data.
constexpr std::array<std::uint8_t, 86> kRgbaPng{
    0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU, 0x00U, 0x00U, 0x00U, 0x0DU,
    0x49U, 0x48U, 0x44U, 0x52U, 0x00U, 0x00U, 0x00U, 0x02U, 0x00U, 0x00U, 0x00U, 0x02U,
    0x08U, 0x06U, 0x00U, 0x00U, 0x00U, 0x72U, 0xB6U, 0x0DU, 0x24U, 0x00U, 0x00U, 0x00U,
    0x1DU, 0x49U, 0x44U, 0x41U, 0x54U, 0x78U, 0x01U, 0x01U, 0x12U, 0x00U, 0xEDU, 0xFFU,
    0x00U, 0xFFU, 0x00U, 0x00U, 0xFFU, 0x00U, 0xFFU, 0x00U, 0x80U, 0x00U, 0x00U, 0x00U,
    0xFFU, 0x40U, 0x14U, 0x1EU, 0x28U, 0x00U, 0x38U, 0x1BU, 0x05U, 0x17U, 0x68U, 0x4CU,
    0xC0U, 0x8FU, 0x00U, 0x00U, 0x00U, 0x00U, 0x49U, 0x45U, 0x4EU, 0x44U, 0xAEU, 0x42U,
    0x60U, 0x82U,
};

constexpr std::array<std::uint8_t, 103> kIndexedPng{
    0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU, 0x00U, 0x00U, 0x00U, 0x0DU,
    0x49U, 0x48U, 0x44U, 0x52U, 0x00U, 0x00U, 0x00U, 0x02U, 0x00U, 0x00U, 0x00U, 0x01U,
    0x08U, 0x03U, 0x00U, 0x00U, 0x00U, 0xC3U, 0xFCU, 0x8FU, 0xB8U, 0x00U, 0x00U, 0x00U,
    0x06U, 0x50U, 0x4CU, 0x54U, 0x45U, 0xFFU, 0x00U, 0x00U, 0x00U, 0xFFU, 0x00U, 0xD2U,
    0x87U, 0xEFU, 0x71U, 0x00U, 0x00U, 0x00U, 0x02U, 0x74U, 0x52U, 0x4EU, 0x53U, 0xFFU,
    0x40U, 0x93U, 0x6BU, 0x71U, 0xDAU, 0x00U, 0x00U, 0x00U, 0x0EU, 0x49U, 0x44U, 0x41U,
    0x54U, 0x78U, 0x01U, 0x01U, 0x03U, 0x00U, 0xFCU, 0xFFU, 0x00U, 0x00U, 0x01U, 0x00U,
    0x04U, 0x00U, 0x02U, 0x0BU, 0x21U, 0x8BU, 0x71U, 0x00U, 0x00U, 0x00U, 0x00U, 0x49U,
    0x45U, 0x4EU, 0x44U, 0xAEU, 0x42U, 0x60U, 0x82U,
};

constexpr std::array<std::uint8_t, 71> kGrayPng{
    0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU, 0x00U, 0x00U, 0x00U, 0x0DU,
    0x49U, 0x48U, 0x44U, 0x52U, 0x00U, 0x00U, 0x00U, 0x02U, 0x00U, 0x00U, 0x00U, 0x01U,
    0x08U, 0x00U, 0x00U, 0x00U, 0x00U, 0xD1U, 0x49U, 0x20U, 0x56U, 0x00U, 0x00U, 0x00U,
    0x0EU, 0x49U, 0x44U, 0x41U, 0x54U, 0x78U, 0x01U, 0x01U, 0x03U, 0x00U, 0xFCU, 0xFFU,
    0x00U, 0x00U, 0xC8U, 0x00U, 0xCBU, 0x00U, 0xC9U, 0x79U, 0x61U, 0xCBU, 0x9EU, 0x00U,
    0x00U, 0x00U, 0x00U, 0x49U, 0x45U, 0x4EU, 0x44U, 0xAEU, 0x42U, 0x60U, 0x82U,
};

const std::vector<std::uint8_t> kExpectedRgba{
    255U, 0U, 0U, 255U, 0U, 255U, 0U, 128U,
    0U, 0U, 255U, 64U, 20U, 30U, 40U, 0U};

void Expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

void ExpectFailure(const ziliu::ui::BitmapDecodeResult& result,
                   ziliu::ui::BitmapDecodeError expected,
                   const std::source_location location = std::source_location::current()) {
  if (result.ok() || result.error != expected) {
    std::cerr << "at line " << location.line() << ": expected="
              << static_cast<int>(expected) << " actual="
              << static_cast<int>(result.error) << " HRESULT="
              << result.platform_error << '\n';
  }
  Expect(!result.ok() && result.error == expected,
         "invalid input must report the expected error category");
  Expect(result.bitmap.width == 0U && result.bitmap.height == 0U &&
             result.bitmap.stride == 0U && result.bitmap.pixels.empty(),
         "decode failure must not expose partially decoded pixels");
}

bool IsExpectedRgba(const ziliu::ui::BitmapDecodeResult& result) {
  return result.ok() && result.bitmap.width == 2U &&
         result.bitmap.height == 2U && result.bitmap.stride == 8U &&
         result.bitmap.pixels == kExpectedRgba;
}

}  // namespace

int main() {
  using namespace ziliu::ui;
  APTTYPE apartment_type{};
  APTTYPEQUALIFIER apartment_qualifier{};
  Expect(CoGetApartmentType(&apartment_type, &apartment_qualifier) ==
             CO_E_NOTINITIALIZED,
         "test thread must begin without COM initialization");
  Expect(IsExpectedRgba(DecodePngBitmap(kRgbaPng)),
         "RGBA PNG should preserve color, alpha and top-down row order");
  Expect(CoGetApartmentType(&apartment_type, &apartment_qualifier) ==
             CO_E_NOTINITIALIZED,
         "decoder must balance its own COM initialization");

  const auto indexed = DecodePngBitmap(kIndexedPng);
  Expect(indexed.ok() && indexed.bitmap.width == 2U &&
             indexed.bitmap.height == 1U && indexed.bitmap.stride == 8U &&
             indexed.bitmap.pixels ==
                 std::vector<std::uint8_t>{255U, 0U, 0U, 255U,
                                          0U, 255U, 0U, 64U},
         "palette PNG with tRNS should produce straight RGBA");
  const auto gray = DecodePngBitmap(kGrayPng);
  Expect(gray.ok() && gray.bitmap.pixels ==
             std::vector<std::uint8_t>{0U, 0U, 0U, 255U, 200U, 200U, 200U, 255U},
         "grayscale PNG should expand channels and opaque alpha");

  auto input = std::vector<std::uint8_t>(kRgbaPng.begin(), kRgbaPng.end());
  const auto owned = DecodePngBitmap(input);
  std::fill(input.begin(), input.end(), std::uint8_t{0U});
  input.clear();
  Expect(IsExpectedRgba(owned), "decoded pixels must outlive input storage");

  ExpectFailure(DecodePngBitmap({}), BitmapDecodeError::kInvalidInput);
  constexpr std::array<std::uint8_t, 3> kNotPng{0xFFU, 0xD8U, 0xFFU};
  ExpectFailure(DecodePngBitmap(kNotPng), BitmapDecodeError::kUnsupportedFormat);
  for (std::size_t length = 8U; length < kRgbaPng.size(); ++length) {
    ExpectFailure(DecodePngBitmap(std::span(kRgbaPng).first(length)),
                  BitmapDecodeError::kInvalidImage);
  }
  auto malformed = std::vector<std::uint8_t>(kRgbaPng.begin(), kRgbaPng.end());
  malformed[41U] = 0U;  // Invalid zlib header, inside IDAT.
  ExpectFailure(DecodePngBitmap(malformed), BitmapDecodeError::kInvalidImage);
  malformed.assign(kRgbaPng.begin(), kRgbaPng.end());
  malformed.push_back(0U);
  ExpectFailure(DecodePngBitmap(malformed), BitmapDecodeError::kInvalidImage);

  auto animated = std::vector<std::uint8_t>(kRgbaPng.begin(), kRgbaPng.end());
  constexpr std::array<std::uint8_t, 20> kAnimationControl{
      0U, 0U, 0U, 8U, 'a', 'c', 'T', 'L',
      0U, 0U, 0U, 1U, 0U, 0U, 0U, 0U, 0xB4U, 0x2DU, 0xE9U, 0xA0U};
  animated.insert(animated.begin() + 33U,
                  kAnimationControl.begin(), kAnimationControl.end());
  ExpectFailure(DecodePngBitmap(animated), BitmapDecodeError::kUnsupportedFormat);

  BitmapDecodeLimits limits;
  limits.maximum_input_bytes = kRgbaPng.size() - 1U;
  ExpectFailure(DecodePngBitmap(kRgbaPng, limits), BitmapDecodeError::kResourceLimit);
  limits = {};
  limits.maximum_dimension = 1U;
  ExpectFailure(DecodePngBitmap(kRgbaPng, limits), BitmapDecodeError::kResourceLimit);
  limits = {};
  limits.maximum_pixels = 3U;
  ExpectFailure(DecodePngBitmap(kRgbaPng, limits), BitmapDecodeError::kResourceLimit);
  limits.maximum_pixels = 4U;
  Expect(IsExpectedRgba(DecodePngBitmap(kRgbaPng, limits)),
         "exact pixel budget must allow a valid image");
  limits.maximum_pixels = 0U;
  ExpectFailure(DecodePngBitmap(kRgbaPng, limits), BitmapDecodeError::kInvalidInput);
  malformed.assign(kRgbaPng.begin(), kRgbaPng.end());
  std::fill(malformed.begin() + 16U, malformed.begin() + 24U, std::uint8_t{0xFFU});
  ExpectFailure(DecodePngBitmap(malformed), BitmapDecodeError::kResourceLimit);
  malformed.assign(kRgbaPng.begin(), kRgbaPng.end());
  std::fill(malformed.begin() + 16U, malformed.begin() + 20U, std::uint8_t{0U});
  ExpectFailure(DecodePngBitmap(malformed), BitmapDecodeError::kInvalidImage);

  const std::string ini = "[General]\nskin_name=Memory Image\n"
                          "[Scheme_H1]\npic=background.png\n";
  const std::vector<std::uint8_t> ini_bytes(ini.begin(), ini.end());
  const std::array entries{
      ziliu::core::SogouThemePackageEntryView{"skin.ini", ini_bytes},
      ziliu::core::SogouThemePackageEntryView{"background.png", kRgbaPng}};
  constexpr std::string_view kSyntheticPackageIdentity =
      "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  const auto package = ziliu::core::ConvertSogouThemePackage(
      entries, "memory.ssf", kSyntheticPackageIdentity);
  const auto resources = ziliu::core::ResolveSogouThemePackageResources(package, entries);
  Expect(package.ok() && resources.ok() && resources.assets.size() == 1U &&
             resources.source_package_sha256 == kSyntheticPackageIdentity,
         "converter and resource binder must deliver the declared image");
  Expect(IsExpectedRgba(DecodePngBitmap(resources.assets[0].bytes)),
         "decoder must consume binder-returned bytes without file extraction");

  const HRESULT sta_status = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  Expect(SUCCEEDED(sta_status), "STA initialization should succeed");
  Expect(IsExpectedRgba(DecodePngBitmap(kRgbaPng)),
         "decoder should work on an existing STA thread");
  Expect(SUCCEEDED(CoGetApartmentType(&apartment_type, &apartment_qualifier)) &&
             (apartment_type == APTTYPE_STA || apartment_type == APTTYPE_MAINSTA),
         "decoder must preserve the caller's STA apartment");
  CoUninitialize();
  const HRESULT mta_status = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  Expect(SUCCEEDED(mta_status), "MTA initialization should succeed");
  Expect(IsExpectedRgba(DecodePngBitmap(kRgbaPng)),
         "decoder should work on an existing MTA thread");
  Expect(SUCCEEDED(CoGetApartmentType(&apartment_type, &apartment_qualifier)) &&
             apartment_type == APTTYPE_MTA,
         "decoder must preserve the caller's MTA apartment");
  CoUninitialize();
  auto first = std::async(std::launch::async, [] { return DecodePngBitmap(kRgbaPng); });
  auto second = std::async(std::launch::async, [] { return DecodePngBitmap(kRgbaPng); });
  Expect(IsExpectedRgba(first.get()) && IsExpectedRgba(second.get()),
         "independent decode calls should work on concurrent threads");
  return EXIT_SUCCESS;
}
