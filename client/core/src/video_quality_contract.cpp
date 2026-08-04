#include "shareme/core/video_quality_contract.hpp"

#include <cmath>

namespace shareme::core {
namespace {

bool valid_rational(const Rational& value) {
  return value.denominator != 0;
}

bool equal_rational(const Rational& lhs, const Rational& rhs) {
  if (!valid_rational(lhs) || !valid_rational(rhs))
    return false;
  using Wide = unsigned __int128;
  return static_cast<Wide>(lhs.numerator) * rhs.denominator ==
         static_cast<Wide>(rhs.numerator) * lhs.denominator;
}

bool finite_at_least(const std::optional<double>& value, double threshold) {
  return value.has_value() && std::isfinite(*value) && *value >= threshold;
}

}  // namespace

QualityComparison compare_quality(const VideoQualityContract& contract,
                                  const VideoQualitySample& sample) {
  QualityComparison comparison;
  comparison.exact_dimensions = contract.width != 0 && contract.height != 0 &&
                                sample.width == contract.width &&
                                sample.height == contract.height;
  comparison.exact_cadence = equal_rational(contract.cadence, sample.cadence);
  comparison.exact_pixel_aspect_ratio =
      equal_rational(contract.pixel_aspect_ratio, sample.pixel_aspect_ratio);
  comparison.exact_color_metadata =
      !contract.color_range.empty() && !contract.color_space.empty() &&
      sample.color_range == contract.color_range &&
      sample.color_space == contract.color_space;
  comparison.exact_codec_profile =
      !contract.codec.empty() && !contract.profile.empty() &&
      sample.codec == contract.codec && sample.profile == contract.profile;
  comparison.no_additional_drops =
      sample.dropped_frames <= contract.baseline_dropped_frames &&
      sample.coalesced_frames <= contract.baseline_coalesced_frames;
  comparison.psnr_pass = finite_at_least(sample.psnr_db, kMinimumPsnrDb);
  comparison.ssim_pass = finite_at_least(sample.ssim, kMinimumSsim);
  comparison.valid_measurements =
      comparison.exact_cadence && comparison.exact_pixel_aspect_ratio &&
      comparison.psnr_pass && comparison.ssim_pass;
  comparison.passed = comparison.exact_dimensions && comparison.exact_cadence &&
                      comparison.exact_pixel_aspect_ratio &&
                      comparison.exact_color_metadata &&
                      comparison.exact_codec_profile &&
                      comparison.no_additional_drops && comparison.psnr_pass &&
                      comparison.ssim_pass && comparison.valid_measurements;
  return comparison;
}

}  // namespace shareme::core
