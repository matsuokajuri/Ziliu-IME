#pragma once

#include "ziliu/core/engine.h"
#include "ziliu/core/settings.h"

#include <d2d1.h>
#include <dwrite.h>
#include <windows.h>
#include <wrl/client.h>

#include <vector>

namespace ziliu::ui {

class CandidateWindow final {
 public:
  CandidateWindow() = default;
  ~CandidateWindow();

  CandidateWindow(const CandidateWindow&) = delete;
  CandidateWindow& operator=(const CandidateWindow&) = delete;

  bool Create(HWND owner);
  void Show(const core::CompositionSnapshot& snapshot, const RECT& text_rectangle,
            const core::Settings& settings, std::size_t page_offset);
  void Hide();

  static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wparam,
                                          LPARAM lparam);

 private:
  LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);
  bool EnsureDeviceResources();
  void Paint();
  void DiscardDeviceResources();

  HWND window_ = nullptr;
  core::CompositionSnapshot snapshot_;
  core::Settings settings_;
  std::size_t page_offset_ = 0;
  float window_width_ = 420.0F;
  float dpi_scale_ = 1.0F;
  float layout_scale_ = 1.0F;
  bool dark_theme_ = false;
  bool dark_theme_initialized_ = false;
  std::vector<float> candidate_widths_;
  std::vector<float> candidate_lefts_;
  std::vector<float> candidate_tops_;
  Microsoft::WRL::ComPtr<ID2D1Factory> d2d_factory_;
  Microsoft::WRL::ComPtr<IDWriteFactory> dwrite_factory_;
  Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> render_target_;
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> text_brush_;
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> preedit_brush_;
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> highlighted_text_brush_;
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> muted_brush_;
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> accent_brush_;
  Microsoft::WRL::ComPtr<IDWriteTextFormat> preedit_format_;
  Microsoft::WRL::ComPtr<IDWriteTextFormat> candidate_format_;
  Microsoft::WRL::ComPtr<IDWriteTextFormat> annotation_format_;
};

}  // namespace ziliu::ui
