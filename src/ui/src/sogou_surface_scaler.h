#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace ziliu::ui::detail {

struct SogouRgba8 {
  std::uint8_t red = 0;
  std::uint8_t green = 0;
  std::uint8_t blue = 0;
  std::uint8_t alpha = 0;

  bool operator==(const SogouRgba8&) const = default;
};

// Premultiplied BGRA byte order. The scaler stores these pixels in positive-
// height DIB (bottom-up) row order; the nine-slice target reuses the same byte
// type in top-down row order for direct D2D upload.
struct SogouPbgra8 {
  std::uint8_t blue = 0;
  std::uint8_t green = 0;
  std::uint8_t red = 0;
  std::uint8_t alpha = 0;

  bool operator==(const SogouPbgra8&) const = default;
};

struct SogouPixelRect {
  std::uint32_t left = 0;
  std::uint32_t top = 0;
  std::uint32_t right = 0;
  std::uint32_t bottom = 0;

  bool operator==(const SogouPixelRect&) const = default;
};

enum class SogouPatchLayout {
  kStretch,
  kTile,
  kFixed,
};

struct SogouMitchellCoefficients {
  double inner_constant = 0.0;
  double inner_linear = 0.0;
  double inner_cubic = 0.0;
  double outer_constant = 0.0;
  double outer_linear = 0.0;
  double outer_quadratic = 0.0;
  double outer_cubic = 0.0;
};

[[nodiscard]] inline const SogouMitchellCoefficients& SogouMitchellCoefficientTable() noexcept {
  static const SogouMitchellCoefficients coefficients = [] {
    // Use the balanced Mitchell-Netravali filter (B=C=1/3) and construct its
    // coefficients once so every scaling pass shares identical values.
    const double b = std::bit_cast<double>(std::uint64_t{0x3FD5555555555555ULL});
    const double c = b;
    const double six = 6.0;
    const double two_b = b + b;
    const double six_c = c * six;
    const double inner_constant = (six - two_b) / six;
    double inner_linear = b * 12.0;
    inner_linear = inner_linear - 18.0;
    inner_linear = inner_linear + six_c;
    inner_linear = inner_linear / six;
    double inner_cubic = 12.0 - b * 9.0;
    const double c_times_24 = c * 24.0;
    const double negative_12b = b * -12.0;
    inner_cubic = inner_cubic - six_c;
    inner_cubic = inner_cubic / six;
    double outer_constant = b * 8.0;
    outer_constant = outer_constant + c_times_24;
    outer_constant = outer_constant / six;
    const double c_times_48 = c * 48.0;
    double outer_linear = negative_12b - c_times_48;
    outer_linear = outer_linear / six;
    double outer_quadratic = b * six;
    outer_quadratic = outer_quadratic + c * 30.0;
    outer_quadratic = outer_quadratic / six;
    double outer_cubic = -b;
    outer_cubic = outer_cubic - six_c;
    outer_cubic = outer_cubic / six;
    return SogouMitchellCoefficients{inner_constant, inner_linear,    inner_cubic, outer_constant,
                                     outer_linear,   outer_quadratic, outer_cubic};
  }();
  return coefficients;
}

// Mitchell-Netravali's balanced reconstruction filter, evaluated in Horner
// order for stable floating-point behavior.
[[nodiscard]] inline double SogouMitchellNetravaliWeight(double distance) noexcept {
  const double value = std::abs(distance);
  const auto& coefficients = SogouMitchellCoefficientTable();
  if (value < 1.0) {
    double result = value;
    const double squared = value * value;
    result = result * coefficients.inner_cubic;
    result = result + coefficients.inner_linear;
    result = result * squared;
    result = result + coefficients.inner_constant;
    return result;
  }
  if (value < 2.0) {
    double result = value;
    result = result * coefficients.outer_cubic;
    result = result + coefficients.outer_quadratic;
    result = result * value;
    result = result + coefficients.outer_linear;
    result = result * value;
    result = result + coefficients.outer_constant;
    return result;
  }
  return 0.0;
}

[[nodiscard]] inline std::uint8_t SogouQuantizeByte(double value) noexcept {
  if (!(value > 0.0)) {
    return 0;
  }
  if (value >= 255.0) {
    return 255;
  }
  return static_cast<std::uint8_t>(std::floor(value + 0.5));
}

struct SogouMitchellContributors {
  std::uint32_t first = 0;
  std::uint32_t count = 0;
  std::array<double, 5> weights{};
};

