#include "ziliu/ui/bitmap.h"

#include <windows.h>
#include <shlwapi.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <utility>

namespace ziliu::ui {
namespace {

using Microsoft::WRL::ComPtr;

BitmapDecodeResult Failure(BitmapDecodeError error, HRESULT status = S_OK) {
  return {{}, error, static_cast<std::int32_t>(status)};
}

std::uint32_t ReadBigEndian(std::span<const std::uint8_t> bytes, std::size_t offset) {
  return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
         (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
         (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
         static_cast<std::uint32_t>(bytes[offset + 3U]);
}

bool ChunkIs(std::span<const std::uint8_t> bytes, std::size_t offset,
             const char (&type)[5]) {
  return std::equal(type, type + 4, bytes.begin() + offset + 4U);
}

std::uint32_t PngCrc32(std::span<const std::uint8_t> bytes) {
  static constexpr auto table = [] {
    std::array<std::uint32_t, 256> values{};
    for (std::uint32_t index = 0; index < values.size(); ++index) {
      std::uint32_t value = index;
      for (unsigned bit = 0; bit < 8U; ++bit) {
        value = (value >> 1U) ^ ((value & 1U) != 0U ? 0xEDB88320U : 0U);
      }
      values[index] = value;
    }
    return values;
  }();
  std::uint32_t crc = 0xFFFFFFFFU;
  for (const std::uint8_t byte : bytes) {
    crc = table[(crc ^ byte) & 0xFFU] ^ (crc >> 8U);
  }
  return ~crc;
}

class ComApartment final {
 public:
  ComApartment() : status_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
  ~ComApartment() {
    if (SUCCEEDED(status_)) {
      CoUninitialize();
    }
  }
  ComApartment(const ComApartment&) = delete;
  ComApartment& operator=(const ComApartment&) = delete;
  [[nodiscard]] HRESULT status() const noexcept { return status_; }

 private:
  HRESULT status_;
};

BitmapDecodeResult Decode(std::span<const std::uint8_t> bytes,
                           const BitmapDecodeLimits& limits) {
  constexpr std::array<std::uint8_t, 8> kPngSignature{
      0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU};
  if (bytes.empty() || limits.maximum_input_bytes == 0U ||
      limits.maximum_dimension == 0U || limits.maximum_pixels == 0U) {
    return Failure(BitmapDecodeError::kInvalidInput);
  }
  if (bytes.size() > limits.maximum_input_bytes ||
      bytes.size() > (std::numeric_limits<UINT>::max)()) {
    return Failure(BitmapDecodeError::kResourceLimit);
  }
  if (bytes.size() < kPngSignature.size() ||
      !std::equal(kPngSignature.begin(), kPngSignature.end(), bytes.begin())) {
    return Failure(BitmapDecodeError::kUnsupportedFormat);
  }
  if (bytes.size() < 33U || ReadBigEndian(bytes, 8U) != 13U ||
      !ChunkIs(bytes, 8U, "IHDR")) {
    return Failure(BitmapDecodeError::kInvalidImage);
  }

  const std::uint32_t width = ReadBigEndian(bytes, 16U);
  const std::uint32_t height = ReadBigEndian(bytes, 20U);
  if (width == 0U || height == 0U) {
    return Failure(BitmapDecodeError::kInvalidImage);
  }
  const std::uint64_t pixel_count = static_cast<std::uint64_t>(width) * height;
  if (width > limits.maximum_dimension || height > limits.maximum_dimension ||
      pixel_count > limits.maximum_pixels ||
      pixel_count > (std::numeric_limits<UINT>::max)() / 4U ||
      pixel_count > (std::numeric_limits<std::size_t>::max)() / 4U) {
    return Failure(BitmapDecodeError::kResourceLimit);
  }

  // WIC can accept corrupt IDAT data without checking the PNG chunk CRC.
  // Require a complete, checksum-valid static container before invoking WIC.
  // Pixel, compression, palette and filter decoding remain the codec's job.
  bool saw_data = false;
  bool saw_end = false;
  for (std::size_t offset = 8U; offset < bytes.size();) {
    if (bytes.size() - offset < 12U) {
      return Failure(BitmapDecodeError::kInvalidImage);
    }
    const std::size_t length = ReadBigEndian(bytes, offset);
    if (length > bytes.size() - offset - 12U) {
      return Failure(BitmapDecodeError::kInvalidImage);
    }
    if (PngCrc32(bytes.subspan(offset + 4U, length + 4U)) !=
        ReadBigEndian(bytes, offset + 8U + length)) {
      return Failure(BitmapDecodeError::kInvalidImage);
    }
    if (ChunkIs(bytes, offset, "acTL")) {
      return Failure(BitmapDecodeError::kUnsupportedFormat);
    }
    saw_data = saw_data || ChunkIs(bytes, offset, "IDAT");
    if (ChunkIs(bytes, offset, "IEND")) {
      if (length != 0U || offset + 12U != bytes.size()) {
        return Failure(BitmapDecodeError::kInvalidImage);
      }
      saw_end = true;
    }
    offset += length + 12U;
  }
  if (!saw_data || !saw_end) {
    return Failure(BitmapDecodeError::kInvalidImage);
  }

  ComApartment apartment;
  if (FAILED(apartment.status()) && apartment.status() != RPC_E_CHANGED_MODE) {
    return Failure(BitmapDecodeError::kPlatformFailure, apartment.status());
  }
  ComPtr<IStream> stream;
  stream.Attach(SHCreateMemStream(bytes.data(), static_cast<UINT>(bytes.size())));
  if (!stream) {
    return Failure(BitmapDecodeError::kOutOfMemory, E_OUTOFMEMORY);
  }
  ComPtr<IWICBitmapDecoder> decoder;
  HRESULT status = CoCreateInstance(CLSID_WICPngDecoder, nullptr,
                                    CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&decoder));
  if (FAILED(status)) {
    return Failure(BitmapDecodeError::kPlatformFailure, status);
  }
  status = decoder->Initialize(stream.Get(), WICDecodeMetadataCacheOnDemand);
  if (FAILED(status)) {
    return Failure(BitmapDecodeError::kInvalidImage, status);
  }
  UINT frames = 0;
  status = decoder->GetFrameCount(&frames);
  if (FAILED(status) || frames != 1U) {
    return Failure(BitmapDecodeError::kInvalidImage, status);
  }
  ComPtr<IWICBitmapFrameDecode> frame;
  status = decoder->GetFrame(0U, &frame);
  if (FAILED(status)) {
    return Failure(BitmapDecodeError::kInvalidImage, status);
  }
  UINT decoded_width = 0;
  UINT decoded_height = 0;
  status = frame->GetSize(&decoded_width, &decoded_height);
  if (FAILED(status) || decoded_width != width || decoded_height != height) {
    return Failure(BitmapDecodeError::kInvalidImage, status);
  }
  ComPtr<IWICBitmapSource> rgba;
  status = WICConvertBitmapSource(GUID_WICPixelFormat32bppRGBA, frame.Get(), &rgba);
  if (FAILED(status)) {
    return Failure(BitmapDecodeError::kInvalidImage, status);
  }
  RgbaBitmap bitmap;
  bitmap.width = width;
  bitmap.height = height;
  bitmap.stride = width * 4U;
  bitmap.pixels.resize(static_cast<std::size_t>(pixel_count) * 4U);
  status = rgba->CopyPixels(nullptr, bitmap.stride,
                            static_cast<UINT>(bitmap.pixels.size()),
                            bitmap.pixels.data());
  if (FAILED(status)) {
    return Failure(BitmapDecodeError::kInvalidImage, status);
  }
  return {std::move(bitmap), BitmapDecodeError::kNone, 0};
}

}  // namespace

BitmapDecodeResult DecodePngBitmap(std::span<const std::uint8_t> bytes,
                                   const BitmapDecodeLimits& limits) {
  try {
    return Decode(bytes, limits);
  } catch (const std::bad_alloc&) {
    return Failure(BitmapDecodeError::kOutOfMemory, E_OUTOFMEMORY);
  }
}

}  // namespace ziliu::ui
