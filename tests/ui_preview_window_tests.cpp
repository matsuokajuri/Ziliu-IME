#include "ziliu/ui/candidate_window.h"

#include <cstdlib>
#include <iostream>
#include <string_view>
#include <thread>

namespace {
void Expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

HWND CreateHiddenOwner() {
  HWND owner = CreateWindowExW(0, L"STATIC", L"Ziliu hidden preview regression",
                               WS_OVERLAPPEDWINDOW, 80, 100, 800, 500, nullptr, nullptr,
                               GetModuleHandleW(nullptr), nullptr);
  Expect(owner != nullptr && !IsWindowVisible(owner), "test owner must remain hidden");
  return owner;
}

void PaintHiddenChild(HWND child, HWND owner) {
  Expect(GetParent(child) == owner && !IsWindowVisible(owner), "never paint a desktop popup");
  InvalidateRect(child, nullptr, FALSE);
  SendMessageW(child, WM_PAINT, 0, 0);
  Expect(!IsWindowVisible(child), "the preview must inherit the hidden owner's visibility");
}

void PumpFor(ULONGLONG milliseconds) {
  const auto started = GetTickCount64();
  do {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    Sleep(1);
  } while (GetTickCount64() - started < milliseconds);
}

LONG WindowWidth(HWND window) {
  RECT bounds{};
  Expect(GetWindowRect(window, &bounds) != FALSE, "read popup rectangle");
  return bounds.right - bounds.left;
}

void CheckRealWidthAnimation() {
  // Never show this popup on the input desktop. No SwitchDesktop, input injection,
  // registration, or VM is involved; the private desktop dies with this test.
  HDESK original = GetThreadDesktop(GetCurrentThreadId());
  const std::wstring name = L"ZiliuWidthTest-" + std::to_wstring(GetCurrentProcessId());
  HDESK isolated = CreateDesktopW(name.c_str(), nullptr, nullptr, 0, GENERIC_ALL, nullptr);
  Expect(isolated != nullptr && SetThreadDesktop(isolated), "attach test thread to private desktop");
  Expect(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "initialize isolated COM");
  HWND owner = CreateHiddenOwner();
  {
    ziliu::ui::CandidateWindow popup;
    Expect(popup.Create(owner), "create isolated candidate popup");
    HWND window = FindWindowW(L"Ziliu.CandidateWindow.v1", nullptr);
    Expect(window != nullptr, "find popup on private desktop");
    ziliu::core::Settings settings;
    settings.theme_mode = ziliu::core::ThemeMode::kLight;
    settings.active_theme_id = ziliu::core::kDefaultThemeId;
    settings.candidate_count = 3;
    settings.candidate_layout = ziliu::core::CandidateLayout::kHorizontal;
    ziliu::core::CompositionSnapshot narrow;
    narrow.preedit = L"ni'hao";
    narrow.candidates = {{L"one", L"", 1.0}, {L"two", L"", 0.9}, {L"six", L"", 0.8}};
    auto wide = narrow;
    wide.candidates[0].text = L"a much longer first candidate";
    const RECT caret{100, 100, 102, 120};
    popup.Show(narrow, caret, settings, 0);
    PumpFor(200);
    const LONG narrow_width = WindowWidth(window);
    BOOL animate = FALSE;
    const bool enabled = SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animate, 0) && animate;
    popup.Show(wide, caret, settings, 0);
    if (enabled) {
      Expect(WindowWidth(window) == narrow_width, "new content must not snap the popup width");
    }
    PumpFor(60);
    const LONG intermediate_width = WindowWidth(window);
    // An identical snapshot must preserve the running transition's deadline.
    popup.Show(wide, caret, settings, 0);
    PumpFor(150);
    const LONG wide_width = WindowWidth(window);
    Expect(wide_width > narrow_width, "fixture produces genuinely different widths");
    if (enabled) {
      Expect(narrow_width < intermediate_width && intermediate_width < wide_width,
             "real layered HWND must pass through an intermediate width after painting");
      popup.Show(narrow, caret, settings, 0);
      Expect(WindowWidth(window) == wide_width, "shrinking starts without a jump");
      PumpFor(60);
      const LONG shrinking_width = WindowWidth(window);
      Expect(narrow_width < shrinking_width && shrinking_width < wide_width, "shrinking is visible");
      popup.Show(wide, caret, settings, 0);
      Expect(WindowWidth(window) == shrinking_width, "rapid reversal continues at the displayed width");
      PumpFor(220);
      Expect(WindowWidth(window) == wide_width, "reversed animation reaches its exact target");
    } else {
      Expect(intermediate_width == wide_width, "Windows disabled animations must snap immediately");
    }
    popup.Show(narrow, caret, settings, 0);
    PumpFor(30);
    popup.Hide();
    PumpFor(150);
    Expect(!IsWindowVisible(window), "hide during resize must terminate and leave no visible popup");
    std::cout << "Isolated layered HWND width transition checks PASS; animation enabled=" << enabled << '\n';
  }
  DestroyWindow(owner);
  CoUninitialize();
  Expect(SetThreadDesktop(original) != FALSE, "detach private test desktop");
  Expect(CloseDesktop(isolated) != FALSE, "release private test desktop");
}
}  // namespace

