// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
//
// Copyright (C) 2020-2021 Intel Corporation

#include "opencv2/videoio.hpp"
#include <list>

#ifdef __cplusplus
extern "C" {
#endif
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#ifdef __cplusplus
}
#endif

#define HW_FRAMES_POOL_SIZE     20

using namespace cv;

AVBufferRef* hw_create_device(VideoAccelerationType va_type, int hw_device);
AVBufferRef* hw_create_frames(AVBufferRef *hw_device_ctx, int width, int height, AVPixelFormat hw_format, AVPixelFormat sw_format, int pool_size = HW_FRAMES_POOL_SIZE);
AVCodec *hw_find_codec(AVCodecID id, AVBufferRef *hw_device_ctx, int (*check_category)(const AVCodec *), AVPixelFormat *hw_pix_fmt = NULL);
AVPixelFormat hw_get_format_callback(struct AVCodecContext *ctx, const enum AVPixelFormat * fmt);

#if LIBAVUTIL_BUILD >= AV_VERSION_INT(55, 78, 100) // FFMPEG 3.4+

static AVHWDeviceType VideoAccelerationTypeToFFMPEG(VideoAccelerationType va_type) {
    struct HWTypeFFMPEG {
        VideoAccelerationType va_type;
        AVHWDeviceType ffmpeg_type;
    } ffmpeg_hw_types[] = {
        { VIDEO_ACCELERATION_D3D11, AV_HWDEVICE_TYPE_D3D11VA },
        { VIDEO_ACCELERATION_VAAPI, AV_HWDEVICE_TYPE_VAAPI },
        { VIDEO_ACCELERATION_MFX, AV_HWDEVICE_TYPE_QSV }
    };
    for (const HWTypeFFMPEG& hw : ffmpeg_hw_types) {
        if (va_type == hw.va_type)
            return hw.ffmpeg_type;
    }
    return AV_HWDEVICE_TYPE_NONE;
}

AVBufferRef* hw_create_device(VideoAccelerationType va_type, int hw_device) {
    AVHWDeviceType hw_type = VideoAccelerationTypeToFFMPEG(va_type);
    if (AV_HWDEVICE_TYPE_NONE == hw_type)
        return NULL;

    AVBufferRef* hw_device_ctx = NULL;
    char device[128] = "";
    char* pdevice = NULL;
    AVDictionary* options = NULL;
    if (hw_device >= 0 && hw_device < 100000) {
#ifdef _WIN32
        snprintf(device, sizeof(device), "%d", hw_device);
#else
        snprintf(device, sizeof(device), "/dev/dri/renderD%d", 128 + hw_device);
#endif
        if (hw_type == AV_HWDEVICE_TYPE_QSV) {
            av_dict_set(&options, "child_device", device, 0);
        }
        else {
            pdevice = device;
        }
    }
    av_hwdevice_ctx_create(&hw_device_ctx, hw_type, pdevice, options, 0);
    if (options)
        av_dict_free(&options);
    return hw_device_ctx;
}

AVBufferRef* hw_create_frames(AVBufferRef *hw_device_ctx, int width, int height, AVPixelFormat hw_format, AVPixelFormat sw_format, int pool_size)
{
    AVBufferRef *hw_frames_ref = av_hwframe_ctx_alloc(hw_device_ctx);
    if (!hw_frames_ref) {
        fprintf(stderr, "Failed to create HW frame context\n");
        return NULL;
    }
    AVHWFramesContext *frames_ctx = (AVHWFramesContext *)(hw_frames_ref->data);
    frames_ctx->format    = hw_format;
    frames_ctx->sw_format = sw_format;
    frames_ctx->width     = width;
    frames_ctx->height    = height;
    frames_ctx->initial_pool_size = pool_size;
    if (av_hwframe_ctx_init(hw_frames_ref) < 0) {
        fprintf(stderr, "Failed to initialize HW frame context\n");
        av_buffer_unref(&hw_frames_ref);
        return NULL;
    }
    return hw_frames_ref;
}

AVCodec *hw_find_codec(AVCodecID id, AVBufferRef *hw_device_ctx, int (*check_category)(const AVCodec *), AVPixelFormat *hw_pix_fmt) {
    AVHWDeviceType hw_type = AV_HWDEVICE_TYPE_NONE;
    AVCodec *c;
    void *opaque = 0;

    if (hw_device_ctx)
        hw_type = ((AVHWDeviceContext *) hw_device_ctx->data)->type;

    while ((c = (AVCodec*)av_codec_iterate(&opaque))) {
        if (!check_category(c))
            continue;
        if (c->id != id)
            continue;
        if (hw_type != AV_HWDEVICE_TYPE_NONE) {
            for (int i = 0;; i++) {
                const AVCodecHWConfig *hw_config = avcodec_get_hw_config(c, i);
                if (!hw_config)
                    break;
                if (hw_config->device_type == hw_type) {
                    int m = hw_config->methods;
                    if (!(m & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) && (m & AV_CODEC_HW_CONFIG_METHOD_HW_FRAMES_CTX) && hw_pix_fmt) {
                        // codec requires frame pool (hw_frames_ctx) created by application
                        *hw_pix_fmt = hw_config->pix_fmt;
                    }
                    return c;
                }
            }
        } else {
            return c;
        }
    }

    return NULL;
}

// Callback to select hardware pixel format (not software format) and allocate frame pool (hw_frames_ctx)
AVPixelFormat hw_get_format_callback(struct AVCodecContext *ctx, const enum AVPixelFormat * fmt) {
    if (!ctx->hw_device_ctx)
        return fmt[0];
    AVHWDeviceType hw_type = ((AVHWDeviceContext*)ctx->hw_device_ctx->data)->type;
    for (int j = 0;; j++) {
        const AVCodecHWConfig *hw_config = avcodec_get_hw_config(ctx->codec, j);
        if (!hw_config)
            break;
        if (hw_config->device_type == hw_type) {
            for (int i = 0; fmt[i] != AV_PIX_FMT_NONE; i++) {
                if (fmt[i] == hw_config->pix_fmt) {
                    if (hw_config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) {
                        return fmt[i];
                    }
                    if (hw_config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_FRAMES_CTX) {
                        ctx->hw_frames_ctx = hw_create_frames(ctx->hw_device_ctx, ctx->width, ctx->height, fmt[i], AV_PIX_FMT_NV12);
                        if (ctx->hw_frames_ctx)
                            return fmt[i];
                    }
                }
            }
        }
    }
    return fmt[0];
}

#else

AVBufferRef* hw_create_device(VideoAccelerationType va_type, int hw_device) {
    return NULL;
}
AVBufferRef* hw_create_frames(AVBufferRef *hw_device_ctx, int width, int height, AVPixelFormat hw_format, AVPixelFormat sw_format, int pool_size) {
    return NULL;
}
AVCodec *hw_find_codec(AVCodecID id, AVBufferRef *hw_device_ctx, int (*check_category)(const AVCodec *), AVPixelFormat *hw_pix_fmt) {
    return NULL;
}
AVPixelFormat hw_get_format_callback(struct AVCodecContext *ctx, const enum AVPixelFormat * fmt) {
    return fmt[0];
}

#endif
