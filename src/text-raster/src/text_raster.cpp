#include "ziliu/text_raster/text_raster_api.h"

#include "text_raster_math.h"

#include <dwrite.h>
#include <ft2build.h>
#include <wrl/client.h>

#include FT_FREETYPE_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ziliu::text_raster {
namespace {

using Microsoft::WRL::ComPtr;

struct FileHandleCloser {
  void operator()(void* handle) const noexcept {
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
      static_cast<void>(CloseHandle(handle));
    }
  }
};

using UniqueFileHandle = std::unique_ptr<void, FileHandleCloser>;

std::optional<std::vector<std::uint8_t>> ReadFileBytes(
    const std::wstring& path) {
  UniqueFileHandle file(
      CreateFileW(path.c_str(), GENERIC_READ,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
  if (file.get() == INVALID_HANDLE_VALUE) {
    static_cast<void>(file.release());
    return std::nullopt;
  }
  LARGE_INTEGER size{};
  if (GetFileSizeEx(file.get(), &size) == FALSE || size.QuadPart <= 0 ||
      size.QuadPart > static_cast<LONGLONG>(
                              (std::numeric_limits<FT_Long>::max)()) ||
      size.QuadPart > static_cast<LONGLONG>(
                              (std::numeric_limits<DWORD>::max)())) {
    return std::nullopt;
  }
  std::vector<std::uint8_t> bytes(
      static_cast<std::size_t>(size.QuadPart));
  DWORD bytes_read = 0;
  if (ReadFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()),
               &bytes_read, nullptr) == FALSE ||
      static_cast<std::size_t>(bytes_read) != bytes.size()) {
    return std::nullopt;
  }
  return bytes;
}

struct Face {
  Face() = default;
  Face(const Face&) = delete;
  Face& operator=(const Face&) = delete;

  Face(Face&& other) noexcept
      : file_bytes(std::move(other.file_bytes)),
        value(std::exchange(other.value, nullptr)) {}

  Face& operator=(Face&& other) noexcept {
    if (this != &other) {
      Reset();
      file_bytes = std::move(other.file_bytes);
      value = std::exchange(other.value, nullptr);
    }
    return *this;
  }

  ~Face() { Reset(); }

  void Reset() noexcept {
    if (value != nullptr) {
      static_cast<void>(FT_Done_Face(value));
      value = nullptr;
    }
  }

  std::vector<std::uint8_t> file_bytes;
  FT_Face value = nullptr;
};

bool DecodeUtf16(std::wstring_view text, bool replace_invalid,
                 std::vector<std::uint32_t>* scalars) {
  if (scalars == nullptr) {
    return false;
  }
  scalars->clear();
  scalars->reserve(text.size());
  std::size_t index = 0;
  while (index < text.size()) {
    const auto first = static_cast<std::uint16_t>(text[index]);
    ++index;
    if (first >= 0xD800U && first <= 0xDBFFU) {
      if (index < text.size()) {
        const auto second = static_cast<std::uint16_t>(text[index]);
        if (second >= 0xDC00U && second <= 0xDFFFU) {
          ++index;
          scalars->push_back(
              0x10000U +
              ((static_cast<std::uint32_t>(first) - 0xD800U) << 10U) +
              (static_cast<std::uint32_t>(second) - 0xDC00U));
          continue;
        }
      }
      if (!replace_invalid) {
        return false;
      }
      scalars->push_back(0xFFFDU);
      continue;
    }
    if (first >= 0xDC00U && first <= 0xDFFFU) {
      if (!replace_invalid) {
        return false;
      }
      scalars->push_back(0xFFFDU);
      continue;
    }
    scalars->push_back(first);
  }
  return true;
}

std::size_t AbsolutePitch(const FT_Bitmap& bitmap) noexcept {
  const auto pitch = static_cast<std::int64_t>(bitmap.pitch);
  return static_cast<std::size_t>(pitch >= 0 ? pitch : -pitch);
}

