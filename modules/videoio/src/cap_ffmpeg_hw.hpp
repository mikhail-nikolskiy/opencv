/*M///////////////////////////////////////////////////////////////////////////////////////
//
//  IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
//
//  By downloading, copying, installing or using the software you agree to this license.
//  If you do not agree to this license, do not download, install,
//  copy or use the software.
//
//
//                          License Agreement
//                For Open Source Computer Vision Library
//
// Copyright (C) 2000-2008, Intel Corporation, all rights reserved.
// Copyright (C) 2009, Willow Garage Inc., all rights reserved.
// Third party copyrights are property of their respective owners.
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
//   * Redistribution's of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//
//   * Redistribution's in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//   * The name of the copyright holders may not be used to endorse or promote products
//     derived from this software without specific prior written permission.
//
// This software is provided by the copyright holders and contributors "as is" and
// any express or implied warranties, including, but not limited to, the implied
// warranties of merchantability and fitness for a particular purpose are disclaimed.
// In no event shall the Intel Corporation or contributors be liable for any direct,
// indirect, incidental, special, exemplary, or consequential damages
// (including, but not limited to, procurement of substitute goods or services;
// loss of use, data, or profits; or business interruption) however caused
// and on any theory of liability, whether in contract, strict liability,
// or tort (including negligence or otherwise) arising in any way out of
// the use of this software, even if advised of the possibility of such damage.
//
//M*/

#include "opencv2/videoio.hpp"

#ifdef HAVE_DIRECTX
#include "opencv2/core/directx.hpp"
#endif
#ifdef HAVE_VA_INTEL
#include "opencv2/core/va_intel.hpp"
#ifdef HAVE_VA_INTEL_OLD_HEADER
#include <CL/va_ext.h>
#else
#include <CL/cl_va_api_media_sharing_intel.h>
#endif
#endif
#ifdef HAVE_OPENCL
#include "opencv2/core/opencl/runtime/opencl_core.hpp"
#include "opencv2/core.hpp"
#include "opencv2/core/ocl.hpp"
#endif

#define HW_FRAMES_POOL_SIZE     20
#define HW_DEVICE_NOT_SET       -1
#define HW_DEVICE_FROM_EXISTENT -2

#ifdef __cplusplus
extern "C" {
#endif

#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#ifdef HAVE_DIRECTX
#include <libavutil/hwcontext_d3d11va.h>
#endif
#ifdef HAVE_VA
#include <libavutil/hwcontext_vaapi.h>
#endif
#ifdef HAVE_MFX
#include <libavutil/hwcontext_qsv.h>
#endif

#ifdef __cplusplus
}
#endif

using namespace cv;

static struct {
    VideoAccelerationType type;
    AVHWDeviceType ffmpeg_type;
} hw_device_types[] = {
    { VIDEO_ACCELERATION_QSV, AV_HWDEVICE_TYPE_QSV },
    { VIDEO_ACCELERATION_D3D11, AV_HWDEVICE_TYPE_D3D11VA },
    { VIDEO_ACCELERATION_VAAPI, AV_HWDEVICE_TYPE_VAAPI }
};

#ifdef HAVE_VA
static VADisplay hw_get_va_display(AVHWDeviceContext* hw_device_ctx) {
    if (hw_device_ctx->type == AV_HWDEVICE_TYPE_VAAPI) {
        return ((AVVAAPIDeviceContext *) hw_device_ctx->hwctx)->display;
    }
#ifdef HAVE_MFX
    else if (hw_device_ctx->type == AV_HWDEVICE_TYPE_QSV) {
        mfxSession session = ((AVQSVDeviceContext *) hw_device_ctx->hwctx)->session;
        mfxHDL hdl = NULL;
        MFXVideoCORE_GetHandle(session, MFX_HANDLE_VA_DISPLAY, &hdl);
        return (VADisplay)hdl;
    }
#endif
    return NULL;
}

static VASurfaceID hw_get_va_surface(AVFrame *picture) {
    if (picture->format == AV_PIX_FMT_VAAPI) {
        return (VASurfaceID) (size_t) picture->data[3]; // As defined by AV_PIX_FMT_VAAPI
    }
#ifdef HAVE_MFX
    else if (picture->format == AV_PIX_FMT_QSV) {
        mfxFrameSurface1 *surface = (mfxFrameSurface1 *) picture->data[3]; // As defined by AV_PIX_FMT_QSV
        return *(VASurfaceID *) surface->Data.MemId;
    }
#endif
    return VA_INVALID_SURFACE;
}
#endif

