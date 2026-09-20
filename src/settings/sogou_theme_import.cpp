#include "sogou_theme_import.h"
#include "sogou_ssf_container.h"
#include "ziliu/core/sogou_theme.h"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <fstream>
#include <span>
#include <vector>

namespace ziliu::settings {
namespace {
using Microsoft::WRL::ComPtr;

std::wstring Wide(std::string_view value) {
  if (value.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
  if (n <= 0) return {};
  std::wstring result(static_cast<std::size_t>(n), L'\0');
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), n) != n) return {};
  return result;
}

std::string Utf8(std::wstring_view value) {
  if (value.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                    static_cast<int>(value.size()), nullptr, 0,
                                    nullptr, nullptr);
  if (n <= 0) return {};
  std::string result(static_cast<std::size_t>(n), '\0');
  if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                          static_cast<int>(value.size()), result.data(), n,
                          nullptr, nullptr) != n) return {};
  return result;
}

std::string IniUtf8(std::span<const std::uint8_t> bytes) {
  if (bytes.empty() || bytes.size() > core::kMaximumSogouThemeIniBytes) return {};
  std::string text;
  if (bytes.size() >= 2 && ((bytes[0] == 0xff && bytes[1] == 0xfe) || (bytes[0] == 0xfe && bytes[1] == 0xff))) {
    if (bytes.size() % 2) return {};
    const bool little = bytes[0] == 0xff;
    std::wstring wide;
    for (std::size_t i = 2; i < bytes.size(); i += 2) {
      const unsigned v = little ? bytes[i] | (static_cast<unsigned>(bytes[i + 1]) << 8U)
                                : bytes[i + 1] | (static_cast<unsigned>(bytes[i]) << 8U);
      if (v == 0) return {};
      wide.push_back(static_cast<wchar_t>(v));
    }
    const int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    text.resize(static_cast<std::size_t>(n));
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), static_cast<int>(wide.size()), text.data(), n, nullptr, nullptr) != n) return {};
  } else {
    if (bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf) bytes = bytes.subspan(3);
    text.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (text.find('\0') != std::string::npos || Wide(text).empty()) return {};
  }
  return text;
}

bool WriteBytes(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  out.close();
  return static_cast<bool>(out);
}

