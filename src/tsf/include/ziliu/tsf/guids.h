#pragma once

#include <windows.h>

namespace ziliu::tsf {

// {7B9C1D3D-9D4E-4F02-A9A6-3EBA99CDE7B1}
inline constexpr GUID kTextServiceClsid = {0x7b9c1d3d,
                                          0x9d4e,
                                          0x4f02,
                                          {0xa9, 0xa6, 0x3e, 0xba, 0x99, 0xcd, 0xe7, 0xb1}};

// {58072F74-ED8B-44B7-AB46-B36D23608934}
inline constexpr GUID kSimplifiedChineseProfileGuid = {
    0x58072f74, 0xed8b, 0x44b7, {0xab, 0x46, 0xb3, 0x6d, 0x23, 0x60, 0x89, 0x34}};

// {7A8AD482-7415-40D6-8692-285432117C07}
inline constexpr GUID kInputModeLangBarItemGuid = {
    0x7a8ad482, 0x7415, 0x40d6, {0x86, 0x92, 0x28, 0x54, 0x32, 0x11, 0x7c, 0x07}};

inline constexpr LANGID kSimplifiedChineseLanguageId = 0x0804;

}  // namespace ziliu::tsf
