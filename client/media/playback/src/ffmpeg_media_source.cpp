#include "shareme/media/ffmpeg_media_source.hpp"

#include "shareme/media/media_time.hpp"
#include "shareme/media/pending_media_events.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixfmt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace shareme::media {
namespace {

[[nodiscard]] std::runtime_error ffmpeg_error(
    const std::string& operation,
    int error_code) {
  std::array<char, AV_ERROR_MAX_STRING_SIZE> detail{};
  av_strerror(error_code, detail.data(), detail.size());
  return std::runtime_error{operation + ": " + detail.data()};
}

struct HardwareFormatSelection {
  bool selected{false};
};

enum AVPixelFormat select_video_pixel_format(
    AVCodecContext* codec_context, const enum AVPixelFormat* formats) {
  auto* selection =
      static_cast<HardwareFormatSelection*>(codec_context->opaque);
  for (const auto* format = formats; *format != AV_PIX_FMT_NONE; ++format) {
    if (*format == AV_PIX_FMT_VIDEOTOOLBOX) {
      if (selection != nullptr)
        selection->selected = true;
      return *format;
    }
  }
  return formats[0];
}

[[nodiscard]] AVCodecContext* open_decoder(
    AVFormatContext* format_context,
    int stream_index,
    const AVCodec* decoder,
    bool prefer_videotoolbox,
    bool* videotoolbox_selected) {
  if (videotoolbox_selected != nullptr)
    *videotoolbox_selected = false;

  const auto open_once = [&](bool use_videotoolbox) -> AVCodecContext* {
    auto* codec_context = avcodec_alloc_context3(decoder);
    if (codec_context == nullptr) {
      if (use_videotoolbox)
        return nullptr;
      throw std::runtime_error{"Could not allocate FFmpeg decoder context"};
    }

    const auto parameters_result = avcodec_parameters_to_context(
        codec_context, format_context->streams[stream_index]->codecpar);
    if (parameters_result < 0) {
      avcodec_free_context(&codec_context);
      if (use_videotoolbox)
        return nullptr;
      throw ffmpeg_error("Could not copy codec parameters", parameters_result);
    }

    HardwareFormatSelection format_selection;
    if (use_videotoolbox) {
      if (decoder->id != AV_CODEC_ID_HEVC) {
        avcodec_free_context(&codec_context);
        return nullptr;
      }
      bool supports_videotoolbox = false;
      for (int index = 0;; ++index) {
        const auto* config = avcodec_get_hw_config(decoder, index);
        if (config == nullptr)
          break;
        if (config->device_type == AV_HWDEVICE_TYPE_VIDEOTOOLBOX &&
            (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) != 0 &&
            config->pix_fmt == AV_PIX_FMT_VIDEOTOOLBOX) {
          supports_videotoolbox = true;
          break;
        }
      }
      if (!supports_videotoolbox) {
        avcodec_free_context(&codec_context);
        return nullptr;
      }
      AVBufferRef* device_context = nullptr;
      const auto device_result = av_hwdevice_ctx_create(
          &device_context, AV_HWDEVICE_TYPE_VIDEOTOOLBOX, nullptr, nullptr, 0);
      if (device_result < 0) {
        avcodec_free_context(&codec_context);
        return nullptr;
      }
      codec_context->hw_device_ctx = device_context;
      codec_context->opaque = &format_selection;
      codec_context->get_format = &select_video_pixel_format;
      format_selection.selected = true;
    }

    const auto open_result = avcodec_open2(codec_context, decoder, nullptr);
    codec_context->opaque = nullptr;
    if (open_result < 0) {
      avcodec_free_context(&codec_context);
      if (use_videotoolbox)
        return nullptr;
      throw ffmpeg_error("Could not open FFmpeg decoder", open_result);
    }
    if (videotoolbox_selected != nullptr)
      *videotoolbox_selected = format_selection.selected;
    return codec_context;
  };

  if (prefer_videotoolbox) {
    if (auto* codec_context = open_once(true))
      return codec_context;
  }
  return open_once(false);
}

[[nodiscard]] Rational time_base_of(
    const AVFormatContext* format_context,
    int stream_index) {
  const auto time_base = format_context->streams[stream_index]->time_base;
  return {time_base.num, time_base.den};
}

[[nodiscard]] const char* color_range_name(AVColorRange range) {
  switch (range) {
  case AVCOL_RANGE_MPEG:
    return "limited";
  case AVCOL_RANGE_JPEG:
    return "full";
  default:
    return "unknown";
  }
}

[[nodiscard]] const char* color_space_name(AVColorSpace space) {
  switch (space) {
  case AVCOL_SPC_BT709:
    return "bt709";
  case AVCOL_SPC_BT2020_NCL:
    return "bt2020nc";
  case AVCOL_SPC_BT2020_CL:
    return "bt2020c";
  default:
    return "unknown";
  }
}

}  // namespace