bool IsSupportedBitmap(const FT_Bitmap& bitmap) noexcept {
  if (bitmap.width == 0U || bitmap.rows == 0U) {
    return true;
  }
  if (bitmap.buffer == nullptr) {
    return false;
  }
  const std::size_t pitch = AbsolutePitch(bitmap);
  if (bitmap.pixel_mode == FT_PIXEL_MODE_GRAY) {
    return bitmap.num_grays > 1U && pitch >= bitmap.width;
  }
  if (bitmap.pixel_mode == FT_PIXEL_MODE_MONO) {
    return pitch >= (static_cast<std::size_t>(bitmap.width) + 7U) / 8U;
  }
  return false;
}

std::uint8_t ReadCoverage(const FT_Bitmap& bitmap, std::uint32_t x,
                          std::uint32_t y) noexcept {
  if (bitmap.buffer == nullptr || x >= bitmap.width || y >= bitmap.rows) {
    return 0U;
  }
  const std::size_t pitch = AbsolutePitch(bitmap);
  const std::size_t row_index =
      bitmap.pitch >= 0
          ? static_cast<std::size_t>(y)
          : static_cast<std::size_t>(bitmap.rows - 1U - y);
  const std::uint8_t* row = bitmap.buffer + row_index * pitch;
  if (bitmap.pixel_mode == FT_PIXEL_MODE_GRAY) {
    const auto maximum = static_cast<std::uint32_t>(bitmap.num_grays - 1U);
    return static_cast<std::uint8_t>(
        (static_cast<std::uint32_t>(row[x]) * 255U) / maximum);
  }
  if (bitmap.pixel_mode == FT_PIXEL_MODE_MONO) {
    const auto mask = static_cast<std::uint8_t>(0x80U >> (x & 7U));
    return (row[x >> 3U] & mask) != 0U ? 255U : 0U;
  }
  return 0U;
}

DWRITE_FONT_STYLE ToDwriteStyle(std::uint32_t style) noexcept {
  switch (style) {
    case ZILIU_TEXT_RASTER_FONT_STYLE_OBLIQUE:
      return DWRITE_FONT_STYLE_OBLIQUE;
    case ZILIU_TEXT_RASTER_FONT_STYLE_ITALIC:
      return DWRITE_FONT_STYLE_ITALIC;
    default:
      return DWRITE_FONT_STYLE_NORMAL;
  }
}

std::optional<FT_Int32> ResolveLoadFlags(
    const ZiliuTextRasterGlyphOptions& options) noexcept {
  FT_Int32 flags = FT_LOAD_DEFAULT;
  switch (options.hinting) {
    case ZILIU_TEXT_RASTER_HINTING_NATIVE:
      break;
    case ZILIU_TEXT_RASTER_HINTING_NONE:
      flags |= FT_LOAD_NO_HINTING;
      break;
    case ZILIU_TEXT_RASTER_HINTING_AUTO:
      flags |= FT_LOAD_FORCE_AUTOHINT;
      break;
    default:
      return std::nullopt;
  }
  switch (options.hinting_target) {
    case ZILIU_TEXT_RASTER_TARGET_NORMAL:
      flags |= FT_LOAD_TARGET_NORMAL;
      break;
    case ZILIU_TEXT_RASTER_TARGET_LIGHT:
      flags |= FT_LOAD_TARGET_LIGHT;
      break;
    case ZILIU_TEXT_RASTER_TARGET_MONO:
      flags |= FT_LOAD_TARGET_MONO;
      break;
    default:
      return std::nullopt;
  }
  return flags;
}

std::optional<FT_Render_Mode> ResolveRenderMode(
    std::uint32_t mode) noexcept {
  switch (mode) {
    case ZILIU_TEXT_RASTER_RENDER_NORMAL:
      return FT_RENDER_MODE_NORMAL;
    case ZILIU_TEXT_RASTER_RENDER_LIGHT:
      return FT_RENDER_MODE_LIGHT;
    case ZILIU_TEXT_RASTER_RENDER_MONO:
      return FT_RENDER_MODE_MONO;
    default:
      return std::nullopt;
  }
}

