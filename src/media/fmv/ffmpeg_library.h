// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// FFmpeg as the movie player uses it: loaded at run time, so the game starts without it and only the headers are
// needed to build. The project's own LGPL set (tools/ffmpeg/build.sh, copied beside the exe) is tried first, a
// system FFmpeg after it. Only built with SH_FMV_FFMPEG.

#if defined(SH_FMV_FFMPEG)

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace sh::fmv {

// X(library, function) for every FFmpeg function the player calls.
#define SH_FFMPEG_FUNCTIONS(X)              \
    X(Format, avformat_open_input)          \
    X(Format, avformat_find_stream_info)    \
    X(Format, avformat_close_input)         \
    X(Format, av_read_frame)                \
    X(Format, av_find_best_stream)          \
    X(Format, avformat_version)             \
    X(Codec, avcodec_find_decoder)          \
    X(Codec, avcodec_alloc_context3)        \
    X(Codec, avcodec_free_context)          \
    X(Codec, avcodec_parameters_to_context) \
    X(Codec, avcodec_open2)                 \
    X(Codec, avcodec_send_packet)           \
    X(Codec, avcodec_receive_frame)         \
    X(Codec, avcodec_get_name)              \
    X(Codec, av_packet_alloc)               \
    X(Codec, av_packet_free)                \
    X(Codec, av_packet_unref)               \
    X(Codec, avcodec_version)               \
    X(Util, av_frame_alloc)                 \
    X(Util, av_frame_free)                  \
    X(Util, av_frame_unref)                 \
    X(Util, av_channel_layout_default)      \
    X(Util, av_channel_layout_uninit)       \
    X(Util, av_opt_get_int)                 \
    X(Util, av_opt_set_int)                 \
    X(Util, av_opt_get_chlayout)            \
    X(Util, avutil_version)                 \
    X(Scale, sws_getContext)                \
    X(Scale, sws_freeContext)               \
    X(Scale, sws_scale)                     \
    X(Scale, sws_getCoefficients)           \
    X(Scale, sws_setColorspaceDetails)      \
    X(Scale, swscale_version)               \
    X(Resample, swr_alloc_set_opts2)        \
    X(Resample, swr_init)                   \
    X(Resample, swr_free)                   \
    X(Resample, swr_convert)                \
    X(Resample, swr_get_out_samples)        \
    X(Resample, swresample_version)

// The loaded functions, under their own names.
struct FfmpegLibrary {
#define SH_FFMPEG_MEMBER(library, name) decltype(&::name) name = nullptr;
    SH_FFMPEG_FUNCTIONS(SH_FFMPEG_MEMBER)
#undef SH_FFMPEG_MEMBER

    // Loads FFmpeg on the first call. Null when no usable set is there; the journal then says what was found
    // and what is needed.
    static const FfmpegLibrary* get();
};

}  // namespace sh::fmv

#endif  // SH_FMV_FFMPEG
