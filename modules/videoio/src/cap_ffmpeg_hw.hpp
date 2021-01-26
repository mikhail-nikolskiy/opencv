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

#ifdef __cplusplus
extern "C" {
#endif
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#ifdef __cplusplus
}
#endif

#define HW_FRAMES_POOL_SIZE     20

AVBufferRef *hw_create_device(cv::VideoAccelerationType *hw_type, int* hw_device);
AVBufferRef* hw_create_frames(AVBufferRef *hw_device_ctx, int width, int height, AVPixelFormat hw_format, AVPixelFormat sw_format, int pool_size = HW_FRAMES_POOL_SIZE);
AVCodec *hw_find_codec(AVCodecID id, AVBufferRef *hw_device_ctx, int (*check_category)(const AVCodec *), AVPixelFormat *hw_pix_fmt = NULL);
AVPixelFormat hw_get_format_callback(struct AVCodecContext *ctx, const enum AVPixelFormat * fmt);
bool hw_copy_media_to_opencl(AVBufferRef* hw_device_ctx, AVFrame* picture, cv::OutputArray output);
bool hw_copy_opencl_to_media(AVBufferRef* hw_device_ctx, cv::InputArray input, AVFrame* hw_frame);

#ifdef HAVE_DIRECTX
#pragma comment (lib, "dxva2.lib")
#define D3D11_NO_HELPERS
#include "opencv2/core/directx.hpp"
#include <d3d9.h>
#include <d3d11.h>
#ifdef HAVE_OPENCL
#include <CL/cl_dx9_media_sharing.h>
#include <CL/cl_d3d11.h>
#endif
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

#define HW_DEVICE_NOT_SET       -1
#define HW_DEVICE_FROM_EXISTENT -2

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAVE_DIRECTX
#include <libavutil/hwcontext_dxva2.h>
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
static IDirect3DDeviceManager9* hw_get_d3d9_device_manager(AVHWDeviceContext* hw_device_ctx) {
    if (hw_device_ctx->type == AV_HWDEVICE_TYPE_DXVA2) {
        return ((AVDXVA2DeviceContext*)hw_device_ctx->hwctx)->devmgr;
    }
#ifdef HAVE_MFX
    else if (hw_device_ctx->type == AV_HWDEVICE_TYPE_QSV) {
        mfxSession session = ((AVQSVDeviceContext*)hw_device_ctx->hwctx)->session;
        mfxHDL hdl = NULL;
        MFXVideoCORE_GetHandle(session, MFX_HANDLE_D3D9_DEVICE_MANAGER, &hdl);
        return (IDirect3DDeviceManager9*)hdl;
    }
#endif
    return NULL;
}

static IDirect3DSurface9* hw_get_d3d9_texture(AVFrame* picture) {
    if (picture->format == AV_PIX_FMT_DXVA2_VLD) {
        return (IDirect3DSurface9*)picture->data[3]; // As defined by AV_PIX_FMT_DXVA2_VLD
    }
#ifdef HAVE_MFX
    else if (picture->format == AV_PIX_FMT_QSV) {
        mfxFrameSurface1* surface = (mfxFrameSurface1*)picture->data[3]; // As defined by AV_PIX_FMT_QSV
        return (IDirect3DSurface9*)surface->Data.MemId;
    }
#endif
    return NULL;
}

