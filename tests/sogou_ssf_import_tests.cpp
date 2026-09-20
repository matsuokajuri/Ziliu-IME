#include "../src/settings/sogou_theme_import.h"
#include "../src/settings/sogou_ssf_container.h"
#include <windows.h>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <span>
#include <vector>

namespace {
using Bytes = std::vector<std::uint8_t>;
void Expect(bool ok, const char* message) { if (!ok) { std::cerr << "FAILED: " << message << '\n'; std::exit(1); } }
void U16(Bytes& b, std::size_t v) { b.push_back(static_cast<std::uint8_t>(v)); b.push_back(static_cast<std::uint8_t>(v >> 8)); }
void U32(Bytes& b, std::size_t v) { U16(b, v); U16(b, v >> 16); }
std::uint32_t Crc(const Bytes& b) {
  std::uint32_t c = 0xffffffffU;
  for (const auto x : b) { c ^= x; for (int i = 0; i < 8; ++i) c = (c >> 1) ^ ((c & 1U) ? 0xedb88320U : 0U); }
  return c ^ 0xffffffffU;
}
Bytes Utf16(std::wstring_view s) { Bytes b{0xff,0xfe}; for (auto c : s) U16(b,c); return b; }
Bytes Utf16Big(std::wstring_view s) { Bytes b{0xfe,0xff}; for (auto c : s) { b.push_back(static_cast<std::uint8_t>(c >> 8)); b.push_back(static_cast<std::uint8_t>(c)); } return b; }
Bytes Utf8Bom(std::string_view s) { Bytes b{0xef,0xbb,0xbf}; b.insert(b.end(),s.begin(),s.end()); return b; }
Bytes Zip(const std::vector<std::pair<std::string, Bytes>>& files, bool deflate) {
  Bytes b, central;
  for (const auto& [name, data] : files) {
    Bytes packed = data;
    if (deflate) { packed = {1}; U16(packed,data.size()); U16(packed,data.size() ^ 0xffffU); packed.insert(packed.end(),data.begin(),data.end()); }
    const auto offset = b.size();
    U32(b,0x04034b50); U16(b,20); U16(b,0); U16(b,deflate?8:0); U32(b,0); U32(b,Crc(data)); U32(b,packed.size()); U32(b,data.size()); U16(b,name.size()); U16(b,0);
    b.insert(b.end(),name.begin(),name.end()); b.insert(b.end(),packed.begin(),packed.end());
    U32(central,0x02014b50); U16(central,20); U16(central,20); U16(central,0); U16(central,deflate?8:0); U32(central,0); U32(central,Crc(data)); U32(central,packed.size()); U32(central,data.size()); U16(central,name.size()); U16(central,0); U16(central,0); U16(central,0); U16(central,0); U32(central,0); U32(central,offset); central.insert(central.end(),name.begin(),name.end());
  }
  const auto offset = b.size(); b.insert(b.end(),central.begin(),central.end());
  U32(b,0x06054b50); U16(b,0); U16(b,0); U16(b,files.size()); U16(b,files.size()); U32(b,central.size()); U32(b,offset); U16(b,0);
  return b;
}
void Write(const std::filesystem::path& p, const Bytes& b) {
  std::ofstream f(p,std::ios::binary); f.write(reinterpret_cast<const char*>(b.data()),static_cast<std::streamsize>(b.size())); f.close(); Expect(static_cast<bool>(f),"write fixture");
}
}

