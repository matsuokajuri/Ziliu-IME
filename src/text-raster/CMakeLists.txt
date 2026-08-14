set(_ziliu_freetype_required_files
  CMakeLists.txt
  LICENSE.TXT
  docs/FTL.TXT
  include/ft2build.h
  src/autofit/autofit.c
  src/base/ftbase.c
  src/base/ftbitmap.c
  src/base/ftglyph.c
  src/base/ftinit.c
  src/base/ftmm.c
  src/cff/cff.c
  src/gzip/ftgzip.c
  src/psaux/psaux.c
  src/pshinter/pshinter.c
  src/psnames/psnames.c
  src/sfnt/sfnt.c
  src/smooth/smooth.c
  src/truetype/truetype.c
  builds/windows/ftdebug.c
  builds/windows/ftsystem.c)

foreach(_required_file IN LISTS _ziliu_freetype_required_files)
  if(NOT EXISTS "${ZILIU_FREETYPE_SOURCE_DIR}/${_required_file}")
    message(FATAL_ERROR
      "Verified FreeType 2.14.3 source is incomplete: "
      "${ZILIU_FREETYPE_SOURCE_DIR}/${_required_file}")
  endif()
endforeach()

add_library(ziliu_freetype STATIC
  "${ZILIU_FREETYPE_SOURCE_DIR}/src/autofit/autofit.c"
  "${ZILIU_FREETYPE_SOURCE_DIR}/src/base/ftbase.c"
  "${ZILIU_FREETYPE_SOURCE_DIR}/src/base/ftbitmap.c"
  "${ZILIU_FREETYPE_SOURCE_DIR}/src/base/ftglyph.c"
  "${ZILIU_FREETYPE_SOURCE_DIR}/src/base/ftinit.c"
  "${ZILIU_FREETYPE_SOURCE_DIR}/src/base/ftmm.c"
  "${ZILIU_FREETYPE_SOURCE_DIR}/src/cff/cff.c"
  "${ZILIU_FREETYPE_SOURCE_DIR}/src/gzip/ftgzip.c"
  "${ZILIU_FREETYPE_SOURCE_DIR}/src/psaux/psaux.c"
  "${ZILIU_FREETYPE_SOURCE_DIR}/src/pshinter/pshinter.c"
  "${ZILIU_FREETYPE_SOURCE_DIR}/src/psnames/psnames.c"
  "${ZILIU_FREETYPE_SOURCE_DIR}/src/sfnt/sfnt.c"
  "${ZILIU_FREETYPE_SOURCE_DIR}/src/smooth/smooth.c"
  "${ZILIU_FREETYPE_SOURCE_DIR}/src/truetype/truetype.c"
  "${ZILIU_FREETYPE_SOURCE_DIR}/builds/windows/ftdebug.c"
  "${ZILIU_FREETYPE_SOURCE_DIR}/builds/windows/ftsystem.c")

target_include_directories(ziliu_freetype
  PUBLIC "${ZILIU_FREETYPE_SOURCE_DIR}/include"
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(ziliu_freetype PRIVATE
  FT2_BUILD_LIBRARY
  "FT_CONFIG_MODULES_H=<freetype_modules.h>"
  _CRT_NONSTDC_NO_WARNINGS
  _CRT_SECURE_NO_WARNINGS)
if(MSVC)
  # FreeType 2.14.3 narrows bounded font-table sizes in upstream code. Keep
  # warnings-as-errors while suppressing only those two upstream conversions.
  target_compile_options(ziliu_freetype PRIVATE /W4 /WX /utf-8 /wd4244 /wd4267)
endif()
set_target_properties(ziliu_freetype PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(ZiliuTextRaster SHARED
  src/text_raster.cpp
  ZiliuTextRaster.def)
target_include_directories(ZiliuTextRaster
  PUBLIC include
  PRIVATE src)
target_compile_definitions(ZiliuTextRaster PRIVATE ZILIU_TEXT_RASTER_EXPORTS)
target_link_libraries(ZiliuTextRaster PRIVATE ziliu_freetype dwrite)
set_target_properties(ZiliuTextRaster PROPERTIES OUTPUT_NAME "ZiliuTextRaster")
ziliu_configure_target(ZiliuTextRaster)
