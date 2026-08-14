#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#define ZILIU_TEXT_RASTER_CALL __stdcall
#if defined(ZILIU_TEXT_RASTER_EXPORTS)
#define ZILIU_TEXT_RASTER_API __declspec(dllexport)
#else
#define ZILIU_TEXT_RASTER_API
#endif
#else
#define ZILIU_TEXT_RASTER_CALL
#define ZILIU_TEXT_RASTER_API
#endif

#define ZILIU_TEXT_RASTER_ABI_VERSION 1U

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ZiliuTextRasterContext* ZiliuTextRasterHandle;

typedef enum ZiliuTextRasterStatus {
  ZILIU_TEXT_RASTER_STATUS_OK = 0,
  ZILIU_TEXT_RASTER_STATUS_INVALID_ARGUMENT = 1,
  ZILIU_TEXT_RASTER_STATUS_OUT_OF_MEMORY = 2,
  ZILIU_TEXT_RASTER_STATUS_NOT_INITIALIZED = 3,
  ZILIU_TEXT_RASTER_STATUS_FONT_NOT_FOUND = 4,
  ZILIU_TEXT_RASTER_STATUS_FONT_LOAD_FAILED = 5,
  ZILIU_TEXT_RASTER_STATUS_GLYPH_NOT_FOUND = 6,
  ZILIU_TEXT_RASTER_STATUS_GLYPH_LOAD_FAILED = 7,
  ZILIU_TEXT_RASTER_STATUS_UNSUPPORTED_BITMAP = 8,
  ZILIU_TEXT_RASTER_STATUS_BUFFER_TOO_SMALL = 9,
  ZILIU_TEXT_RASTER_STATUS_INVALID_UTF16 = 10,
  ZILIU_TEXT_RASTER_STATUS_INTERNAL_ERROR = 11,
} ZiliuTextRasterStatus;

typedef enum ZiliuTextRasterFontStyle {
  ZILIU_TEXT_RASTER_FONT_STYLE_NORMAL = 0,
  ZILIU_TEXT_RASTER_FONT_STYLE_OBLIQUE = 1,
  ZILIU_TEXT_RASTER_FONT_STYLE_ITALIC = 2,
} ZiliuTextRasterFontStyle;

typedef enum ZiliuTextRasterHinting {
  ZILIU_TEXT_RASTER_HINTING_NATIVE = 0,
  ZILIU_TEXT_RASTER_HINTING_NONE = 1,
  ZILIU_TEXT_RASTER_HINTING_AUTO = 2,
} ZiliuTextRasterHinting;

typedef enum ZiliuTextRasterHintingTarget {
  ZILIU_TEXT_RASTER_TARGET_NORMAL = 0,
  ZILIU_TEXT_RASTER_TARGET_LIGHT = 1,
  ZILIU_TEXT_RASTER_TARGET_MONO = 2,
} ZiliuTextRasterHintingTarget;

typedef enum ZiliuTextRasterRenderMode {
  ZILIU_TEXT_RASTER_RENDER_NORMAL = 0,
  ZILIU_TEXT_RASTER_RENDER_LIGHT = 1,
  ZILIU_TEXT_RASTER_RENDER_MONO = 2,
} ZiliuTextRasterRenderMode;

typedef enum ZiliuTextRasterInvalidUtf16Policy {
  ZILIU_TEXT_RASTER_INVALID_UTF16_REJECT = 0,
  ZILIU_TEXT_RASTER_INVALID_UTF16_REPLACE = 1,
} ZiliuTextRasterInvalidUtf16Policy;

typedef struct ZiliuTextRasterFontConfig {
  uint32_t struct_size;
  const wchar_t* family_name;
  uint32_t weight;
  uint32_t stretch;
  uint32_t style;
  uint32_t pixel_width;
  uint32_t pixel_height;
} ZiliuTextRasterFontConfig;

typedef struct ZiliuTextRasterGlyphOptions {
  uint32_t struct_size;
  uint32_t hinting;
  uint32_t hinting_target;
  uint32_t render_mode;
} ZiliuTextRasterGlyphOptions;

typedef struct ZiliuTextRasterGlyphMetrics {
  uint32_t struct_size;
  uint32_t glyph_index;
  int32_t bitmap_left;
  int32_t bitmap_top;
  uint32_t bitmap_width;
  uint32_t bitmap_height;
  int32_t advance_x_26_6;
  int32_t advance_y_26_6;
  int32_t bearing_x_26_6;
  int32_t bearing_y_26_6;
  int32_t outline_width_26_6;
  int32_t outline_height_26_6;
} ZiliuTextRasterGlyphMetrics;

typedef struct ZiliuTextRasterCoverageBitmap {
  uint32_t struct_size;
  uint8_t* pixels;
  uint32_t capacity_bytes;
  uint32_t stride_bytes;
  uint32_t width;
  uint32_t height;
} ZiliuTextRasterCoverageBitmap;

typedef struct ZiliuTextRasterBgraSurface {
  uint32_t struct_size;
  uint8_t* pixels;
  uint32_t capacity_bytes;
  uint32_t width;
  uint32_t height;
  uint32_t stride_bytes;
} ZiliuTextRasterBgraSurface;

ZILIU_TEXT_RASTER_API uint32_t ZILIU_TEXT_RASTER_CALL
ZiliuTextRasterGetAbiVersion(void);

ZILIU_TEXT_RASTER_API ZiliuTextRasterStatus ZILIU_TEXT_RASTER_CALL
ZiliuTextRasterCreate(ZiliuTextRasterHandle* handle);

ZILIU_TEXT_RASTER_API ZiliuTextRasterStatus ZILIU_TEXT_RASTER_CALL
ZiliuTextRasterConfigureFont(ZiliuTextRasterHandle handle,
                              const ZiliuTextRasterFontConfig* config);

ZILIU_TEXT_RASTER_API ZiliuTextRasterStatus ZILIU_TEXT_RASTER_CALL
ZiliuTextRasterDecodeUtf16(const wchar_t* text, uint32_t text_length,
                           uint32_t invalid_policy, uint32_t* scalars,
                           uint32_t scalar_capacity,
                           uint32_t* scalar_count);

ZILIU_TEXT_RASTER_API ZiliuTextRasterStatus ZILIU_TEXT_RASTER_CALL
ZiliuTextRasterLookupGlyph(ZiliuTextRasterHandle handle,
                            uint32_t unicode_scalar,
                            uint32_t* glyph_index);

ZILIU_TEXT_RASTER_API ZiliuTextRasterStatus ZILIU_TEXT_RASTER_CALL
ZiliuTextRasterRenderGlyph(ZiliuTextRasterHandle handle, uint32_t glyph_index,
                            const ZiliuTextRasterGlyphOptions* options,
                            ZiliuTextRasterGlyphMetrics* metrics,
                            ZiliuTextRasterCoverageBitmap* bitmap);

ZILIU_TEXT_RASTER_API ZiliuTextRasterStatus ZILIU_TEXT_RASTER_CALL
ZiliuTextRasterCompositeCoverageBgra(
    const ZiliuTextRasterCoverageBitmap* coverage, uint32_t color_argb,
    int32_t origin_x, int32_t origin_y,
    ZiliuTextRasterBgraSurface* destination);

ZILIU_TEXT_RASTER_API void ZILIU_TEXT_RASTER_CALL
ZiliuTextRasterDestroy(ZiliuTextRasterHandle handle);

#ifdef __cplusplus
}
#endif
