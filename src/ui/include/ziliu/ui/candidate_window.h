#pragma once

#include "ziliu/core/engine.h"
#include "ziliu/core/settings.h"
#include "ziliu/core/theme_manifest.h"

#include <d2d1.h>
#include <dwrite.h>
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <filesystem>
#include <functional>
#include <string_view>
#include <vector>

namespace ziliu::ui {

class CandidateWindow final {
 public:
  CandidateWindow() = default;
  ~CandidateWindow();

  CandidateWindow(const CandidateWindow&) = delete;
  CandidateWindow& operator=(const CandidateWindow&) = delete;

  bool Create(HWND owner);
  bool CreatePreview(HWND owner);
  void Show(const core::CompositionSnapshot& snapshot, const RECT& text_rectangle,
            const core::Settings& settings, std::size_t page_offset);
  void ShowPreview(const core::CompositionSnapshot& snapshot, const RECT& preview_bounds,
                   const core::Settings& settings, std::size_t page_offset,
                   const RECT* viewport_bounds = nullptr);
  void SetExpanded(bool expanded);
  void SetQuickMenuAction(std::function<void(POINT)> action);
  void Hide();

 static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wparam,
                                          LPARAM lparam);

 private:
  struct RenderPalette {
    D2D1_COLOR_F preedit{};
    D2D1_COLOR_F highlighted_candidate{};
    D2D1_COLOR_F candidate{};
    D2D1_COLOR_F background{};
    D2D1_COLOR_F muted{};
    D2D1_COLOR_F highlighted_background{};
    D2D1_COLOR_F separator{};
    D2D1_COLOR_F surface_border{};
  };

  struct ButtonBitmaps {
    Microsoft::WRL::ComPtr<ID2D1Bitmap> normal;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> hover;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> pressed;
  };

  struct SurfaceBitmaps {
    Microsoft::WRL::ComPtr<ID2D1Bitmap> background;
    struct OverlayBitmap {
      core::ThemeOverlay overlay;
      Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
    };
    std::vector<OverlayBitmap> overlays;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> separator;
    ButtonBitmaps previous;
    ButtonBitmaps next;
    ButtonBitmaps expand;
    ButtonBitmaps collapse;
    ButtonBitmaps menu;
  };

  struct ScaledInsets {
    float left = 0.0F;
    float top = 0.0F;
    float right = 0.0F;
    float bottom = 0.0F;
  };

  bool CreateInternal(HWND owner, bool preview);
  void ShowInternal(const core::CompositionSnapshot& snapshot, const RECT& text_rectangle,
                    const RECT* preview_bounds, const core::Settings& settings,
                    std::size_t page_offset);
  LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);
  void RefreshTheme(std::string_view theme_id);
  [[nodiscard]] bool UsesSogouRendering() const noexcept;
  [[nodiscard]] bool UsesNativeDefaultTheme() const noexcept;
  void ApplyWindowRenderingMode();
  [[nodiscard]] const core::ThemeAppearance& ActiveThemeAppearance() const;
  [[nodiscard]] const core::ThemeSurface& ActiveThemeSurface() const;
  [[nodiscard]] RenderPalette ResolveRenderPalette() const;
  [[nodiscard]] float CoordinateScale() const noexcept;
  [[nodiscard]] float ThemeUnitScale() const;
  bool EnsureImagingFactory();
  [[nodiscard]] Microsoft::WRL::ComPtr<ID2D1Bitmap> LoadThemeBitmap(
      std::string_view asset) const;
  [[nodiscard]] ButtonBitmaps LoadButtonBitmaps(
      const std::optional<core::ThemeButtonImages>& images) const;
  void LoadSurfaceBitmaps();
  bool EnsureDeviceResources();
  bool EnsureLayeredSurface(UINT32 width, UINT32 height);
  void ReleaseLayeredSurface();
  [[nodiscard]] bool PresentLayeredSurface();
  void DrawSurfaceBackground();
  void DrawSurfaceOverlays();
  void DrawSurfaceSeparator(float y);
  [[nodiscard]] bool DrawThemeButton(const ButtonBitmaps& bitmaps,
                                     const D2D1_RECT_F& bounds, bool hovered,
                                     bool pressed);
  void Paint();
  void DiscardDeviceResources();

  HWND window_ = nullptr;
  core::CompositionSnapshot snapshot_;
  core::Settings settings_;
  RECT text_rectangle_{};
  std::size_t page_offset_ = 0;
  float window_width_ = 420.0F;
  float dpi_scale_ = 1.0F;
  float layout_scale_ = 1.0F;
  float preedit_height_ = 42.0F;
  float candidate_row_height_ = 38.0F;
  ScaledInsets preedit_insets_;
  ScaledInsets candidate_insets_;
  bool dark_theme_ = false;
  bool dark_theme_initialized_ = false;
  bool theme_initialized_ = false;
  bool preview_mode_ = false;
  bool expanded_ = false;
  bool can_expand_ = false;
  bool sogou_fallback_actions_ = false;
  bool sogou_vertical_fallback_pager_ = false;
  std::function<void(POINT)> quick_menu_action_;
  std::vector<std::size_t> candidate_indices_;
  std::vector<float> candidate_widths_;
  std::vector<float> candidate_lefts_;
  std::vector<float> candidate_tops_;
  core::ThemeManifest theme_manifest_;
  std::filesystem::path theme_directory_;
  D2D1_RECT_F expand_button_bounds_{};
  D2D1_RECT_F menu_button_bounds_{};
  bool expand_button_hovered_ = false;
  bool menu_button_hovered_ = false;
  bool expand_button_pressed_ = false;
  bool menu_button_pressed_ = false;
  bool tracking_mouse_leave_ = false;
  bool layered_rendering_enabled_ = false;
  bool layered_present_retry_attempted_ = false;
  Microsoft::WRL::ComPtr<ID2D1Factory> d2d_factory_;
  Microsoft::WRL::ComPtr<IDWriteFactory> dwrite_factory_;
  Microsoft::WRL::ComPtr<IWICImagingFactory> imaging_factory_;
  Microsoft::WRL::ComPtr<ID2D1RenderTarget> render_target_;
  Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> hwnd_render_target_;
  Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> dc_render_target_;
  HDC layered_memory_dc_ = nullptr;
  HBITMAP layered_bitmap_ = nullptr;
  HGDIOBJ layered_previous_bitmap_ = nullptr;
  SIZE layered_pixel_size_{};
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> text_brush_;
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> preedit_brush_;
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> highlighted_text_brush_;
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> muted_brush_;
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> accent_brush_;
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> separator_brush_;
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> background_brush_;
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> surface_border_brush_;
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> sogou_action_fill_brush_;
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> sogou_action_border_brush_;
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> sogou_action_icon_brush_;
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> sogou_action_separator_brush_;
  SurfaceBitmaps surface_bitmaps_;
  Microsoft::WRL::ComPtr<IDWriteTextFormat> preedit_format_;
  Microsoft::WRL::ComPtr<IDWriteTextLayout> native_preedit_layout_;
  D2D1_POINT_2F native_preedit_origin_offset_{};
  Microsoft::WRL::ComPtr<IDWriteTextFormat> preedit_caret_format_;
  Microsoft::WRL::ComPtr<IDWriteTextFormat> candidate_format_;
  Microsoft::WRL::ComPtr<IDWriteTextFormat> candidate_number_format_;
  Microsoft::WRL::ComPtr<IDWriteTextFormat> annotation_format_;
};

}  // namespace ziliu::ui
