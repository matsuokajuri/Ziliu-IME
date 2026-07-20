#pragma once

#include "ziliu/core/engine.h"

#include <d2d1.h>
#include <dwrite.h>
#include <windows.h>
#include <wrl/client.h>

namespace ziliu::ui {

class CandidateWindow final {
 public:
  CandidateWindow() = default;
  ~CandidateWindow();

  CandidateWindow(const CandidateWindow&) = delete;
  CandidateWindow& operator=(const CandidateWindow&) = delete;

  bool Create(HWND owner);
  void Show(const core::CompositionSnapshot& snapshot, POINT anchor);
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
  Microsoft::WRL::ComPtr<ID2D1Factory> d2d_factory_;
  Microsoft::WRL::ComPtr<IDWriteFactory> dwrite_factory_;
  Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> render_target_;
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> text_brush_;
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> muted_brush_;
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> accent_brush_;
  Microsoft::WRL::ComPtr<IDWriteTextFormat> preedit_format_;
  Microsoft::WRL::ComPtr<IDWriteTextFormat> candidate_format_;
  Microsoft::WRL::ComPtr<IDWriteTextFormat> annotation_format_;
};

}  // namespace ziliu::ui
