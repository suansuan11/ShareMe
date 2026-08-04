#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace shareme::core {

struct Rational {
  std::uint64_t numerator = 0;
  std::uint64_t denominator = 0;
};

struct VideoQualityContract {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  Rational cadence;
  Rational pixel_aspect_ratio;
  std::string color_range;
  std::string color_space;
  std::string codec;
  std::string profile;
  std::uint64_t baseline_dropped_frames = 0;
  std::uint64_t baseline_coalesced_frames = 0;
};

struct VideoQualitySample {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  Rational cadence;
  Rational pixel_aspect_ratio;
  std::string color_range;
  std::string color_space;
  std::string codec;
  std::string profile;
  std::optional<double> psnr_db;
  std::optional<double> ssim;
  std::uint64_t dropped_frames = 0;
  std::uint64_t coalesced_frames = 0;
};

struct QualityComparison {
  bool passed = false;
  bool exact_dimensions = false;
  bool exact_cadence = false;
  bool exact_pixel_aspect_ratio = false;
  bool exact_color_metadata = false;
  bool exact_codec_profile = false;
  bool no_additional_drops = false;
  bool psnr_pass = false;
  bool ssim_pass = false;
  bool valid_measurements = false;
};

inline constexpr double kMinimumPsnrDb = 45.0;
inline constexpr double kMinimumSsim = 0.995;

QualityComparison compare_quality(const VideoQualityContract& contract,
                                  const VideoQualitySample& sample);

}  // namespace shareme::core