static ID3D11Device* hw_get_d3d11_device(AVHWDeviceContext* hw_device_ctx) {
    if (hw_device_ctx->type == AV_HWDEVICE_TYPE_D3D11VA) {
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

static void hw_bind_opencl(AVBufferRef *ctx) {
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
    IDirect3DDeviceManager9* device_manager = hw_get_d3d9_device_manager(hw_device_ctx);
    if (device_manager) {
        HANDLE hDevice = 0;
        if (SUCCEEDED(device_manager->OpenDeviceHandle(&hDevice))) {
            IDirect3DDevice9* pDevice = 0;
            if (SUCCEEDED(device_manager->LockDevice(hDevice, &pDevice, FALSE))) {
                directx::ocl::initializeContextFromDirect3DDevice9(pDevice);
                device_manager->UnlockDevice(hDevice, FALSE);
            }
            device_manager->CloseDeviceHandle(hDevice);
        }
    }
    //TODO check if need to do device->AddRef() or OpenCL context does that automatically
    //ID3D11Device *device = hw_get_d3d11_device(hw_device_ctx);
    //if (device) {
    //    directx::ocl::initializeContextFromD3D11Device(device);
    //}
#endif
}

static std::pair<AVHWDeviceType, void*> hw_query_opencl_context(ocl::OpenCLExecutionContext& ocl_context) {
    if (ocl_context.empty())
        return { AV_HWDEVICE_TYPE_NONE, NULL };
    static struct {
        AVHWDeviceType hw_type;
        intptr_t cl_property;
    } cl_media_properties[] = {
#ifdef HAVE_VA_INTEL
        { AV_HWDEVICE_TYPE_VAAPI, CL_CONTEXT_VA_API_DISPLAY_INTEL },
#endif
#ifdef HAVE_DIRECTX
        { AV_HWDEVICE_TYPE_DXVA2, CL_CONTEXT_ADAPTER_D3D9_KHR },
        { AV_HWDEVICE_TYPE_D3D11VA, CL_CONTEXT_D3D11_DEVICE_KHR },
#endif
    };
    cl_context context = (cl_context)ocl_context.getContext().ptr();
    ::size_t size = 0;
    clGetContextInfo(context, CL_CONTEXT_PROPERTIES, 0, NULL, &size);
    std::vector<cl_context_properties> prop(size / sizeof(cl_context_properties));
    clGetContextInfo(context, CL_CONTEXT_PROPERTIES, size, prop.data(), NULL);
    for (size_t i = 0; i < prop.size(); i += 2) {
        for (size_t j = 0; j < sizeof(cl_media_properties) / sizeof(cl_media_properties[0]); j++) {
            if (prop[i] == cl_media_properties[j].cl_property) {
                return { cl_media_properties[j].hw_type, (void*)prop[i + 1] };
            }
        }
    }
    return { AV_HWDEVICE_TYPE_NONE, NULL };
}

static AVBufferRef *hw_create_device_from_existent(ocl::OpenCLExecutionContext& ocl_context, AVHWDeviceType hw_type) {
    std::pair<AVHWDeviceType, void*> q = hw_query_opencl_context(ocl_context);
    if (q.first == AV_HWDEVICE_TYPE_NONE || !q.second)
        return NULL;
    AVHWDeviceType base_hw_type = q.first;
    AVBufferRef *ctx = av_hwdevice_ctx_alloc(base_hw_type);
    if (!ctx)
        return NULL;
    AVHWDeviceContext *hw_device_ctx = (AVHWDeviceContext *) ctx->data;
#ifdef HAVE_VA
    if (base_hw_type == AV_HWDEVICE_TYPE_VAAPI) {
        ((AVVAAPIDeviceContext *) hw_device_ctx->hwctx)->display = (VADisplay)q.second;
    }
#endif
#ifdef HAVE_DIRECTX
    if (base_hw_type == AV_HWDEVICE_TYPE_D3D11VA) {
        ((AVD3D11VADeviceContext*)hw_device_ctx->hwctx)->device = (ID3D11Device*)q.second;
    } else
    if (base_hw_type == AV_HWDEVICE_TYPE_DXVA2) {
        IDirect3DDevice9* pDevice = (IDirect3DDevice9*)q.second;
        IDirect3DDeviceManager9* pD3DManager = NULL;
        UINT resetToken = 0;
        if (SUCCEEDED(DXVA2CreateDirect3DDeviceManager9(&resetToken, &pD3DManager))) {
            if (SUCCEEDED(pD3DManager->ResetDevice(pDevice, resetToken))) {
                ((AVDXVA2DeviceContext*)hw_device_ctx->hwctx)->devmgr = pD3DManager;
            }
        }
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

static bool hw_check_opencl_context(AVHWDeviceContext* ctx) {
    ocl::OpenCLExecutionContext& ocl_context = ocl::OpenCLExecutionContext::getCurrentRef();
    if (!ctx || ocl_context.empty())
        return false;
    std::pair<AVHWDeviceType, void*> q = hw_query_opencl_context(ocl_context);
    AVHWDeviceType hw_type = q.first;
    if (hw_type == AV_HWDEVICE_TYPE_NONE || q.second == NULL)
        return false;
#ifdef HAVE_VA
    if (hw_type == AV_HWDEVICE_TYPE_VAAPI) {
        return (hw_get_va_display(ctx) == (VADisplay)q.second);
    }
#endif
#ifdef HAVE_DIRECTX
    if (hw_type == AV_HWDEVICE_TYPE_D3D11VA) {
        return (hw_get_d3d11_device(ctx) == (ID3D11Device*)q.second);
    }
    else if (hw_type == AV_HWDEVICE_TYPE_DXVA2) {
        bool device_matched = false;
        IDirect3DDeviceManager9* device_manager = hw_get_d3d9_device_manager(ctx);
        HANDLE hDevice = 0;
        if (device_manager && SUCCEEDED(device_manager->OpenDeviceHandle(&hDevice))) {
            IDirect3DDevice9* pDevice = 0;
            if (SUCCEEDED(device_manager->LockDevice(hDevice, &pDevice, FALSE))) {
                device_matched = (pDevice == (IDirect3DDevice9*)q.second);
                device_manager->UnlockDevice(hDevice, FALSE);
            }
            device_manager->CloseDeviceHandle(hDevice);
        }
        return device_matched;
    }
#endif
    return false;
}

// Parameters hw_type and hw_device are input+output
// The function returns HW device context (or NULL), and updates hw_type and hw_device to specific type/device
AVBufferRef *hw_create_device(VideoAccelerationType *hw_type, int* hw_device) {
    if (*hw_type == VIDEO_ACCELERATION_NONE)
        return NULL;

    ocl::OpenCLExecutionContext& ocl_context = ocl::OpenCLExecutionContext::getCurrentRef();

    static struct {
        VideoAccelerationType type;
        AVHWDeviceType ffmpeg_type;
    } hw_device_types[] = {
        { VIDEO_ACCELERATION_QSV, AV_HWDEVICE_TYPE_QSV },
        { VIDEO_ACCELERATION_D3D11, AV_HWDEVICE_TYPE_D3D11VA },
        { VIDEO_ACCELERATION_D3D9, AV_HWDEVICE_TYPE_DXVA2 },
        { VIDEO_ACCELERATION_VAAPI, AV_HWDEVICE_TYPE_VAAPI }
    };

    char device[128] = "";
    for (size_t i = 0; i < sizeof(hw_device_types) / sizeof(hw_device_types[0]); i++) {
        if (!(*hw_type & hw_device_types[i].type))
            continue;
        AVHWDeviceType device_type = hw_device_types[i].ffmpeg_type;

        // try create media context on media device attached to OpenCL context
        AVBufferRef* hw_device_ctx = hw_create_device_from_existent(ocl_context, device_type);
        if (hw_device_ctx != NULL) {
            *hw_type = hw_device_types[i].type;
            *hw_device = HW_DEVICE_FROM_EXISTENT;
            return hw_device_ctx;
        }

        // create new media context
        char* pdevice = NULL;
        if (*hw_device >= 0 && *hw_device < 100000) {
            if (device_type == AV_HWDEVICE_TYPE_VAAPI) {
                snprintf(device, sizeof(device), "/dev/dri/renderD%d", 128 + *hw_device);
#ifdef HAVE_MFX
            } else if (device_type == AV_HWDEVICE_TYPE_QSV) {
                snprintf(device, sizeof(device), "%d", MFX_IMPL_HARDWARE_ANY + *hw_device);
#endif
            } else {
                snprintf(device, sizeof(device), "%d", *hw_device);
            }
            pdevice = device;
        }
        if (av_hwdevice_ctx_create(&hw_device_ctx, device_type, pdevice, NULL, 0) == 0) {
            // if OpenCL context not set yet, create it with binding to media device
            if (ocl_context.empty() && ocl::useOpenCL()) {
                hw_bind_opencl(hw_device_ctx);
            }
            *hw_type = hw_device_types[i].type;
            if (*hw_device < 0)
                *hw_device = 0;
            return hw_device_ctx;
        }
    }

    *hw_type = VIDEO_ACCELERATION_NONE;
    return NULL;
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
        return AV_PIX_FMT_YUV420P;
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
    return AV_PIX_FMT_YUV420P;
}

// GPU color conversion NV12->BGRA via OpenCL extensions
bool hw_copy_media_to_opencl(AVBufferRef* ctx, AVFrame* picture, cv::OutputArray output) {
    if (!ctx)
        return false;

    // check that current OpenCL context initilized with binding to our media context
    AVHWDeviceContext* hw_device_ctx = (AVHWDeviceContext*)ctx->data;
    if (!hw_check_opencl_context(hw_device_ctx))
        return false;

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
    IDirect3DDeviceManager9* pD3D9Device = hw_get_d3d9_device_manager(hw_device_ctx);
    IDirect3DSurface9* pD3D9Surface = hw_get_d3d9_texture(picture);
    if (pD3D9Device && pD3D9Surface) {
        directx::convertFromDirect3DSurface9(pD3D9Surface, output);
        return true;
    }

    ID3D11Device* pD3D11Device = hw_get_d3d11_device(hw_device_ctx);
    ID3D11Texture2D* pD3D11Texture = hw_get_d3d11_texture(picture);
    if (pD3D11Device && pD3D11Texture) {
        directx::convertFromD3D11Texture2D(pD3D11Texture, output);
        return true;
    }
#endif

    return false;
}

// GPU color conversion BGRA->NV12 via OpenCL extensions
bool hw_copy_opencl_to_media(AVBufferRef* ctx, cv::InputArray input, AVFrame* hw_frame) {
    if (!ctx)
        return false;

    // check that current OpenCL context initilized with binding to our media context
    AVHWDeviceContext* hw_device_ctx = (AVHWDeviceContext*)ctx->data;
    if (!hw_check_opencl_context(hw_device_ctx))
        return false;

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
    IDirect3DDeviceManager9* pD3D9Device = hw_get_d3d9_device_manager(hw_device_ctx);
    IDirect3DSurface9* pD3D9Surface = hw_get_d3d9_texture(hw_frame);
    if (pD3D9Device && pD3D9Surface) {
        directx::convertToDirect3DSurface9(input, pD3D9Surface);
        return true;
    }

    ID3D11Device* pD3D11Device = hw_get_d3d11_device(hw_device_ctx);
    ID3D11Texture2D* pD3D11Texture = hw_get_d3d11_texture(hw_frame);
    if (pD3D11Device && pD3D11Texture) {
        directx::convertToD3D11Texture2D(input, pD3D11Texture);
        return true;
    }
#endif
    return false;
}