class RasterContext final {
 public:
  RasterContext() = default;
  RasterContext(const RasterContext&) = delete;
  RasterContext& operator=(const RasterContext&) = delete;

  ~RasterContext() {
    face_.Reset();
    if (library_ != nullptr) {
      static_cast<void>(FT_Done_FreeType(library_));
      library_ = nullptr;
    }
  }

  ZiliuTextRasterStatus Initialize() {
    if (FT_Init_FreeType(&library_) != 0 || library_ == nullptr) {
      return ZILIU_TEXT_RASTER_STATUS_NOT_INITIALIZED;
    }
    const HRESULT result = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(factory_.ReleaseAndGetAddressOf()));
    if (FAILED(result) || factory_ == nullptr) {
      static_cast<void>(FT_Done_FreeType(library_));
      library_ = nullptr;
      return ZILIU_TEXT_RASTER_STATUS_NOT_INITIALIZED;
    }
    return ZILIU_TEXT_RASTER_STATUS_OK;
  }

  ZiliuTextRasterStatus ConfigureFont(
      const ZiliuTextRasterFontConfig& config) {
    if (config.family_name == nullptr || config.family_name[0] == L'\0' ||
        config.weight < 1U || config.weight > 999U ||
        config.stretch < 1U || config.stretch > 9U ||
        config.style > ZILIU_TEXT_RASTER_FONT_STYLE_ITALIC ||
        config.pixel_width > 65535U || config.pixel_height == 0U ||
        config.pixel_height > 65535U) {
      return ZILIU_TEXT_RASTER_STATUS_INVALID_ARGUMENT;
    }

    std::scoped_lock lock(mutex_);
    std::wstring path;
    FT_Long face_index = 0;
    const ZiliuTextRasterStatus resolve_status = ResolveFontFile(
        config, &path, &face_index);
    if (resolve_status != ZILIU_TEXT_RASTER_STATUS_OK) {
      return resolve_status;
    }
    auto bytes = ReadFileBytes(path);
    if (!bytes.has_value()) {
      return ZILIU_TEXT_RASTER_STATUS_FONT_LOAD_FAILED;
    }

    Face replacement;
    replacement.file_bytes = std::move(*bytes);
    FT_Face loaded_face = nullptr;
    if (FT_New_Memory_Face(
            library_, replacement.file_bytes.data(),
            static_cast<FT_Long>(replacement.file_bytes.size()), face_index,
            &loaded_face) != 0 ||
        loaded_face == nullptr) {
      return ZILIU_TEXT_RASTER_STATUS_FONT_LOAD_FAILED;
    }
    replacement.value = loaded_face;
    if (FT_Select_Charmap(replacement.value, FT_ENCODING_UNICODE) != 0 ||
        FT_Set_Pixel_Sizes(replacement.value, config.pixel_width,
                           config.pixel_height) != 0) {
      return ZILIU_TEXT_RASTER_STATUS_FONT_LOAD_FAILED;
    }
    face_ = std::move(replacement);
    return ZILIU_TEXT_RASTER_STATUS_OK;
  }

  ZiliuTextRasterStatus LookupGlyph(std::uint32_t scalar,
                                    std::uint32_t* glyph_index) {
    if (!detail::IsUnicodeScalar(scalar) || glyph_index == nullptr) {
      return ZILIU_TEXT_RASTER_STATUS_INVALID_ARGUMENT;
    }
    std::scoped_lock lock(mutex_);
    if (face_.value == nullptr) {
      return ZILIU_TEXT_RASTER_STATUS_NOT_INITIALIZED;
    }
    const FT_UInt resolved = FT_Get_Char_Index(face_.value, scalar);
    if (resolved == 0U) {
      *glyph_index = 0U;
      return ZILIU_TEXT_RASTER_STATUS_GLYPH_NOT_FOUND;
    }
    *glyph_index = resolved;
    return ZILIU_TEXT_RASTER_STATUS_OK;
  }

  ZiliuTextRasterStatus RenderGlyph(
      std::uint32_t glyph_index,
      const ZiliuTextRasterGlyphOptions& options,
      ZiliuTextRasterGlyphMetrics* metrics,
      ZiliuTextRasterCoverageBitmap* bitmap) {
    if (glyph_index == 0U || metrics == nullptr || bitmap == nullptr ||
        metrics->struct_size < sizeof(ZiliuTextRasterGlyphMetrics) ||
        bitmap->struct_size < sizeof(ZiliuTextRasterCoverageBitmap)) {
      return ZILIU_TEXT_RASTER_STATUS_INVALID_ARGUMENT;
    }
    const auto load_flags = ResolveLoadFlags(options);
    const auto render_mode = ResolveRenderMode(options.render_mode);
    if (!load_flags.has_value() || !render_mode.has_value()) {
      return ZILIU_TEXT_RASTER_STATUS_INVALID_ARGUMENT;
    }

    std::scoped_lock lock(mutex_);
    if (face_.value == nullptr) {
      return ZILIU_TEXT_RASTER_STATUS_NOT_INITIALIZED;
    }
    if (FT_Load_Glyph(face_.value, glyph_index, *load_flags) != 0 ||
        FT_Render_Glyph(face_.value->glyph, *render_mode) != 0) {
      return ZILIU_TEXT_RASTER_STATUS_GLYPH_LOAD_FAILED;
    }
    const FT_GlyphSlot slot = face_.value->glyph;
    if (!IsSupportedBitmap(slot->bitmap)) {
      return ZILIU_TEXT_RASTER_STATUS_UNSUPPORTED_BITMAP;
    }

    metrics->glyph_index = glyph_index;
    metrics->bitmap_left = slot->bitmap_left;
    metrics->bitmap_top = slot->bitmap_top;
    metrics->bitmap_width = slot->bitmap.width;
    metrics->bitmap_height = slot->bitmap.rows;
    metrics->advance_x_26_6 = static_cast<std::int32_t>(slot->advance.x);
    metrics->advance_y_26_6 = static_cast<std::int32_t>(slot->advance.y);
    metrics->bearing_x_26_6 =
        static_cast<std::int32_t>(slot->metrics.horiBearingX);
    metrics->bearing_y_26_6 =
        static_cast<std::int32_t>(slot->metrics.horiBearingY);
    metrics->outline_width_26_6 =
        static_cast<std::int32_t>(slot->metrics.width);
    metrics->outline_height_26_6 =
        static_cast<std::int32_t>(slot->metrics.height);

    bitmap->width = slot->bitmap.width;
    bitmap->height = slot->bitmap.rows;
    if (bitmap->width == 0U || bitmap->height == 0U) {
      return ZILIU_TEXT_RASTER_STATUS_OK;
    }
    if (bitmap->pixels == nullptr ||
        !detail::FitsBuffer(bitmap->width, bitmap->height,
                            bitmap->stride_bytes, bitmap->capacity_bytes)) {
      return ZILIU_TEXT_RASTER_STATUS_BUFFER_TOO_SMALL;
    }
    for (std::uint32_t y = 0; y < bitmap->height; ++y) {
      std::uint8_t* destination =
          bitmap->pixels + static_cast<std::size_t>(y) * bitmap->stride_bytes;
      for (std::uint32_t x = 0; x < bitmap->width; ++x) {
        destination[x] = ReadCoverage(slot->bitmap, x, y);
      }
    }
    return ZILIU_TEXT_RASTER_STATUS_OK;
  }

 private:
  ZiliuTextRasterStatus ResolveFontFile(
      const ZiliuTextRasterFontConfig& config, std::wstring* path,
      FT_Long* face_index) const {
    if (path == nullptr || face_index == nullptr || factory_ == nullptr) {
      return ZILIU_TEXT_RASTER_STATUS_INVALID_ARGUMENT;
    }
    ComPtr<IDWriteFontCollection> collection;
    HRESULT result =
        factory_->GetSystemFontCollection(collection.GetAddressOf(), FALSE);
    if (FAILED(result) || collection == nullptr) {
      return ZILIU_TEXT_RASTER_STATUS_FONT_NOT_FOUND;
    }

    UINT32 family_index = 0;
    BOOL exists = FALSE;
    result = collection->FindFamilyName(config.family_name, &family_index,
                                        &exists);
    if (FAILED(result) || exists == FALSE) {
      return ZILIU_TEXT_RASTER_STATUS_FONT_NOT_FOUND;
    }
    ComPtr<IDWriteFontFamily> family;
    result = collection->GetFontFamily(family_index, family.GetAddressOf());
    if (FAILED(result) || family == nullptr) {
      return ZILIU_TEXT_RASTER_STATUS_FONT_NOT_FOUND;
    }
    ComPtr<IDWriteFont> font;
    result = family->GetFirstMatchingFont(
        static_cast<DWRITE_FONT_WEIGHT>(config.weight),
        static_cast<DWRITE_FONT_STRETCH>(config.stretch),
        ToDwriteStyle(config.style), font.GetAddressOf());
    if (FAILED(result) || font == nullptr) {
      return ZILIU_TEXT_RASTER_STATUS_FONT_NOT_FOUND;
    }
    ComPtr<IDWriteFontFace> font_face;
    result = font->CreateFontFace(font_face.GetAddressOf());
    if (FAILED(result) || font_face == nullptr) {
      return ZILIU_TEXT_RASTER_STATUS_FONT_NOT_FOUND;
    }

    UINT32 file_count = 0;
    result = font_face->GetFiles(&file_count, nullptr);
    if (FAILED(result) || file_count == 0U) {
      return ZILIU_TEXT_RASTER_STATUS_FONT_NOT_FOUND;
    }
    std::vector<IDWriteFontFile*> raw_files(file_count, nullptr);
    result = font_face->GetFiles(&file_count, raw_files.data());
    if (FAILED(result)) {
      for (IDWriteFontFile* file : raw_files) {
        if (file != nullptr) {
          file->Release();
        }
      }
      return ZILIU_TEXT_RASTER_STATUS_FONT_NOT_FOUND;
    }

    std::vector<ComPtr<IDWriteFontFile>> files;
    files.reserve(file_count);
    for (IDWriteFontFile* raw_file : raw_files) {
      ComPtr<IDWriteFontFile> file;
      file.Attach(raw_file);
      files.push_back(std::move(file));
    }
    for (const auto& file : files) {
      const void* key = nullptr;
      UINT32 key_size = 0;
      result = file->GetReferenceKey(&key, &key_size);
      if (FAILED(result) || key == nullptr) {
        continue;
      }
      ComPtr<IDWriteFontFileLoader> loader;
      result = file->GetLoader(loader.GetAddressOf());
      if (FAILED(result) || loader == nullptr) {
        continue;
      }
      ComPtr<IDWriteLocalFontFileLoader> local_loader;
      result = loader.As(&local_loader);
      if (FAILED(result) || local_loader == nullptr) {
        continue;
      }
      UINT32 path_length = 0;
      result = local_loader->GetFilePathLengthFromKey(
          key, key_size, &path_length);
      if (FAILED(result) || path_length == 0U) {
        continue;
      }
      std::vector<wchar_t> path_buffer(
          static_cast<std::size_t>(path_length) + 1U, L'\0');
      result = local_loader->GetFilePathFromKey(
          key, key_size, path_buffer.data(), path_length + 1U);
      if (FAILED(result)) {
        continue;
      }
      path->assign(path_buffer.data(), path_length);
      *face_index = static_cast<FT_Long>(font_face->GetIndex());
      return ZILIU_TEXT_RASTER_STATUS_OK;
    }
    return ZILIU_TEXT_RASTER_STATUS_FONT_NOT_FOUND;
  }

  FT_Library library_ = nullptr;
  ComPtr<IDWriteFactory> factory_;
  Face face_;
  std::mutex mutex_;
};

