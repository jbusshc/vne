#include "rhi/rhi.h"

#if defined(SZ_RHI_BACKEND_D3D11)

#define WIN32_LEAN_AND_MEAN
#include <SDL3/SDL.h>
#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>

#include "core/log.h"

namespace {

ID3D11Device*           g_device              = nullptr;
ID3D11DeviceContext*    g_context             = nullptr;
IDXGISwapChain*         g_swapchain           = nullptr;
ID3D11Texture2D*        g_backbuffer          = nullptr;
ID3D11RenderTargetView* g_render_target_view  = nullptr;
i32                     g_rtv_width           = 0;
i32                     g_rtv_height          = 0;

void release_render_target() {
    if (g_render_target_view != nullptr) {
        g_render_target_view->Release();
        g_render_target_view = nullptr;
    }
    if (g_backbuffer != nullptr) {
        g_backbuffer->Release();
        g_backbuffer = nullptr;
    }
}

bool create_render_target() {
    HRESULT hr = g_swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                         reinterpret_cast<void**>(&g_backbuffer));
    if (FAILED(hr)) {
        log_error("D3D11: GetBuffer fallo (hr=0x%08lX)", static_cast<unsigned long>(hr));
        return false;
    }
    hr = g_device->CreateRenderTargetView(g_backbuffer, nullptr, &g_render_target_view);
    if (FAILED(hr)) {
        log_error("D3D11: CreateRenderTargetView fallo (hr=0x%08lX)",
                  static_cast<unsigned long>(hr));
        return false;
    }
    return true;
}

}  // namespace

bool rhi_init(PlatformWindow* window) {
    void* hwnd_ptr = SDL_GetPointerProperty(SDL_GetWindowProperties(window->sdl_window),
                                             SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    if (hwnd_ptr == nullptr) {
        log_error("D3D11: no se pudo obtener el HWND de la ventana SDL");
        return false;
    }
    HWND hwnd = static_cast<HWND>(hwnd_ptr);

    DXGI_SWAP_CHAIN_DESC sc_desc{};
    sc_desc.BufferCount       = 2;
    sc_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sc_desc.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sc_desc.OutputWindow      = hwnd;
    sc_desc.SampleDesc.Count  = 1;
    sc_desc.Windowed          = TRUE;
    // FLIP_DISCARD (en vez del modelo de "blit" DISCARD) evita el paso extra por el
    // compositor de escritorio (DWM) en modo ventana: menos latencia y overhead de
    // Present() por frame, visible en el criterio de fps de M1.
    sc_desc.SwapEffect        = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    D3D_FEATURE_LEVEL feature_level;
    HRESULT           hr = E_FAIL;
#if defined(SZ_DEBUG)
    // La capa de depuracion D3D11 requiere el componente opcional de Windows "Graphics
    // Tools", que no siempre esta instalado. Si falta, DXGI_ERROR_SDK_COMPONENT_MISSING
    // no debe tumbar el motor: se reintenta sin la capa de depuracion.
    hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                        D3D11_CREATE_DEVICE_DEBUG, nullptr, 0,
                                        D3D11_SDK_VERSION, &sc_desc, &g_swapchain, &g_device,
                                        &feature_level, &g_context);
    if (FAILED(hr)) {
        log_warn("D3D11 capa de depuracion no disponible (hr=0x%08lX); reintentando sin ella",
                 static_cast<unsigned long>(hr));
    }
#endif
    if (FAILED(hr)) {
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                            nullptr, 0, D3D11_SDK_VERSION, &sc_desc,
                                            &g_swapchain, &g_device, &feature_level, &g_context);
    }
    if (FAILED(hr)) {
        log_error("D3D11CreateDeviceAndSwapChain fallo (hr=0x%08lX)",
                  static_cast<unsigned long>(hr));
        return false;
    }

    return create_render_target();
}

void rhi_shutdown() {
    release_render_target();
    if (g_swapchain != nullptr) {
        g_swapchain->Release();
        g_swapchain = nullptr;
    }
    if (g_context != nullptr) {
        g_context->Release();
        g_context = nullptr;
    }
    if (g_device != nullptr) {
        g_device->Release();
        g_device = nullptr;
    }
    g_rtv_width  = 0;
    g_rtv_height = 0;
}

