#pragma once

namespace shareme::media {

enum class VideoAccelerationMode { software, auto_mode };

enum class VideoDecoderPath { software, hardware, fallback };

struct VideoPathReport {
  VideoAccelerationMode requested{VideoAccelerationMode::software};
  VideoDecoderPath decoder{VideoDecoderPath::software};
};

}  // namespace shareme::media