#ifdef HAVE_DIRECTX
static ID3D11Device* hw_get_d3d11_device(AVHWDeviceContext* hw_device_ctx) {
    if (hw_device_ctx->type == AV_HWDEVICE_TYPE_D3D11) {
        return ((AVD3D11VADeviceContext *) hw_device_ctx->hwctx)->device;
    }
#ifdef HAVE_MFX
    else if (hw_device_ctx->type == AV_HWDEVICE_TYPE_QSV) {
        mfxSession session = ((AVQSVDeviceContext *) hw_device_ctx->hwctx)->session;
        mfxHDL hdl = NULL;
        MFXVideoCORE_GetHandle(session, MFX_HANDLE_D3D11_DEVICE, &hdl);
        return (ID3D11Device*)hdl;
    }
#endif
    return NULL;
}

static ID3D11Texture2D* hw_get_d3d11_texture(AVFrame *picture) {
    if (picture->format == AV_PIX_FMT_D3D11) {
        return (ID3D11Texture2D*)picture->data[0]; // As defined by AV_PIX_FMT_D3D11
    }
#ifdef HAVE_MFX
    else if (picture->format == AV_PIX_FMT_QSV) {
        mfxFrameSurface1 *surface = (mfxFrameSurface1 *) picture->data[3]; // As defined by AV_PIX_FMT_QSV
        return (ID3D11Texture2D*) surface->Data.MemId;
    }
#endif
    return NULL;
}
#endif

static void hw_bind_device(AVBufferRef *ctx) {
    if (!ctx)
        return;
    AVHWDeviceContext *hw_device_ctx = (AVHWDeviceContext *) ctx->data;
    if (!hw_device_ctx)
        return;
#ifdef HAVE_VA_INTEL
    VADisplay va_display = hw_get_va_display(hw_device_ctx);
    if (va_display) {
        va_intel::ocl::initializeContextFromVA(va_display);
    }
#endif
#if defined(HAVE_DIRECTX) && defined(HAVE_OPENCL)
    ID3D11Device *device = hw_get_d3d11_device(hw_device_ctx);
    if (device) {
        directx::ocl::initializeContextFromD3D11Device(device);
    }
#endif
}

static AVBufferRef *hw_create_device_from_existent(ocl::OpenCLExecutionContext& ocl_context, AVHWDeviceType hw_type) {
    if (ocl_context.empty())
        return nullptr;
    AVHWDeviceType base_hw_type = hw_type;
    if (hw_type == AV_HWDEVICE_TYPE_QSV) {
#ifdef _WIN32
        base_hw_type = AV_HWDEVICE_TYPE_D3D11VA;
#else
        base_hw_type = AV_HWDEVICE_TYPE_VAAPI;
#endif
    }
    intptr_t cl_media_property = 0;
#ifdef HAVE_VA_INTEL
    if (base_hw_type == AV_HWDEVICE_TYPE_VAAPI) {
        cl_media_property = CL_CONTEXT_VA_API_DISPLAY_INTEL;
    }
#endif
#ifdef HAVE_DIRECTX
    if (base_hw_type == AV_HWDEVICE_TYPE_D3D11VA)
        cl_media_property = CL_CONTEXT_D3D11_DEVICE_KHR;
    }
#endif
    if (!cl_media_property)
        return NULL;
    void *media_device = NULL;
    cl_context context = (cl_context) ocl_context.getContext().ptr();
    ::size_t size = 0;
    clGetContextInfo(context, CL_CONTEXT_PROPERTIES, 0, NULL, &size);
    std::vector<cl_context_properties> prop(size / sizeof(cl_context_properties));
    clGetContextInfo(context, CL_CONTEXT_PROPERTIES, size, prop.data(), NULL);
    for (size_t i = 0; i < prop.size(); i += 2) {
        if (prop[i] == cl_media_property) {
            media_device = (void *) prop[i + 1];
            break;
        }
    }
    if (!media_device)
        return NULL;
    AVBufferRef *ctx = av_hwdevice_ctx_alloc(base_hw_type);
    if (!ctx)
        return NULL;
    AVHWDeviceContext *hw_device_ctx = (AVHWDeviceContext *) ctx->data;