sg_environment rhi_environment() {
    sg_environment env{};
    env.defaults.color_format = SG_PIXELFORMAT_RGBA8;
    env.defaults.depth_format = SG_PIXELFORMAT_NONE;
    env.defaults.sample_count = 1;
    env.d3d11.device          = g_device;
    env.d3d11.device_context  = g_context;
    return env;
}

sg_swapchain rhi_begin_frame(i32 window_w, i32 window_h) {
    if (window_w != g_rtv_width || window_h != g_rtv_height) {
        release_render_target();
        HRESULT hr = g_swapchain->ResizeBuffers(0, static_cast<UINT>(window_w),
                                                 static_cast<UINT>(window_h),
                                                 DXGI_FORMAT_UNKNOWN, 0);
        if (FAILED(hr)) {
            log_error("D3D11: ResizeBuffers fallo (hr=0x%08lX)", static_cast<unsigned long>(hr));
        }
        create_render_target();
        g_rtv_width  = window_w;
        g_rtv_height = window_h;
    }

    sg_swapchain sc{};
    sc.width              = window_w;
    sc.height             = window_h;
    sc.sample_count       = 1;
    sc.color_format       = SG_PIXELFORMAT_RGBA8;
    sc.depth_format       = SG_PIXELFORMAT_NONE;
    sc.d3d11.render_view  = g_render_target_view;
    return sc;
}

void rhi_present() {
    g_swapchain->Present(1, 0);
}

bool rhi_capture_thumbnail(sg_image scene_image, u8* out_rgb, i32 out_w, i32 out_h) {
    sg_d3d11_image_info info = sg_d3d11_query_image_info(scene_image);
    if (info.res == nullptr) {
        log_error("rhi_capture_thumbnail: sg_d3d11_query_image_info sin recurso");
        return false;
    }
    auto* src_tex = static_cast<ID3D11Texture2D*>(const_cast<void*>(info.res));

    D3D11_TEXTURE2D_DESC src_desc{};
    src_tex->GetDesc(&src_desc);

    D3D11_TEXTURE2D_DESC staging_desc = src_desc;
    staging_desc.Usage          = D3D11_USAGE_STAGING;
    staging_desc.BindFlags      = 0;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staging_desc.MiscFlags      = 0;

    ID3D11Texture2D* staging = nullptr;
    HRESULT          hr      = g_device->CreateTexture2D(&staging_desc, nullptr, &staging);
    if (FAILED(hr)) {
        log_error("rhi_capture_thumbnail: CreateTexture2D (staging) fallo (hr=0x%08lX)",
                  static_cast<unsigned long>(hr));
        return false;
    }

    g_context->CopyResource(staging, src_tex);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = g_context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        log_error("rhi_capture_thumbnail: Map fallo (hr=0x%08lX)",
                  static_cast<unsigned long>(hr));
        staging->Release();
        return false;
    }

    const u8* src_pixels = static_cast<const u8*>(mapped.pData);
    i32       src_w       = static_cast<i32>(src_desc.Width);
    i32       src_h       = static_cast<i32>(src_desc.Height);
    for (i32 y = 0; y < out_h; ++y) {
        i32 src_y = (y * src_h) / out_h;
        for (i32 x = 0; x < out_w; ++x) {
            i32       src_x = (x * src_w) / out_w;
            const u8* px    = src_pixels + static_cast<usize>(src_y) * mapped.RowPitch +
                            static_cast<usize>(src_x) * 4;
            u8* dst = out_rgb + (static_cast<usize>(y) * static_cast<usize>(out_w) +
                                  static_cast<usize>(x)) *
                                     3;
            dst[0] = px[0];
            dst[1] = px[1];
            dst[2] = px[2];
        }
    }

    g_context->Unmap(staging, 0);
    staging->Release();
    return true;
}

#endif  // SZ_RHI_BACKEND_D3D11