int wmain(int argc, wchar_t** argv) {
  if (argc == 4 && std::wstring_view(argv[1]) == L"--install") {
    const auto r = ziliu::settings::InstallSogouSsfPackage(argv[2],argv[3]);
    if (!r.ok()) { std::wcerr << r.error << '\n'; return 1; }
    std::cout << r.manifest->id << '\n'; return 0;
  }
  Expect(argc == 1,"invalid test arguments");
  const auto root = std::filesystem::temp_directory_path() / (L"ZiliuSsfImportTest-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
  Expect(std::filesystem::create_directory(root),"fresh test root");
  const auto archive = root / L"真实主题.ssf", installed = root / "themes";
  const Bytes ini = Utf16(L"[General]\nskin_name=new\nskin_version=0.9\nskin_author=匿名\nskin_info=欢迎大家使用\n[Display]\nfont_size=20\nuse_gdip=1\n[Scheme_H1]\npic=skin1.gif\npinyin_marge=4,3,2,1\n");
  const Bytes gif = {'G','I','F','8','9','a',1,0,1,0,0x80,0,0,0,0,0,255,255,255,0x21,0xf9,4,1,0,0,0,0,0x2c,0,0,0,0,1,0,1,0,0,2,2,0x44,1,0,0x3b};
  auto zip = Zip({{"skin.ini",ini},{"skin1.gif",gif}},false);
  Write(archive,zip);
  const auto decoded = ziliu::settings::DecodeSogouSsfArchive(archive);
  Expect(decoded.ok() && decoded.entries.size()==2 && decoded.package_sha256.size()==64,"decode ZIP and bind source SHA");
  auto result = ziliu::settings::InstallSogouSsfPackage(archive,installed);
  if (!result.ok()) std::wcerr << result.error << '\n';
  Expect(result.ok(),"install ZIP SSF using real product installer");
  Expect(result.manifest->id=="sogou."+decoded.package_sha256 && result.manifest->light.typography.sogou_use_gdip==1U,"preserve identity and display flag");
  Expect(result.manifest->name=="真实主题" && result.manifest->author=="未提供" && result.manifest->version=="未提供" && result.manifest->description.empty(),"replace the complete authoring template quartet with honest package metadata");
  Expect(result.manifest->light.horizontal.background.has_value(),"retain H1 surface");
  Expect(std::filesystem::exists(installed/result.manifest->id/result.manifest->light.horizontal.background->asset),"convert referenced GIF into installed PNG");
  Expect(!ziliu::settings::InstallSogouSsfPackage(archive,installed).ok(),"do not overwrite existing package");
  Write(archive,Zip({{"skin.ini",ini},{"skin1.gif",gif}},true));
  Expect(ziliu::settings::InstallSogouSsfPackage(archive,installed).ok(),"raw DEFLATE ZIP supported");
  const auto explicit_archive = root / L"文件名不应覆盖.ssf";
  const Bytes explicit_ini = Utf8Bom("[General]\nskin_name=Explicit Name\nskin_version=7.4\nskin_author=Explicit Author\n[Scheme_H1]\npic=skin1.gif\n");
  Write(explicit_archive,Zip({{"skin.ini",explicit_ini},{"skin1.gif",gif}},false));
  const auto explicit_result = ziliu::settings::InstallSogouSsfPackage(explicit_archive,installed);
  Expect(explicit_result.ok() && explicit_result.manifest->name=="Explicit Name" && explicit_result.manifest->author=="Explicit Author" && explicit_result.manifest->version=="7.4","UTF-8 BOM input preserves explicit metadata over the filename");
  const auto missing_archive = root / L"缺少字段.ssf";
  const Bytes missing_ini = Utf16Big(L"[General]\nskin_name=保留名称\n[Scheme_H1]\npic=skin1.gif\n");
  Write(missing_archive,Zip({{"skin.ini",missing_ini},{"skin1.gif",gif}},false));
  const auto missing_result = ziliu::settings::InstallSogouSsfPackage(missing_archive,installed);
  Expect(missing_result.ok() && missing_result.manifest->name=="保留名称" && missing_result.manifest->author=="未提供" && missing_result.manifest->version=="未提供","UTF-16BE input keeps explicit fields and marks missing metadata honestly");
  zip[38] ^= 1; Write(archive,zip);
  Expect(!ziliu::settings::DecodeSogouSsfArchive(archive).ok(),"reject CRC mismatch");
  for (const auto* bad : {"../skin.ini","C:/skin.ini","CON","folder./skin.ini"}) {
    Write(archive,Zip({{bad,ini},{"skin1.gif",gif}},false));
    Expect(!ziliu::settings::DecodeSogouSsfArchive(archive).ok(),"reject unsafe archive paths");
  }
  Write(archive,Zip({{"skin.ini",ini},{"SKIN.INI",ini}},false));
  Expect(!ziliu::settings::DecodeSogouSsfArchive(archive).ok(),"reject case alias collision");
  Write(archive,Zip({{"skin.ini",{0xff,0xfe,0}},{"skin1.gif",gif}},false));
  Expect(!ziliu::settings::InstallSogouSsfPackage(archive,installed).ok(),"reject malformed UTF16 without publishing");
  for (const auto& e : std::filesystem::directory_iterator(installed))
    Expect(!e.path().filename().string().starts_with(".import-ssf-"),"failed installation removes only its own staging directory");
  std::filesystem::remove_all(root);
  std::cout << "SSF import tests PASS\n";
  return 0;
}
