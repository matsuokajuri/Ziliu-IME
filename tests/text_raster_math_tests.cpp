#include "text_raster_math.h"

#include <cstdint>

namespace {

using ziliu::text_raster::detail::DivideByteProductBy255;
using ziliu::text_raster::detail::FitsBuffer;
using ziliu::text_raster::detail::IsUnicodeScalar;
using ziliu::text_raster::detail::PremultipliedBgra;
using ziliu::text_raster::detail::ResolveGlyphPixel;
using ziliu::text_raster::detail::SourceOver;

static_assert(IsUnicodeScalar(0U));
static_assert(IsUnicodeScalar(0x10FFFFU));
static_assert(!IsUnicodeScalar(0xD800U));
static_assert(!IsUnicodeScalar(0x110000U));

static_assert(DivideByteProductBy255(128U, 128U) == 64U);
static_assert(FitsBuffer(8U, 4U, 8U, 32U));
static_assert(!FitsBuffer(8U, 4U, 7U, 32U));
static_assert(!FitsBuffer(8U, 4U, 8U, 31U));

constexpr PremultipliedBgra kGlyph =
    ResolveGlyphPixel(0x80804020U, 128U);
static_assert(kGlyph.blue == 8U);
static_assert(kGlyph.green == 16U);
static_assert(kGlyph.red == 32U);
static_assert(kGlyph.alpha == 64U);

constexpr PremultipliedBgra kComposite =
    SourceOver(kGlyph, PremultipliedBgra{10U, 20U, 30U, 40U});
static_assert(kComposite.blue == 15U);
static_assert(kComposite.green == 30U);
static_assert(kComposite.red == 54U);
static_assert(kComposite.alpha == 93U);

}  // namespace

int main() {
  return 0;
}