template <typename Function>
ZiliuTextRasterStatus GuardStatus(Function&& function) noexcept {
  try {
    return function();
  } catch (const std::bad_alloc&) {
    return ZILIU_TEXT_RASTER_STATUS_OUT_OF_MEMORY;
  } catch (...) {
    return ZILIU_TEXT_RASTER_STATUS_INTERNAL_ERROR;
  }
}

}  // namespace
}  // namespace ziliu::text_raster

struct ZiliuTextRasterContext {
  ziliu::text_raster::RasterContext implementation;
};

extern "C" {

uint32_t ZILIU_TEXT_RASTER_CALL ZiliuTextRasterGetAbiVersion(void) {
  return ZILIU_TEXT_RASTER_ABI_VERSION;
}

ZiliuTextRasterStatus ZILIU_TEXT_RASTER_CALL
ZiliuTextRasterCreate(ZiliuTextRasterHandle* handle) {
  if (handle == nullptr) {
    return ZILIU_TEXT_RASTER_STATUS_INVALID_ARGUMENT;
  }
  *handle = nullptr;
  return ziliu::text_raster::GuardStatus([&]() {
    auto context = std::make_unique<ZiliuTextRasterContext>();
    const ZiliuTextRasterStatus status = context->implementation.Initialize();
    if (status != ZILIU_TEXT_RASTER_STATUS_OK) {
      return status;
    }
    *handle = context.release();
    return ZILIU_TEXT_RASTER_STATUS_OK;
  });
}

ZiliuTextRasterStatus ZILIU_TEXT_RASTER_CALL ZiliuTextRasterConfigureFont(
    ZiliuTextRasterHandle handle, const ZiliuTextRasterFontConfig* config) {
  if (handle == nullptr || config == nullptr ||
      config->struct_size < sizeof(ZiliuTextRasterFontConfig)) {
    return ZILIU_TEXT_RASTER_STATUS_INVALID_ARGUMENT;
  }
  return ziliu::text_raster::GuardStatus(
      [&]() { return handle->implementation.ConfigureFont(*config); });
}

ZiliuTextRasterStatus ZILIU_TEXT_RASTER_CALL ZiliuTextRasterDecodeUtf16(
    const wchar_t* text, uint32_t text_length, uint32_t invalid_policy,
    uint32_t* scalars, uint32_t scalar_capacity, uint32_t* scalar_count) {
  if ((text == nullptr && text_length != 0U) || scalar_count == nullptr ||
      (scalars == nullptr && scalar_capacity != 0U) ||
      invalid_policy > ZILIU_TEXT_RASTER_INVALID_UTF16_REPLACE) {
    return ZILIU_TEXT_RASTER_STATUS_INVALID_ARGUMENT;
  }
  return ziliu::text_raster::GuardStatus([&]() {
    const std::wstring_view input(text == nullptr ? L"" : text, text_length);
    std::vector<std::uint32_t> decoded;
    if (!ziliu::text_raster::DecodeUtf16(
            input,
            invalid_policy == ZILIU_TEXT_RASTER_INVALID_UTF16_REPLACE,
            &decoded)) {
      *scalar_count = 0U;
      return ZILIU_TEXT_RASTER_STATUS_INVALID_UTF16;
    }
    if (decoded.size() > (std::numeric_limits<std::uint32_t>::max)()) {
      return ZILIU_TEXT_RASTER_STATUS_INTERNAL_ERROR;
    }
    *scalar_count = static_cast<std::uint32_t>(decoded.size());
    if (scalar_capacity < decoded.size()) {
      return ZILIU_TEXT_RASTER_STATUS_BUFFER_TOO_SMALL;
    }
    std::copy(decoded.begin(), decoded.end(), scalars);
    return ZILIU_TEXT_RASTER_STATUS_OK;
  });
}

ZiliuTextRasterStatus ZILIU_TEXT_RASTER_CALL ZiliuTextRasterLookupGlyph(
    ZiliuTextRasterHandle handle, uint32_t unicode_scalar,
    uint32_t* glyph_index) {
  if (handle == nullptr) {
    return ZILIU_TEXT_RASTER_STATUS_INVALID_ARGUMENT;
  }
  return ziliu::text_raster::GuardStatus([&]() {
    return handle->implementation.LookupGlyph(unicode_scalar, glyph_index);
  });
}

ZiliuTextRasterStatus ZILIU_TEXT_RASTER_CALL ZiliuTextRasterRenderGlyph(
    ZiliuTextRasterHandle handle, uint32_t glyph_index,
    const ZiliuTextRasterGlyphOptions* options,
    ZiliuTextRasterGlyphMetrics* metrics,
    ZiliuTextRasterCoverageBitmap* bitmap) {
  if (handle == nullptr || options == nullptr ||
      options->struct_size < sizeof(ZiliuTextRasterGlyphOptions)) {
    return ZILIU_TEXT_RASTER_STATUS_INVALID_ARGUMENT;
  }
  return ziliu::text_raster::GuardStatus([&]() {
    return handle->implementation.RenderGlyph(glyph_index, *options, metrics,
                                               bitmap);
  });
}

ZiliuTextRasterStatus ZILIU_TEXT_RASTER_CALL
ZiliuTextRasterCompositeCoverageBgra(
    const ZiliuTextRasterCoverageBitmap* coverage, uint32_t color_argb,
    int32_t origin_x, int32_t origin_y,
    ZiliuTextRasterBgraSurface* destination) {
  if (coverage == nullptr || destination == nullptr ||
      coverage->struct_size < sizeof(ZiliuTextRasterCoverageBitmap) ||
      destination->struct_size < sizeof(ZiliuTextRasterBgraSurface) ||
      (coverage->pixels == nullptr &&
       (coverage->width != 0U || coverage->height != 0U)) ||
      destination->pixels == nullptr ||
      !ziliu::text_raster::detail::FitsBuffer(
          coverage->width, coverage->height, coverage->stride_bytes,
          coverage->capacity_bytes)) {
    return ZILIU_TEXT_RASTER_STATUS_INVALID_ARGUMENT;
  }
  const std::uint64_t destination_row_bytes =
      static_cast<std::uint64_t>(destination->width) * 4U;
  const std::uint64_t destination_required =
      static_cast<std::uint64_t>(destination->stride_bytes) *
      destination->height;
  if (destination_row_bytes > destination->stride_bytes ||
      destination_required > destination->capacity_bytes) {
    return ZILIU_TEXT_RASTER_STATUS_BUFFER_TOO_SMALL;
  }

  for (std::uint32_t source_y = 0; source_y < coverage->height; ++source_y) {
    const std::int64_t destination_y =
        static_cast<std::int64_t>(origin_y) + source_y;
    if (destination_y < 0 ||
        destination_y >= static_cast<std::int64_t>(destination->height)) {
      continue;
    }
    for (std::uint32_t source_x = 0; source_x < coverage->width; ++source_x) {
      const std::int64_t destination_x =
          static_cast<std::int64_t>(origin_x) + source_x;
      if (destination_x < 0 ||
          destination_x >= static_cast<std::int64_t>(destination->width)) {
        continue;
      }
      const std::uint8_t alpha =
          coverage->pixels[static_cast<std::size_t>(source_y) *
                               coverage->stride_bytes +
                           source_x];
      if (alpha == 0U) {
        continue;
      }
      std::uint8_t* pixel =
          destination->pixels +
          static_cast<std::size_t>(destination_y) *
              destination->stride_bytes +
          static_cast<std::size_t>(destination_x) * 4U;
      const ziliu::text_raster::detail::PremultipliedBgra source =
          ziliu::text_raster::detail::ResolveGlyphPixel(color_argb, alpha);
      const ziliu::text_raster::detail::PremultipliedBgra existing{
          .blue = pixel[0],
          .green = pixel[1],
          .red = pixel[2],
          .alpha = pixel[3],
      };
      const auto output =
          ziliu::text_raster::detail::SourceOver(source, existing);
      pixel[0] = output.blue;
      pixel[1] = output.green;
      pixel[2] = output.red;
      pixel[3] = output.alpha;
    }
  }
  return ZILIU_TEXT_RASTER_STATUS_OK;
}

void ZILIU_TEXT_RASTER_CALL
ZiliuTextRasterDestroy(ZiliuTextRasterHandle handle) {
  delete handle;
}

}  // extern "C"