[[nodiscard]] inline SogouMitchellContributors BuildSogouMitchell2xContributors(
    std::uint32_t output_coordinate, std::uint32_t source_length) noexcept {
  SogouMitchellContributors contributors;
  if (source_length == 0U || source_length > (std::numeric_limits<std::uint32_t>::max)() / 2U ||
      output_coordinate >= source_length * 2U) {
    return contributors;
  }

  const double scale = static_cast<double>(source_length * 2U) / static_cast<double>(source_length);
  double radius = 2.0;
  double filter_scale = 1.0;
  if (scale < 1.0) {
    radius = radius / scale;
    filter_scale = scale;
  }
  double offset = 0.5 / scale;
  offset = offset - 0.5;
  double center = static_cast<double>(output_coordinate) / scale;
  center = center + offset;

  const std::int64_t last_source = static_cast<std::int64_t>(source_length) - 1;
  std::int64_t first =
      (std::max)(std::int64_t{0}, static_cast<std::int64_t>(std::floor(center - radius)));
  std::int64_t last =
      (std::min)(last_source, static_cast<std::int64_t>(std::ceil(center + radius)));
  const std::int64_t maximum_slots = static_cast<std::int64_t>(contributors.weights.size());
  while (last - first + 1 > maximum_slots) {
    // At 2x the discarded low tap is an exact zero. Removing it first keeps
    // the fixed contributor table bounded without losing a sample.
    if (first < static_cast<std::int64_t>(source_length)) {
      ++first;
    } else {
      --last;
    }
  }
  contributors.first = static_cast<std::uint32_t>(first);

  double total = 0.0;
  for (std::int64_t coordinate = first; coordinate <= last; ++coordinate) {
    double argument = center - static_cast<double>(coordinate);
    argument = argument * filter_scale;
    double weight = SogouMitchellNetravaliWeight(argument);
    weight = weight * filter_scale;
    total = total + weight;
    contributors.weights[contributors.count] = weight;
    ++contributors.count;
  }
  if (total > 0.0 && total != 1.0) {
    for (std::uint32_t index = 0; index < contributors.count; ++index) {
      contributors.weights[index] = contributors.weights[index] / total;
    }
  }
  while (last != first && contributors.count > 0U &&
         contributors.weights[contributors.count - 1U] == 0.0) {
    --last;
    --contributors.count;
  }
  return contributors;
}

[[nodiscard]] inline SogouPbgra8 QuantizeSogouMitchellPass(
    const std::array<double, 4>& values) noexcept {
  SogouPbgra8 pixel{SogouQuantizeByte(values[0]), SogouQuantizeByte(values[1]),
                    SogouQuantizeByte(values[2]), SogouQuantizeByte(values[3])};
  pixel.alpha = (std::max)({pixel.blue, pixel.green, pixel.red, pixel.alpha});
  return pixel;
}

[[nodiscard]] inline bool ScaleSogouRgbaMitchell2x(std::span<const SogouRgba8> source,
                                                   std::uint32_t source_width,
                                                   std::uint32_t source_height,
                                                   std::vector<SogouPbgra8>* output) {
  if (output == nullptr || source_width == 0 || source_height == 0 ||
      source_width > (std::numeric_limits<std::uint32_t>::max)() / 2U ||
      source_height > (std::numeric_limits<std::uint32_t>::max)() / 2U ||
      static_cast<std::uint64_t>(source_width) * source_height != source.size()) {
    return false;
  }
  const std::uint32_t output_width = source_width * 2U;
  const std::uint32_t output_height = source_height * 2U;
  const std::uint64_t horizontal_count = static_cast<std::uint64_t>(output_width) * source_height;
  const std::uint64_t output_count = static_cast<std::uint64_t>(output_width) * output_height;
  if (horizontal_count > (std::numeric_limits<std::size_t>::max)() ||
      output_count > (std::numeric_limits<std::size_t>::max)()) {
    return false;
  }

  // Input pixels are logical top-down RGBA. Output pixels use positive-height
  // DIB row order, so filtering stores rows bottom-up.
  std::vector<SogouPbgra8> premultiplied(source.size());
  for (std::uint32_t logical_y = 0; logical_y < source_height; ++logical_y) {
    const std::uint32_t storage_y = source_height - 1U - logical_y;
    for (std::uint32_t x = 0; x < source_width; ++x) {
      const auto& pixel = source[static_cast<std::size_t>(logical_y) * source_width + x];
      const auto premultiply = [alpha = pixel.alpha](std::uint8_t channel) {
        return static_cast<std::uint8_t>(static_cast<std::uint32_t>(channel) * alpha / 255U);
      };
      premultiplied[static_cast<std::size_t>(storage_y) * source_width + x] = {
          premultiply(pixel.blue), premultiply(pixel.green), premultiply(pixel.red), pixel.alpha};
    }
  }

  const auto accumulate_axis = [](std::uint32_t output_coordinate, std::uint32_t source_length,
                                  const auto& sample) {
    const SogouMitchellContributors contributors =
        BuildSogouMitchell2xContributors(output_coordinate, source_length);
    double values[4]{};
    for (std::uint32_t index = 0; index < contributors.count; ++index) {
      const SogouPbgra8 pixel = sample(contributors.first + index);
      const double weight = contributors.weights[index];
      const double blue = static_cast<double>(pixel.blue) * weight;
      const double green = static_cast<double>(pixel.green) * weight;
      const double red = static_cast<double>(pixel.red) * weight;
      const double alpha = static_cast<double>(pixel.alpha) * weight;
      values[0] = values[0] + blue;
      values[1] = values[1] + green;
      values[2] = values[2] + red;
      values[3] = values[3] + alpha;
    }
    return std::array<double, 4>{values[0], values[1], values[2], values[3]};
  };

  std::vector<SogouPbgra8> horizontal(static_cast<std::size_t>(horizontal_count));
  for (std::uint32_t y = 0; y < source_height; ++y) {
    for (std::uint32_t x = 0; x < output_width; ++x) {
      const auto values = accumulate_axis(x, source_width, [&](std::uint32_t source_x) {
        return premultiplied[static_cast<std::size_t>(y) * source_width + source_x];
      });
      horizontal[static_cast<std::size_t>(y) * output_width + x] =
          QuantizeSogouMitchellPass(values);
    }
  }

  output->assign(static_cast<std::size_t>(output_count), {});
  for (std::uint32_t y = 0; y < output_height; ++y) {
    for (std::uint32_t x = 0; x < output_width; ++x) {
      const auto values = accumulate_axis(y, source_height, [&](std::uint32_t source_y) {
        return horizontal[static_cast<std::size_t>(source_y) * output_width + x];
      });
      (*output)[static_cast<std::size_t>(y) * output_width + x] = QuantizeSogouMitchellPass(values);
    }
  }
  return true;
}