bool SaveImage(IWICImagingFactory* factory, const std::vector<std::uint8_t>& bytes,
               const std::filesystem::path& path, std::uint64_t& total_pixels) {
  ComPtr<IWICStream> stream;
  ComPtr<IWICBitmapDecoder> decoder;
  ComPtr<IWICBitmapFrameDecode> frame;
  if (bytes.empty() || FAILED(factory->CreateStream(&stream)) ||
      FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size()))) ||
      FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder)) ||
      FAILED(decoder->GetFrame(0, &frame))) return false;
  UINT w = 0, h = 0;
  if (FAILED(frame->GetSize(&w, &h)) || !w || !h || w > core::kMaximumThemeImageDimension || h > core::kMaximumThemeImageDimension) return false;
  const auto pixels = static_cast<std::uint64_t>(w) * h;
  if (total_pixels + pixels > core::kMaximumThemeDecodedPixels) return false;
  total_pixels += pixels;
  GUID format{};
  if (FAILED(decoder->GetContainerFormat(&format))) return false;
  ComPtr<IWICFormatConverter> converted;
  WICPixelFormatGUID pixel_format = GUID_WICPixelFormat32bppBGRA;
  if (FAILED(factory->CreateFormatConverter(&converted)) ||
      FAILED(converted->Initialize(frame.Get(), pixel_format, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return false;
  // Validate compressed pixels too, not only the image header dimensions.
  std::vector<BYTE> verified_pixels(static_cast<std::size_t>(pixels) * 4U);
  if (FAILED(converted->CopyPixels(nullptr, w * 4U, static_cast<UINT>(verified_pixels.size()), verified_pixels.data()))) return false;
  if (format == GUID_ContainerFormatPng) return WriteBytes(path, bytes);
  ComPtr<IWICStream> output;
  ComPtr<IWICBitmapEncoder> encoder;
  ComPtr<IWICBitmapFrameEncode> encoded;
  if (FAILED(factory->CreateStream(&output)) || FAILED(output->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) ||
      FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
      FAILED(encoder->Initialize(output.Get(), WICBitmapEncoderNoCache)) ||
      FAILED(encoder->CreateNewFrame(&encoded, nullptr)) || FAILED(encoded->Initialize(nullptr)) ||
      FAILED(encoded->SetSize(w, h)) || FAILED(encoded->SetPixelFormat(&pixel_format)) ||
      FAILED(encoded->WriteSource(converted.Get(), nullptr)) || FAILED(encoded->Commit()) || FAILED(encoder->Commit())) return false;
  return true;
}

struct Staging {
  std::filesystem::path path;
  ~Staging() { if (!path.empty()) { std::error_code e; std::filesystem::remove_all(path, e); } }
};
struct Apartment {
  HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
};
}

SogouThemeImportResult InstallSogouSsfPackage(const std::filesystem::path& source,
                                            const std::filesystem::path& themes_directory) {
  const auto fail = [](std::wstring text) { return SogouThemeImportResult{std::nullopt, std::move(text)}; };
  try {
    if (source.empty() || themes_directory.empty()) return fail(L"SSF 安装路径为空。");
    std::error_code e;
    std::filesystem::create_directories(themes_directory, e);
    if (e) return fail(L"无法创建主题目录。");
    GUID guid{}; wchar_t name[40]{};
    if (FAILED(CoCreateGuid(&guid)) || StringFromGUID2(guid, name, 40) == 0) return fail(L"无法生成安装标识。");
    const auto temporary = themes_directory / (std::wstring(L".import-ssf-") + name);
    if (!std::filesystem::create_directory(temporary, e) || e) return fail(L"无法创建新的临时目录。");
    Staging staging{temporary};
    const auto frozen = temporary / L"package.ssf";
    if (!std::filesystem::copy_file(source, frozen, std::filesystem::copy_options::none, e) || e) return fail(L"无法读取 SSF。");
    auto decoded = DecodeSogouSsfArchive(frozen);
    if (!decoded.ok()) return fail(Wide(decoded.error));
    std::filesystem::remove(frozen, e);
    if (e) return fail(L"无法完成临时包校验。");
    const auto ini = std::find_if(decoded.entries.begin(), decoded.entries.end(), [](const auto& v) { return _stricmp(v.relative_path.c_str(), "skin.ini") == 0; });
    if (ini == decoded.entries.end()) return fail(L"SSF 缺少根目录 skin.ini。");
    const auto text = IniUtf8(ini->bytes);
    if (text.empty()) return fail(L"skin.ini 不是有效 UTF-8 或带 BOM 的 UTF-16。");
    const auto source_hint = Utf8(source.filename().wstring());
    if (source_hint.empty()) return fail(L"SSF 文件名不是有效 Unicode。");
    auto converted = core::ConvertSogouThemeIni(text, source_hint);
    if (!converted.ok()) return fail(Wide(converted.issues.front().path + ": " + converted.issues.front().message));
    if (decoded.package_sha256.size() != 64) return fail(L"SSF 包标识无效。");
    converted.manifest.id = "sogou." + decoded.package_sha256;
    if (!core::ValidateThemeManifest(converted.manifest).empty()) return fail(L"SSF 转换结果无效。");
    const auto destination = themes_directory / converted.manifest.id;
    if (std::filesystem::exists(destination, e) || e) return fail(L"这个 SSF 已安装；现有主题未被覆盖。");
    Apartment apartment;
    if (FAILED(apartment.result) && apartment.result != RPC_E_CHANGED_MODE) return fail(L"无法初始化图像转换。");
    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) return fail(L"无法创建图像解码器。");
    std::uint64_t pixels = 0, installed_bytes = 0;
    for (const auto& asset : converted.assets) {
      const auto entry = std::find_if(decoded.entries.begin(), decoded.entries.end(), [&](const auto& v) {
        return _stricmp(v.relative_path.c_str(), asset.source_path.c_str()) == 0;
      });
      if (entry == decoded.entries.end() || !core::IsSafeThemeAssetPath(asset.target_path)) return fail(L"SSF 引用的图片不存在或路径无效。");
      const auto output = temporary / Wide(asset.target_path);
      std::filesystem::create_directories(output.parent_path(), e);
      if (e || !SaveImage(factory.Get(), entry->bytes, output, pixels)) return fail(L"SSF 图片无效或超过解码限制。");
      const auto size = std::filesystem::file_size(output, e);
      if (e || size > core::kMaximumThemeAssetBytes || installed_bytes + size > core::kMaximumThemePackageBytes) return fail(L"SSF 转换图片超过大小限制。");
      installed_bytes += size;
    }
    const auto manifest = core::SerializeThemeManifest(converted.manifest);
    if (manifest.size() > core::kMaximumThemeManifestBytes ||
        !WriteBytes(temporary / "manifest.json", std::span(reinterpret_cast<const std::uint8_t*>(manifest.data()), manifest.size()))) return fail(L"无法写入主题清单。");
    std::filesystem::rename(temporary, destination, e);
    if (e) return fail(L"无法完成主题安装，现有主题未修改。");
    staging.path.clear();
    return {std::move(converted.manifest), {}};
  } catch (...) {
    return fail(L"SSF 导入失败；未发布的临时文件已清理。");
  }
}
}