#ifdef HAVE_VA
    if (base_hw_type == AV_HWDEVICE_TYPE_VAAPI) {
        ((AVVAAPIDeviceContext *) hw_device_ctx->hwctx)->display = (VADisplay)media_device;
    }
#endif
#ifdef HAVE_DIRECTX
    if (base_hw_type == AV_HWDEVICE_TYPE_VAAPI) {
        ((AVD3D11VADeviceContext *) hw_device_ctx->hwctx)->device = (ID3D11Device*)media_device;
    }
#endif
    if (av_hwdevice_ctx_init(ctx) < 0) {
        return NULL;
    }
    if (hw_type != base_hw_type) {
        AVBufferRef *derived_ctx = NULL;
        av_hwdevice_ctx_create_derived(&derived_ctx, hw_type, ctx, 0);
        av_buffer_unref(&ctx);
        return derived_ctx;
    }
    return ctx;
}

// Parameters hw_type and hw_device are input+output
// The function returns HW device context (or NULL), and updates hw_type and hw_device to specific type/device
static AVBufferRef *hw_create_device(VideoAccelerationType *hw_type, int* hw_device) {
    if (*hw_type == VIDEO_ACCELERATION_NONE)
        return NULL;

    ocl::OpenCLExecutionContext& ocl_context = ocl::OpenCLExecutionContext::getCurrentRef();

    char device[128] = "";
    for (size_t i = 0; i < sizeof(hw_device_types) / sizeof(hw_device_types[0]); i++) {
        if (!(*hw_type & hw_device_types[i].type))
            continue;
        AVHWDeviceType device_type = hw_device_types[i].ffmpeg_type;

        AVBufferRef *hw_device_ctx = hw_create_device_from_existent(ocl_context, device_type);
        if (hw_device_ctx != NULL) {
            *hw_type = hw_device_types[i].type;
            *hw_device = HW_DEVICE_FROM_EXISTENT;
            return hw_device_ctx;
        }

        // create new media context and bind to new OpenCL context only if OpenCL context not set yet
        if (ocl_context.empty()) {
            char *pdevice = NULL;
            if (*hw_device >= 0 && *hw_device < 100000) {
                if (device_type == AV_HWDEVICE_TYPE_VAAPI)
                    snprintf(device, sizeof(device), "/dev/dri/renderD%d", 128 + *hw_device);
                else
                    snprintf(device, sizeof(device), "%d", *hw_device);
                pdevice = device;
            }
            if (av_hwdevice_ctx_create(&hw_device_ctx, device_type, pdevice, NULL, 0) == 0) {
                hw_bind_device(hw_device_ctx);
                *hw_type = hw_device_types[i].type;
                if (*hw_device < 0)
                    *hw_device = 0;
                return hw_device_ctx;
            }
        }
    }
    *hw_type = VIDEO_ACCELERATION_NONE;
    return NULL;
}

