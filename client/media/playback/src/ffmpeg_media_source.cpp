#include "shareme/media/ffmpeg_media_source.hpp"

#include "shareme/media/media_time.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
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

[[nodiscard]] AVCodecContext* open_decoder(
    AVFormatContext* format_context,
    int stream_index,
    const AVCodec* decoder) {
  auto* codec_context = avcodec_alloc_context3(decoder);
  if (codec_context == nullptr) {
    throw std::runtime_error{"Could not allocate FFmpeg decoder context"};
  }

  const auto parameters_result = avcodec_parameters_to_context(
      codec_context, format_context->streams[stream_index]->codecpar);
  if (parameters_result < 0) {
    avcodec_free_context(&codec_context);
    throw ffmpeg_error("Could not copy codec parameters", parameters_result);
  }

  const auto open_result = avcodec_open2(codec_context, decoder, nullptr);
  if (open_result < 0) {
    avcodec_free_context(&codec_context);
    throw ffmpeg_error("Could not open FFmpeg decoder", open_result);
  }
  return codec_context;
}

[[nodiscard]] Rational time_base_of(
    const AVFormatContext* format_context,
    int stream_index) {
  const auto time_base = format_context->streams[stream_index]->time_base;
  return {time_base.num, time_base.den};
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
        video_codec_context_ =
            open_decoder(format_context_, video_stream_index_, video_decoder);
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
              open_decoder(format_context_, audio_stream_index_, audio_decoder);
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
        if (pending_events_.empty()) {
          return EndOfStream{};
        }
        break;
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

    auto event = std::move(pending_events_.front());
    pending_events_.pop_front();
    return event;
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
    decoders_flushed_ = false;
    last_video_pts_ms_ = target_ms - 1;
    last_audio_pts_ms_ = target_ms - 1;
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
    decoders_flushed_ = false;
    last_video_pts_ms_ = -1;
    last_audio_pts_ms_ = -1;
    video_discard_before_ms_.reset();
    audio_discard_before_ms_.reset();
  }

private:
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
    drain(codec_context, is_video, generation);
  }

  void drain(
      AVCodecContext* codec_context,
      bool is_video,
      std::uint64_t generation) {
    while (true) {
      const auto receive_result = avcodec_receive_frame(codec_context, frame_);
      if (receive_result == AVERROR(EAGAIN) || receive_result == AVERROR_EOF) {
        return;
      }
      if (receive_result < 0) {
        throw ffmpeg_error("Could not receive decoded frame", receive_result);
      }

      try {
        if (is_video) {
          auto video = convert_video(generation);
          if (!video_discard_before_ms_.has_value() ||
              video.pts_ms >= *video_discard_before_ms_) {
            video_discard_before_ms_.reset();
            pending_events_.emplace_back(std::move(video));
          }
        } else {
          auto audio = convert_audio(generation);
          if (!audio_discard_before_ms_.has_value() ||
              audio.pts_ms >= *audio_discard_before_ms_) {
            audio_discard_before_ms_.reset();
            pending_events_.emplace_back(std::move(audio));
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
    const auto width = frame_->width;
    const auto height = frame_->height;
    if (width <= 0 || height <= 0) {
      throw std::runtime_error{"Decoded video frame has invalid dimensions"};
    }

    sws_context_ = sws_getCachedContext(
        sws_context_,
        width,
        height,
        static_cast<AVPixelFormat>(frame_->format),
        width,
        height,
        AV_PIX_FMT_RGBA,
        SWS_BILINEAR,
        nullptr,
        nullptr,
        nullptr);
    if (sws_context_ == nullptr) {
      throw std::runtime_error{"Could not create FFmpeg video converter"};
    }

    VideoFrame output;
    output.width = width;
    output.height = height;
    output.stride = width * 4;
    output.rgba.resize(
        static_cast<std::size_t>(output.stride) *
        static_cast<std::size_t>(output.height));
    output.generation = generation;
    output.pts_ms = frame_pts_ms(video_stream_index_, last_video_pts_ms_);
    last_video_pts_ms_ = output.pts_ms;

    std::array<std::uint8_t*, 4> destination_data{
        reinterpret_cast<std::uint8_t*>(output.rgba.data()),
        nullptr,
        nullptr,
        nullptr,
    };
    std::array<int, 4> destination_linesize{output.stride, 0, 0, 0};
    const auto scaled_height = sws_scale(
        sws_context_,
        frame_->data,
        frame_->linesize,
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
    if (decoders_flushed_) {
      return;
    }
    decoders_flushed_ = true;

    auto flush_one = [this, generation](
                         AVCodecContext* codec_context, bool is_video) {
      if (codec_context == nullptr) {
        return;
      }
      const auto send_result = avcodec_send_packet(codec_context, nullptr);
      if (send_result < 0 && send_result != AVERROR_EOF) {
        throw ffmpeg_error("Could not flush decoder", send_result);
      }
      drain(codec_context, is_video, generation);
    };

    flush_one(video_codec_context_, true);
    flush_one(audio_codec_context_, false);
  }

  AVFormatContext* format_context_{nullptr};
  AVCodecContext* video_codec_context_{nullptr};
  AVCodecContext* audio_codec_context_{nullptr};
  SwsContext* sws_context_{nullptr};
  SwrContext* swr_context_{nullptr};
  AVPacket* packet_{nullptr};
  AVFrame* frame_{nullptr};
  int video_stream_index_{-1};
  int audio_stream_index_{-1};
  bool input_finished_{false};
  bool decoders_flushed_{false};
  std::int64_t last_video_pts_ms_{-1};
  std::int64_t last_audio_pts_ms_{-1};
  std::optional<std::int64_t> video_discard_before_ms_;
  std::optional<std::int64_t> audio_discard_before_ms_;
  std::deque<MediaEvent> pending_events_;
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

}  // namespace shareme::media
