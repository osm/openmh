#ifndef MH_FFMPEG_SDK_H
#define MH_FFMPEG_SDK_H

/*
 * FFmpeg's headers use inline even when included by a C90 application.
 * Keep the compiler spelling confined to third-party headers.
 */
#if defined(_MSC_VER)
#define inline __inline
#else
#define inline __inline__
#endif
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#undef inline

/* Some 32-bit SDKs expand this macro to a C99 integer literal. */
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wlong-long"
#endif
static const int64_t mh_ffmpeg_no_timestamp = AV_NOPTS_VALUE;
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#endif