[[nodiscard]] inline std::uint32_t ResolveSogouNearestSourceOffset(
    std::uint32_t destination_offset, std::uint32_t destination_length,
    std::uint32_t source_length) noexcept {
  if (destination_length == 0 || source_length == 0) {
    return 0;
  }
  const std::uint64_t numerator =
      (static_cast<std::uint64_t>(destination_offset) * 2U + 1U) * source_length;
  const std::uint64_t denominator = static_cast<std::uint64_t>(destination_length) * 2U;
  return std::min<std::uint32_t>(static_cast<std::uint32_t>(numerator / denominator),
                                 source_length - 1U);
}

[[nodiscard]] inline bool CompositeSogouRgbaPatchNearest(
    std::span<const SogouPbgra8> source, std::uint32_t source_width, std::uint32_t source_height,
    const SogouPixelRect& source_rect, std::span<SogouPbgra8> target, std::uint32_t target_width,
    std::uint32_t target_height, const SogouPixelRect& target_rect,
    SogouPatchLayout horizontal_layout, SogouPatchLayout vertical_layout) noexcept {
  if (source_width == 0 || source_height == 0 || target_width == 0 || target_height == 0 ||
      static_cast<std::uint64_t>(source_width) * source_height != source.size() ||
      static_cast<std::uint64_t>(target_width) * target_height != target.size() ||
      source_rect.left >= source_rect.right || source_rect.top >= source_rect.bottom ||
      source_rect.right > source_width || source_rect.bottom > source_height ||
      target_rect.left >= target_rect.right || target_rect.top >= target_rect.bottom ||
      target_rect.right > target_width || target_rect.bottom > target_height) {
    return false;
  }
  const std::uint32_t source_patch_width = source_rect.right - source_rect.left;
  const std::uint32_t source_patch_height = source_rect.bottom - source_rect.top;
  const std::uint32_t target_patch_width = target_rect.right - target_rect.left;
  const std::uint32_t target_patch_height = target_rect.bottom - target_rect.top;
  const auto resolve = [](std::uint32_t offset, std::uint32_t destination_length,
                          std::uint32_t source_length, SogouPatchLayout layout,
                          std::uint32_t* source_offset) {
    if (source_offset == nullptr) {
      return false;
    }
    switch (layout) {
    case SogouPatchLayout::kStretch:
      *source_offset = ResolveSogouNearestSourceOffset(offset, destination_length, source_length);
      return true;
    case SogouPatchLayout::kTile:
      *source_offset = offset % source_length;
      return true;
    case SogouPatchLayout::kFixed:
      if (offset >= source_length) {
        return false;
      }
      *source_offset = offset;
      return true;
    }
    return false;
  };

  for (std::uint32_t target_y = target_rect.top; target_y < target_rect.bottom; ++target_y) {
    std::uint32_t source_y = 0;
    if (!resolve(target_y - target_rect.top, target_patch_height, source_patch_height,
                 vertical_layout, &source_y)) {
      continue;
    }
    source_y += source_rect.top;
    const std::uint32_t storage_y = source_height - 1U - source_y;
    for (std::uint32_t target_x = target_rect.left; target_x < target_rect.right; ++target_x) {
      std::uint32_t source_x = 0;
      if (!resolve(target_x - target_rect.left, target_patch_width, source_patch_width,
                   horizontal_layout, &source_x)) {
        continue;
      }
      source_x += source_rect.left;
      target[static_cast<std::size_t>(target_y) * target_width + target_x] =
          source[static_cast<std::size_t>(storage_y) * source_width + source_x];
    }
  }
  return true;
}

} // namespace ziliu::ui::detail