static AVBufferRef* hw_create_frames(AVBufferRef *hw_device_ctx, int width, int height, AVPixelFormat format, int pool_size = HW_FRAMES_POOL_SIZE)
{
    AVBufferRef *hw_frames_ref = av_hwframe_ctx_alloc(hw_device_ctx);
    if (!hw_frames_ref) {
        fprintf(stderr, "Failed to create HW frame context\n");
        return NULL;
    }
    AVHWFramesContext *frames_ctx = (AVHWFramesContext *)(hw_frames_ref->data);
    frames_ctx->format    = format;
    frames_ctx->sw_format = AV_PIX_FMT_NV12;
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

static AVPixelFormat hw_get_codec_native_format(AVCodec* codec, AVBufferRef *hw_device_ctx) {
    AVHWDeviceType hw_type = ((AVHWDeviceContext *) hw_device_ctx->data)->type;
    if (hw_type == AV_HWDEVICE_TYPE_VAAPI) {
        return AV_PIX_FMT_VAAPI;
    } else if (hw_type == AV_HWDEVICE_TYPE_D3D11VA) {
        return AV_PIX_FMT_D3D11;
    } else if (hw_type == AV_HWDEVICE_TYPE_QSV) {
        return AV_PIX_FMT_QSV;
    }
    if (codec && codec->pix_fmts)
        return codec->pix_fmts[0];
    return AV_PIX_FMT_NONE;
}

static const struct {
    int codec_id;
    const char *string;
} ffmpeg_codec_id_string[] = {
        { AV_CODEC_ID_H263, "h263" },
        { AV_CODEC_ID_H264, "h264" },
        { AV_CODEC_ID_HEVC, "hevc" },
        { AV_CODEC_ID_MPEG2VIDEO, "mpeg2" },
        { AV_CODEC_ID_MPEG4, "mpeg4" },
        { AV_CODEC_ID_VC1, "vc1" },
        { AV_CODEC_ID_VP8, "vp8" },
        { AV_CODEC_ID_VP9, "vp9" },
        { AV_CODEC_ID_WMV3, "wmv3" },
        { AV_CODEC_ID_AV1, "av1" }
};

static std::string hw_get_codec_name(int codec_id, VideoAccelerationType hw_type) {
    std::string name;
    for (size_t i = 0; i < sizeof(ffmpeg_codec_id_string)/sizeof(ffmpeg_codec_id_string[0]); i++) {
        if (ffmpeg_codec_id_string[i].codec_id == codec_id) {
            name = ffmpeg_codec_id_string[i].string;
            break;
        }
    }
    if (name.empty()) {
        return "";
    }
    if (hw_type == VIDEO_ACCELERATION_VAAPI) {
        name += "_vaapi";
    } else if (hw_type == VIDEO_ACCELERATION_D3D11) {
        name += "_d3d11va"; // "_d3d11va2"
    } else if (hw_type == VIDEO_ACCELERATION_QSV) {
        name += "_qsv";
    } else {
        return "";
    }
    return name;
}

// In case of QSV we have to set this callback to select hardware format AV_PIX_FMT_QSV, not software format which is
// first in the list of supported formats. Also in case of QSV we have to allocate frame pool.
static enum AVPixelFormat hw_qsv_format_callback(struct AVCodecContext *ctx, const enum AVPixelFormat * fmt) {
    for (int i = 0; ;i++) {
        if (fmt[i] == AV_PIX_FMT_QSV) {
            ctx->hw_frames_ctx = hw_create_frames(ctx->hw_device_ctx, ctx->width, ctx->height, AV_PIX_FMT_QSV);
            if (ctx->hw_frames_ctx)
                return AV_PIX_FMT_QSV;
        }
        if (!fmt[i])
            return fmt[0];
    }
}

// GPU color conversion NV12->BGRA via OpenCL extensions
bool hw_copy_media_to_opencl(AVHWDeviceContext* hw_device_ctx, AVFrame* picture, cv::OutputArray output) {
#ifdef HAVE_VA_INTEL
    VADisplay va_display = hw_get_va_display(hw_device_ctx);
    VASurfaceID va_surface = hw_get_va_surface(picture);
    if (va_display != NULL && va_surface != VA_INVALID_SURFACE) {
        Size size(picture->width, picture->height);
        va_intel::convertFromVASurface(va_display, va_surface, size, output);
        return true;
    }
#endif

#ifdef HAVE_DIRECTX
    ID3D11Device* pD3D11Device = hw_get_d3d11_device(hw_device_ctx);
        ID3D11Texture2D* pD3D11Texture = hw_get_d3d11_texture(picture);
        if (pD3D11Device && pD3D11Texture) {
            directx::convertFromD3D11Texture2D(pD3D11Texture2D, output);
            return true;
        }
#endif
    return false;
}

// GPU color conversion BGRA->NV12 via OpenCL extensions
bool hw_copy_opencl_to_media(AVHWDeviceContext* hw_device_ctx, cv::InputArray input, AVFrame* hw_frame) {
#ifdef HAVE_VA_INTEL
    VADisplay va_display = hw_get_va_display(hw_device_ctx);
    VASurfaceID va_surface = hw_get_va_surface(hw_frame);
    if (va_display != NULL && va_surface != VA_INVALID_SURFACE) {
        Size size(hw_frame->width, hw_frame->height);
        va_intel::convertToVASurface(va_display, input, va_surface, size);
        return true;
    }
#endif

#ifdef HAVE_DIRECTX
    ID3D11Device* pD3D11Device = hw_get_d3d11_device(hw_device_ctx);
    ID3D11Texture2D* pD3D11Texture = hw_get_d3d11_texture(hw_frame);
    if (pD3D11Device && pD3D11Texture) {
        directx::convertToD3D10Texture2D(input, pD3D11Texture);
        return true;
    }
#endif
    return false;
}