class FfmpegMediaSource::Impl {
public:
  explicit Impl(FfmpegMediaSourceOptions options) : options_(options) {
    if (!options_.decode_video && !options_.decode_audio) {
      throw std::invalid_argument{
          "At least one FFmpeg decoder must be enabled"};
    }
  }

  ~Impl() {
    close();
  }

  MediaInfo open(const std::filesystem::path& path) {
    close();
    pending_events_ = PendingMediaEvents{};
    decoded_video_frames_ = 0;
    decoded_audio_frames_ = 0;

    auto* opened_format = static_cast<AVFormatContext*>(nullptr);
    const auto open_result =
        avformat_open_input(&opened_format, path.string().c_str(), nullptr, nullptr);
    if (open_result < 0) {
      throw ffmpeg_error("Could not open media file", open_result);
    }
    format_context_ = opened_format;

    try {
      const auto info_result =
          avformat_find_stream_info(format_context_, nullptr);
      if (info_result < 0) {
        throw ffmpeg_error("Could not read media stream information", info_result);
      }

      if (options_.decode_video) {
        const AVCodec* video_decoder = nullptr;
        video_stream_index_ = av_find_best_stream(
            format_context_,
            AVMEDIA_TYPE_VIDEO,
            -1,
            -1,
            &video_decoder,
            0);
        if (video_stream_index_ < 0 || video_decoder == nullptr) {
          throw VideoStreamUnavailable{};
        }
        bool videotoolbox_selected = false;
        video_codec_context_ = open_decoder(
            format_context_, video_stream_index_, video_decoder,
            options_.video_acceleration == VideoAccelerationMode::auto_mode,
            &videotoolbox_selected);
        video_acceleration_path_ = videotoolbox_selected ? "hardware" : "software";
      }

      if (options_.decode_audio) {
        const AVCodec* audio_decoder = nullptr;
        audio_stream_index_ = av_find_best_stream(
            format_context_,
            AVMEDIA_TYPE_AUDIO,
            -1,
            video_stream_index_,
            &audio_decoder,
            0);
        if (audio_stream_index_ >= 0 && audio_decoder != nullptr) {
          audio_codec_context_ =
              open_decoder(
                  format_context_, audio_stream_index_, audio_decoder, false,
                  nullptr);
        } else {
          audio_stream_index_ = -1;
          if (!options_.decode_video) {
            throw AudioStreamUnavailable{};
          }
        }
      }

      packet_ = av_packet_alloc();
      frame_ = av_frame_alloc();
      if (packet_ == nullptr || frame_ == nullptr) {
        throw std::runtime_error{"Could not allocate FFmpeg packet or frame"};
      }

      MediaInfo info;
      info.has_video = video_codec_context_ != nullptr;
      info.has_audio = audio_codec_context_ != nullptr;
      if (video_codec_context_ != nullptr) {
        info.video_width = video_codec_context_->width;
        info.video_height = video_codec_context_->height;
        const auto* video_stream = format_context_->streams[video_stream_index_];
        info.video_frame_rate_num = video_stream->avg_frame_rate.num;
        info.video_frame_rate_den = video_stream->avg_frame_rate.den;
        if (video_codec_context_->sample_aspect_ratio.num > 0 &&
            video_codec_context_->sample_aspect_ratio.den > 0) {
          info.video_pixel_aspect_num =
              video_codec_context_->sample_aspect_ratio.num;
          info.video_pixel_aspect_den =
              video_codec_context_->sample_aspect_ratio.den;
        } else {
          info.video_pixel_aspect_num = 1;
          info.video_pixel_aspect_den = 1;
        }
        info.video_codec = avcodec_get_name(video_codec_context_->codec_id);
        if (const auto* profile = avcodec_profile_name(
                video_codec_context_->codec_id, video_codec_context_->profile))
          info.video_profile = profile;
        info.video_color_range =
            color_range_name(video_codec_context_->color_range);
        info.video_color_space =
            color_space_name(video_codec_context_->colorspace);
        info.video_acceleration = video_acceleration_path_;
      }
      if (format_context_->duration != AV_NOPTS_VALUE) {
        info.duration_ms = av_rescale_q(
            format_context_->duration,
            AV_TIME_BASE_Q,
            AVRational{1, 1'000});
      }
      if (format_context_->start_time != AV_NOPTS_VALUE) {
        info.start_time_ms = av_rescale_q(
            format_context_->start_time,
            AV_TIME_BASE_Q,
            AVRational{1, 1'000});
      }
      return info;
    } catch (...) {
      close();
      throw;
    }
  }

  MediaEvent read_next(std::uint64_t generation) {
    ensure_open();
    while (pending_events_.empty()) {
      if (input_finished_) {
        flush_decoders(generation);
        if (pending_events_.empty() && decoders_fully_drained()) {
          return EndOfStream{};
        }
        if (!pending_events_.empty()) {
          break;
        }
        continue;
      }

      const auto read_result = av_read_frame(format_context_, packet_);
      if (read_result == AVERROR_EOF) {
        input_finished_ = true;
        av_packet_unref(packet_);
        continue;
      }
      if (read_result < 0) {
        av_packet_unref(packet_);
        throw ffmpeg_error("Could not read media packet", read_result);
      }

      try {
        if (packet_->stream_index == video_stream_index_) {
          send_and_drain(video_codec_context_, true, generation);
        } else if (packet_->stream_index == audio_stream_index_) {
          send_and_drain(audio_codec_context_, false, generation);
        }
      } catch (...) {
        av_packet_unref(packet_);
        throw;
      }
      av_packet_unref(packet_);
    }

    auto event = pending_events_.pop();
    if (!event.has_value()) {
      throw std::logic_error{"Pending media event disappeared"};
    }
    return std::move(*event);
  }

  void seek(std::int64_t target_ms) {
    ensure_open();
    if (target_ms < 0) {
      target_ms = 0;
    }

    const auto seek_stream_index =
        video_codec_context_ != nullptr ? video_stream_index_
                                        : audio_stream_index_;
    const auto stream_time_base =
        format_context_->streams[seek_stream_index]->time_base;
    const auto target_timestamp = av_rescale_q(
        target_ms,
        AVRational{1, 1'000},
        stream_time_base);
    const auto seek_result = av_seek_frame(
        format_context_,
        seek_stream_index,
        target_timestamp,
        AVSEEK_FLAG_BACKWARD);
    if (seek_result < 0) {
      throw ffmpeg_error("Could not seek media file", seek_result);
    }

    avformat_flush(format_context_);
    if (video_codec_context_ != nullptr) {
      avcodec_flush_buffers(video_codec_context_);
    }
    if (audio_codec_context_ != nullptr) {
      avcodec_flush_buffers(audio_codec_context_);
    }
    if (swr_context_ != nullptr) {
      swr_free(&swr_context_);
    }

    pending_events_.clear();
    input_finished_ = false;
    video_flush_sent_ = false;
    audio_flush_sent_ = false;
    video_flush_complete_ = false;
    audio_flush_complete_ = false;
    resampler_drained_ = false;
    last_video_pts_ms_ = target_ms - 1;
    last_audio_pts_ms_ = target_ms - 1;
    next_audio_output_pts_ms_.reset();
    video_discard_before_ms_ = target_ms;
    audio_discard_before_ms_ = target_ms;
  }

  void close() noexcept {
    pending_events_.clear();
    if (swr_context_ != nullptr) {
      swr_free(&swr_context_);
    }
    if (sws_context_ != nullptr) {
      sws_freeContext(sws_context_);
      sws_context_ = nullptr;
    }
    if (frame_ != nullptr) {
      av_frame_free(&frame_);
    }
    if (software_frame_ != nullptr) {
      av_frame_free(&software_frame_);
    }
    if (packet_ != nullptr) {
      av_packet_free(&packet_);
    }
    if (audio_codec_context_ != nullptr) {
      avcodec_free_context(&audio_codec_context_);
    }
    if (video_codec_context_ != nullptr) {
      avcodec_free_context(&video_codec_context_);
    }
    if (format_context_ != nullptr) {
      avformat_close_input(&format_context_);
    }

    video_stream_index_ = -1;
    audio_stream_index_ = -1;
    input_finished_ = false;
    video_flush_sent_ = false;
    audio_flush_sent_ = false;
    video_flush_complete_ = false;
    audio_flush_complete_ = false;
    resampler_drained_ = false;
    last_video_pts_ms_ = -1;
    last_audio_pts_ms_ = -1;
    next_audio_output_pts_ms_.reset();
    video_discard_before_ms_.reset();
    audio_discard_before_ms_.reset();
    video_acceleration_path_ = "software";
  }

  [[nodiscard]] MediaSourceMetrics metrics() const noexcept {
    const auto pending = pending_events_.metrics();
    return {
        .decoded_video_frames = decoded_video_frames_,
        .decoded_audio_frames = decoded_audio_frames_,
        .pending_events = pending.size,
        .pending_bytes = pending.bytes,
        .peak_pending_events = pending.peak_size,
        .peak_pending_bytes = pending.peak_bytes,
        .backpressure_events = pending.backpressure_events,
    };
  }

private:
  enum class DrainResult {
    exhausted,
    pending_full,
  };

  void ensure_open() const {
    if (format_context_ == nullptr ||
        (video_codec_context_ == nullptr && audio_codec_context_ == nullptr)) {
      throw std::logic_error{"Media source is not open"};
    }
  }

  void send_and_drain(
      AVCodecContext* codec_context,
      bool is_video,
      std::uint64_t generation) {
    const auto send_result = avcodec_send_packet(codec_context, packet_);
    if (send_result < 0) {
      throw ffmpeg_error("Could not send packet to decoder", send_result);
    }
    static_cast<void>(drain(codec_context, is_video, generation));
  }

  DrainResult drain(
      AVCodecContext* codec_context,
      bool is_video,
      std::uint64_t generation) {
    while (true) {
      const auto can_push =
          is_video ? pending_events_.can_push_video()
                   : pending_events_.can_push_audio();
      if (!can_push) {
        pending_events_.note_backpressure();
        return DrainResult::pending_full;
      }

      const auto receive_result = avcodec_receive_frame(codec_context, frame_);
      if (receive_result == AVERROR(EAGAIN) || receive_result == AVERROR_EOF) {
        return DrainResult::exhausted;
      }
      if (receive_result < 0) {
        throw ffmpeg_error("Could not receive decoded frame", receive_result);
      }

      if (is_video) {
        ++decoded_video_frames_;
      } else {
        ++decoded_audio_frames_;
      }

      try {
        if (is_video) {
          auto video = convert_video(generation);
          if (!video_discard_before_ms_.has_value() ||
              video.pts_ms >= *video_discard_before_ms_) {
            video_discard_before_ms_.reset();
            MediaEvent event{std::move(video)};
            if (!pending_events_.push(std::move(event))) {
              throw std::logic_error{"Video pending queue capacity changed"};
            }
          }
        } else {
          auto audio = convert_audio(generation);
          if (!audio_discard_before_ms_.has_value() ||
              audio.pts_ms >= *audio_discard_before_ms_) {
            audio_discard_before_ms_.reset();
            MediaEvent event{std::move(audio)};
            if (!pending_events_.push(std::move(event))) {
              throw std::logic_error{"Audio pending queue capacity changed"};
            }
          }
        }
      } catch (...) {
        av_frame_unref(frame_);
        throw;
      }
      av_frame_unref(frame_);
    }
  }

  VideoFrame convert_video(std::uint64_t generation) {
    const AVFrame* source_frame = frame_;
    if (frame_->format == AV_PIX_FMT_VIDEOTOOLBOX) {
      if (software_frame_ == nullptr) {
        software_frame_ = av_frame_alloc();
        if (software_frame_ == nullptr) {
          throw std::runtime_error{"Could not allocate software video frame"};
        }
      }
      av_frame_unref(software_frame_);
      const auto transfer_result =
          av_hwframe_transfer_data(software_frame_, frame_, 0);
      if (transfer_result < 0) {
        throw ffmpeg_error(
            "Could not transfer VideoToolbox frame", transfer_result);
      }
      source_frame = software_frame_;
    }

    const auto width = source_frame->width;
    const auto height = source_frame->height;
    if (width <= 0 || height <= 0) {
      throw std::runtime_error{"Decoded video frame has invalid dimensions"};
    }

    sws_context_ = sws_getCachedContext(
        sws_context_,
        width,
        height,
        static_cast<AVPixelFormat>(source_frame->format),
        width,
        height,
        AV_PIX_FMT_YUV420P,
        SWS_BILINEAR,
        nullptr,
        nullptr,
        nullptr);
    if (sws_context_ == nullptr) {
      throw std::runtime_error{"Could not create FFmpeg video converter"};
    }

    VideoFrame output;
    output.pixel_format = VideoPixelFormat::i420;
    output.width = width;
    output.height = height;
    output.stride_y = width;
    output.stride_u = (width + 1) / 2;
    output.stride_v = (width + 1) / 2;
    output.i420_y.resize(static_cast<std::size_t>(output.stride_y) *
                         static_cast<std::size_t>(height));
    output.i420_u.resize(static_cast<std::size_t>(output.stride_u) *
                         static_cast<std::size_t>((height + 1) / 2));
    output.i420_v.resize(static_cast<std::size_t>(output.stride_v) *
                         static_cast<std::size_t>((height + 1) / 2));
    output.generation = generation;
    output.pts_ms = frame_pts_ms(video_stream_index_, last_video_pts_ms_);
    last_video_pts_ms_ = output.pts_ms;

    std::array<std::uint8_t*, 4> destination_data{
        reinterpret_cast<std::uint8_t*>(output.i420_y.data()),
        reinterpret_cast<std::uint8_t*>(output.i420_u.data()),
        reinterpret_cast<std::uint8_t*>(output.i420_v.data()),
        nullptr,
    };
    std::array<int, 4> destination_linesize{
        output.stride_y, output.stride_u, output.stride_v, 0};
    const auto scaled_height = sws_scale(
        sws_context_,
        source_frame->data,
        source_frame->linesize,
        0,
        height,
        destination_data.data(),
        destination_linesize.data());
    if (scaled_height != height) {
      throw std::runtime_error{"FFmpeg video conversion returned a short frame"};
    }
    return output;
  }

  AudioFrame convert_audio(std::uint64_t generation) {
    if (swr_context_ == nullptr) {
      AVChannelLayout stereo_layout;
      av_channel_layout_default(&stereo_layout, 2);
      const auto allocation_result = swr_alloc_set_opts2(
          &swr_context_,
          &stereo_layout,
          AV_SAMPLE_FMT_S16,
          48'000,
          &frame_->ch_layout,
          static_cast<AVSampleFormat>(frame_->format),
          frame_->sample_rate,
          0,
          nullptr);
      av_channel_layout_uninit(&stereo_layout);
      if (allocation_result < 0 || swr_context_ == nullptr) {
        throw ffmpeg_error(
            "Could not allocate FFmpeg audio resampler", allocation_result);
      }
      const auto initialize_result = swr_init(swr_context_);
      if (initialize_result < 0) {
        throw ffmpeg_error(
            "Could not initialize FFmpeg audio resampler", initialize_result);
      }
    }

    const auto output_sample_capacity =
        swr_get_out_samples(swr_context_, frame_->nb_samples);
    if (output_sample_capacity < 0) {
      throw ffmpeg_error(
          "Could not calculate resampled audio size", output_sample_capacity);
    }

    AudioFrame output;
    output.sample_rate = 48'000;
    output.channels = 2;
    output.generation = generation;
    output.pts_ms = frame_pts_ms(audio_stream_index_, last_audio_pts_ms_);
    last_audio_pts_ms_ = output.pts_ms;
    output.interleaved_samples.resize(
        static_cast<std::size_t>(output_sample_capacity) *
        static_cast<std::size_t>(output.channels));

    std::uint8_t* destination[]{
        reinterpret_cast<std::uint8_t*>(output.interleaved_samples.data())};
    const auto* const* source =
        const_cast<const std::uint8_t* const*>(frame_->extended_data);
    const auto converted_samples = swr_convert(
        swr_context_,
        destination,
        output_sample_capacity,
        source,
        frame_->nb_samples);
    if (converted_samples < 0) {
      throw ffmpeg_error("Could not resample decoded audio", converted_samples);
    }
    output.interleaved_samples.resize(
        static_cast<std::size_t>(converted_samples) *
        static_cast<std::size_t>(output.channels));
    next_audio_output_pts_ms_ =
        output.pts_ms +
        av_rescale_q(
            converted_samples,
            AVRational{1, output.sample_rate},
            AVRational{1, 1'000});
    return output;
  }

  std::int64_t frame_pts_ms(int stream_index, std::int64_t previous_pts_ms) const {
    const auto timestamp = frame_->best_effort_timestamp;
    const auto converted =
        to_milliseconds(timestamp, time_base_of(format_context_, stream_index));
    if (converted.has_value()) {
      return *converted;
    }
    return previous_pts_ms + 1;
  }

  void flush_decoders(std::uint64_t generation) {
    auto flush_one = [this, generation](
                         AVCodecContext* codec_context,
                         bool is_video,
                         bool& flush_sent,
                         bool& flush_complete) {
      if (codec_context == nullptr) {
        flush_complete = true;
        return;
      }
      if (!flush_sent) {
        const auto send_result = avcodec_send_packet(codec_context, nullptr);
        if (send_result < 0 && send_result != AVERROR_EOF) {
          throw ffmpeg_error("Could not flush decoder", send_result);
        }
        flush_sent = true;
      }
      if (flush_complete) {
        return;
      }
      if (drain(codec_context, is_video, generation) == DrainResult::exhausted) {
        flush_complete = true;
      }
    };

    flush_one(
        video_codec_context_, true, video_flush_sent_, video_flush_complete_);
    if (!video_flush_complete_) {
      return;
    }
    flush_one(
        audio_codec_context_, false, audio_flush_sent_, audio_flush_complete_);
    if (!audio_flush_complete_) {
      return;
    }
    static_cast<void>(drain_resampler(generation));
  }

  [[nodiscard]] bool decoders_fully_drained() const noexcept {
    return (video_codec_context_ == nullptr || video_flush_complete_) &&
           (audio_codec_context_ == nullptr || audio_flush_complete_) &&
           resampler_drained_;
  }

  [[nodiscard]] bool drain_resampler(std::uint64_t generation) {
    if (resampler_drained_) {
      return true;
    }
    if (swr_context_ == nullptr) {
      resampler_drained_ = true;
      return true;
    }

    while (swr_get_delay(swr_context_, 48'000) > 0) {
      if (!pending_events_.can_push_audio()) {
        pending_events_.note_backpressure();
        return false;
      }
      const auto output_sample_capacity =
          swr_get_out_samples(swr_context_, 0);
      if (output_sample_capacity < 0) {
        throw ffmpeg_error(
            "Could not calculate drained audio size",
            output_sample_capacity);
      }
      if (output_sample_capacity == 0) {
        resampler_drained_ = true;
        return true;
      }

      AudioFrame output;
      output.sample_rate = 48'000;
      output.channels = 2;
      output.generation = generation;
      if (!next_audio_output_pts_ms_.has_value()) {
        throw std::logic_error{
            "Resampled audio drain has no output timestamp"};
      }
      output.pts_ms = *next_audio_output_pts_ms_;
      output.interleaved_samples.resize(
          static_cast<std::size_t>(output_sample_capacity) *
          static_cast<std::size_t>(output.channels));

      std::uint8_t* destination[]{
          reinterpret_cast<std::uint8_t*>(
              output.interleaved_samples.data())};
      const auto converted_samples = swr_convert(
          swr_context_,
          destination,
          output_sample_capacity,
          nullptr,
          0);
      if (converted_samples < 0) {
        throw ffmpeg_error(
            "Could not drain resampled audio", converted_samples);
      }
      if (converted_samples == 0) {
        resampler_drained_ = true;
        return true;
      }

      output.interleaved_samples.resize(
          static_cast<std::size_t>(converted_samples) *
          static_cast<std::size_t>(output.channels));
      next_audio_output_pts_ms_ =
          output.pts_ms +
          av_rescale_q(
              converted_samples,
              AVRational{1, output.sample_rate},
              AVRational{1, 1'000});
      if (!audio_discard_before_ms_.has_value() ||
          output.pts_ms >= *audio_discard_before_ms_) {
        audio_discard_before_ms_.reset();
        MediaEvent event{std::move(output)};
        if (!pending_events_.push(std::move(event))) {
          throw std::logic_error{"Audio pending queue capacity changed"};
        }
      }
    }
    resampler_drained_ = true;
    return true;
  }

  AVFormatContext* format_context_{nullptr};
  AVCodecContext* video_codec_context_{nullptr};
  AVCodecContext* audio_codec_context_{nullptr};
  SwsContext* sws_context_{nullptr};
  SwrContext* swr_context_{nullptr};
  AVPacket* packet_{nullptr};
  AVFrame* frame_{nullptr};
  AVFrame* software_frame_{nullptr};
  int video_stream_index_{-1};
  int audio_stream_index_{-1};
  bool input_finished_{false};
  bool video_flush_sent_{false};
  bool audio_flush_sent_{false};
  bool video_flush_complete_{false};
  bool audio_flush_complete_{false};
  bool resampler_drained_{false};
  std::uint64_t decoded_video_frames_{0};
  std::uint64_t decoded_audio_frames_{0};
  std::int64_t last_video_pts_ms_{-1};
  std::int64_t last_audio_pts_ms_{-1};
  std::optional<std::int64_t> next_audio_output_pts_ms_;
  std::optional<std::int64_t> video_discard_before_ms_;
  std::optional<std::int64_t> audio_discard_before_ms_;
  std::string video_acceleration_path_{"software"};
  PendingMediaEvents pending_events_;
  const FfmpegMediaSourceOptions options_;
};

FfmpegMediaSource::FfmpegMediaSource(FfmpegMediaSourceOptions options)
    : impl_{std::make_unique<Impl>(options)} {}

FfmpegMediaSource::~FfmpegMediaSource() = default;

FfmpegMediaSource::FfmpegMediaSource(FfmpegMediaSource&&) noexcept = default;

FfmpegMediaSource& FfmpegMediaSource::operator=(
    FfmpegMediaSource&&) noexcept = default;

MediaInfo FfmpegMediaSource::open(const std::filesystem::path& path) {
  return impl_->open(path);
}

MediaEvent FfmpegMediaSource::read_next(std::uint64_t generation) {
  return impl_->read_next(generation);
}

void FfmpegMediaSource::seek(std::int64_t target_ms) {
  impl_->seek(target_ms);
}

void FfmpegMediaSource::close() noexcept {
  impl_->close();
}

MediaSourceMetrics FfmpegMediaSource::metrics() const noexcept {
  return impl_->metrics();
}

}  // namespace shareme::media