int main() {
  Expect(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "initialize COM");
  HWND owner = CreateHiddenOwner();
  {
    ziliu::ui::CandidateWindow preview;
    Expect(!preview.CreatePreview(nullptr), "a preview requires a valid owner");
    Expect(preview.CreatePreview(owner), "create the native preview");
    HWND child = FindWindowExW(owner, nullptr, L"Ziliu.CandidatePreview.v1", nullptr);
    Expect(child != nullptr && GetParent(child) == owner, "preview must be a child of Settings");
    const LONG_PTR style = GetWindowLongPtrW(child, GWL_STYLE);
    Expect((style & WS_CHILD) != 0 && (style & WS_POPUP) == 0,
           "reject the old detached popup path before any show operation");

    ziliu::core::Settings settings;
    settings.theme_mode = ziliu::core::ThemeMode::kLight;
    settings.active_theme_id = ziliu::core::kDefaultThemeId;
    settings.candidate_count = 3;
    settings.candidate_layout = ziliu::core::CandidateLayout::kHorizontal;
    ziliu::core::CompositionSnapshot snapshot;
    snapshot.preedit = L"ni'hao";
    snapshot.candidates = {{L"你好", L"", 1.0}, {L"世界", L"", 0.9}, {L"示例", L"", 0.8}};
    const RECT host_bounds{20, 30, 720, 250};
    preview.ShowPreview(snapshot, host_bounds, settings, 0);
    RECT before_paint{};
    GetWindowRect(child, &before_paint);
    PaintHiddenChild(child, owner);
    RECT after_paint{};
    GetWindowRect(child, &after_paint);
    Expect(EqualRect(&before_paint, &after_paint),
           "layered presentation must not reinterpret owner-client coordinates as screen coordinates");
    RECT relative = after_paint;
    MapWindowPoints(nullptr, owner, reinterpret_cast<POINT*>(&relative), 2);
    Expect(relative.left >= host_bounds.left && relative.top >= host_bounds.top &&
               relative.right <= host_bounds.right && relative.bottom <= host_bounds.bottom,
           "preview stays within its XAML host bounds");

    RECT owner_before{};
    GetWindowRect(owner, &owner_before);
    SetWindowPos(owner, nullptr, owner_before.left + 140, owner_before.top + 70, 0, 0,
                  SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
    PaintHiddenChild(child, owner);
    RECT moved{};
    GetWindowRect(child, &moved);
    Expect(moved.left - after_paint.left == 140 && moved.top - after_paint.top == 70,
           "preview must move with Settings without a layout refresh");
    Expect(SendMessageW(child, WM_NCHITTEST, 0, 0) == HTTRANSPARENT,
           "preview must leave pointer input to the XAML page");

    const RECT viewport{relative.left + 13, relative.top + 7,
                        relative.right - 11, relative.bottom - 5};
    preview.ShowPreview(snapshot, host_bounds, settings, 0, &viewport);
    HRGN region = CreateRectRgn(0, 0, 0, 0);
    Expect(GetWindowRgn(child, region) != ERROR, "preview must expose a viewport clipping region");
    RECT clipped{};
    GetRgnBox(region, &clipped);
    DeleteObject(region);
    const RECT expected{13, 7, relative.right - relative.left - 11,
                        relative.bottom - relative.top - 5};
    Expect(EqualRect(&clipped, &expected), "scroll clipping must use child-local pixels");
    const RECT outside{0, 0, 1, 1};
    preview.ShowPreview(snapshot, host_bounds, settings, 0, &outside);
    Expect((GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE) == 0,
           "a preview outside the viewport must be hidden");
    preview.ShowPreview(snapshot, host_bounds, settings, 0);
    preview.Hide();
    Expect((GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE) == 0,
           "leaving Appearance must hide the preview");
    DestroyWindow(owner);
    Expect(!IsWindow(child), "closing Settings must destroy its preview");
    owner = CreateHiddenOwner();
    Expect(preview.CreatePreview(owner), "preview can be recreated after owner destruction");
  }
  Expect(FindWindowExW(owner, nullptr, L"Ziliu.CandidatePreview.v1", nullptr) == nullptr,
         "preview destruction leaves no orphan window");
  DestroyWindow(owner);
  CoUninitialize();
  std::thread animation_test(CheckRealWidthAnimation);
  animation_test.join();
  std::cout << "Hidden preview parent, move, clipping, input and destruction checks PASS\n";
  return EXIT_SUCCESS;
}
