#include "shareme/core/video_quality_contract.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

void require(bool condition, const char* expression, int line) {
  if (condition)
    return;
  std::cerr << "Requirement failed at line " << line << ": " << expression
            << '\n';
  std::exit(EXIT_FAILURE);
}

#define REQUIRE(expression) require((expression), #expression, __LINE__)

shareme::core::VideoQualityContract contract() {
  return {
      .width = 3'840,
      .height = 2'160,
      .cadence = {24, 1},
      .pixel_aspect_ratio = {1, 1},
      .color_range = "limited",
      .color_space = "bt2020nc",
      .codec = "hevc",
      .profile = "main10",
      .baseline_dropped_frames = 3,
      .baseline_coalesced_frames = 1,
  };
}

shareme::core::VideoQualitySample sample() {
  return {
      .width = 3'840,
      .height = 2'160,
      .cadence = {48, 2},
      .pixel_aspect_ratio = {1, 1},
      .color_range = "limited",
      .color_space = "bt2020nc",
      .codec = "hevc",
      .profile = "main10",
      .psnr_db = 45.0,
      .ssim = 0.995,
      .dropped_frames = 3,
      .coalesced_frames = 1,
  };
}

void accepts_equivalent_rational_cadence_and_exact_quality_boundaries() {
  const auto comparison =
      shareme::core::compare_quality(contract(), sample());
  REQUIRE(comparison.passed);
  REQUIRE(comparison.exact_dimensions);
  REQUIRE(comparison.exact_cadence);
  REQUIRE(comparison.exact_pixel_aspect_ratio);
  REQUIRE(comparison.exact_color_metadata);
  REQUIRE(comparison.exact_codec_profile);
  REQUIRE(comparison.no_additional_drops);
  REQUIRE(comparison.psnr_pass);
  REQUIRE(comparison.ssim_pass);
}

void accepts_equivalent_large_rationals_without_cross_multiplication() {
  auto expected = contract();
  expected.cadence = {4'000'000'000'000'000'000ULL,
                      3'000'000'000'000'000'000ULL};
  auto observed = sample();
  observed.cadence = {8'000'000'000'000'000'000ULL,
                      6'000'000'000'000'000'000ULL};
  const auto comparison =
      shareme::core::compare_quality(expected, observed);
  REQUIRE(comparison.exact_cadence);
}

void rejects_geometry_metadata_codec_and_extra_drop_changes() {
  auto observed = sample();
  observed.width = 853;
  observed.color_space = "bt709";
  observed.profile = "main";
  observed.dropped_frames = 4;
  const auto comparison =
      shareme::core::compare_quality(contract(), observed);
  REQUIRE(!comparison.passed);
  REQUIRE(!comparison.exact_dimensions);
  REQUIRE(!comparison.exact_color_metadata);
  REQUIRE(!comparison.exact_codec_profile);
  REQUIRE(!comparison.no_additional_drops);
}

void rejects_invalid_rationals_missing_metrics_and_non_finite_values() {
  auto observed = sample();
  observed.cadence.denominator = 0;
  observed.psnr_db.reset();
  observed.ssim = std::numeric_limits<double>::quiet_NaN();
  const auto comparison =
      shareme::core::compare_quality(contract(), observed);
  REQUIRE(!comparison.passed);
  REQUIRE(!comparison.exact_cadence);
  REQUIRE(!comparison.psnr_pass);
  REQUIRE(!comparison.ssim_pass);
  REQUIRE(!comparison.valid_measurements);
}

void enforces_psnr_and_ssim_lower_bounds() {
  auto observed = sample();
  observed.psnr_db = 44.999;
  observed.ssim = 0.994999;
  const auto comparison =
      shareme::core::compare_quality(contract(), observed);
  REQUIRE(!comparison.passed);
  REQUIRE(!comparison.psnr_pass);
  REQUIRE(!comparison.ssim_pass);
}

}  // namespace

int main() {
  accepts_equivalent_rational_cadence_and_exact_quality_boundaries();
  accepts_equivalent_large_rationals_without_cross_multiplication();
  rejects_geometry_metadata_codec_and_extra_drop_changes();
  rejects_invalid_rationals_missing_metrics_and_non_finite_values();
  enforces_psnr_and_ssim_lower_bounds();
  return EXIT_SUCCESS;
}
