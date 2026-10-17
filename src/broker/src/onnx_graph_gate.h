#pragma once

#include <string_view>

namespace ziliu::broker::detail {

// A bounded format gate for the supported self-contained IR10/opset20 FP32
// inference graph. Call only after verifying the trusted manifest and exact
// graph digest. This is defense in depth, not model approval or an ONNX checker.
// Throws std::runtime_error without including any model contents in the message.
void ValidateOnnxGraph(std::string_view bytes);

}  // namespace ziliu::broker::detail
