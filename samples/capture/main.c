/*
 * cwinrt sample: Windows.Graphics.Capture end-to-end pipeline (pure C).
 *
 * Builds a real capture pipeline: GraphicsCaptureItem (via interop) for a window,
 * a hardware Direct3D11 device, a free-threaded Direct3D11CaptureFramePool, a
 * GraphicsCaptureSession, then StartCapture.
 *
 * Runtime: requires a Windows 10/11 desktop with a GPU. In CI this is COMPILE-ONLY
 * (no device/GPU present). Must build clean under MSVC /W4 and clang -Wall -Wextra (C17).
 *
 * Link libraries (the build adds these): d3d11, dxgi.
 */

#include <cwinrt/init.h>
#include <cwinrt/cast.h>
#include <cwinrt/factory.h>
#include <cwinrt/hstring.h>
#include <cwinrt/Windows.Graphics.Capture.h>
#include <cwinrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <stdio.h>

/* Exported by d3d11.dll; the windows.ui.composition.interop header that declares
   it is not present in every SDK, so declare it directly. */
HRESULT WINAPI CreateDirect3D11DeviceFromDXGIDevice(IDXGIDevice *dxgiDevice,
                                                    IInspectable **graphicsDevice);

/* GraphicsCaptureItem is created through a classic-COM interop factory, not the
   projection. Declare the interop interface inline. */
#ifndef __IGraphicsCaptureItemInterop_DEFINED
#define __IGraphicsCaptureItemInterop_DEFINED
typedef struct IGraphicsCaptureItemInterop IGraphicsCaptureItemInterop;
typedef struct IGraphicsCaptureItemInteropVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IGraphicsCaptureItemInterop *, REFIID, void **);
    ULONG   (STDMETHODCALLTYPE *AddRef)(IGraphicsCaptureItemInterop *);
    ULONG   (STDMETHODCALLTYPE *Release)(IGraphicsCaptureItemInterop *);
    HRESULT (STDMETHODCALLTYPE *CreateForWindow)(IGraphicsCaptureItemInterop *, HWND, REFIID, void **);
    HRESULT (STDMETHODCALLTYPE *CreateForMonitor)(IGraphicsCaptureItemInterop *, HMONITOR, REFIID, void **);
} IGraphicsCaptureItemInteropVtbl;
struct IGraphicsCaptureItemInterop { const IGraphicsCaptureItemInteropVtbl *lpVtbl; };
static const GUID IID_IGraphicsCaptureItemInterop =
    { 0x3628E81B, 0x3CAC, 0x4C60, { 0xB7, 0xF4, 0x23, 0xCE, 0x0E, 0x0C, 0x33, 0x56 } };
#endif

#define CHECK(hr, what)                                                  \
    do {                                                                 \
        HRESULT _hr = (hr);                                              \
        if (FAILED(_hr)) {                                               \
            printf("FAIL %s: hr=0x%08lX\n", (what), (unsigned long)_hr); \
            goto cleanup;                                                \
        }                                                                \
    } while (0)

int main(void)
{
    IGraphicsCaptureItemInterop  *interop = NULL;
    WGC_GraphicsCaptureItem      *item = NULL;
    ID3D11Device                 *d3dDevice = NULL;
    IDXGIDevice                  *dxgiDevice = NULL;
    IInspectable                 *inspectable = NULL;
    WGRDIDI_IDirect3DDevice      *d3dWinrtDevice = NULL;
    WGC_Direct3D11CaptureFramePool *pool = NULL;
    WGC_GraphicsCaptureSession   *session = NULL;
    cwinrt_hstring                displayName = NULL;
    WG_SizeInt32                  size = { 0, 0 };
    char                          nameBuf[256];
    HWND                          hwnd;
    int                           rc = 1;

    /* Capture is free-threaded; an MTA apartment is fine. */
    HRESULT hr = cwinrt_init(RO_INIT_MULTITHREADED);
    if (FAILED(hr)) {
        printf("FAIL cwinrt_init: 0x%08lX\n", (unsigned long)hr);
        return 1;
    }

    hwnd = GetDesktopWindow();
    if (hwnd == NULL) {
        printf("FAIL GetDesktopWindow\n");
        goto cleanup;
    }

    /* 1. GraphicsCaptureItem via the interop factory. */
    CHECK(cwinrt_factory_get_statics(L"Windows.Graphics.Capture.GraphicsCaptureItem",
                                     &IID_IGraphicsCaptureItemInterop,
                                     (void **)&interop),
          "get IGraphicsCaptureItemInterop");
    CHECK(interop->lpVtbl->CreateForWindow(interop, hwnd,
                                           &CWINRT_IID_WGC_IGraphicsCaptureItem,
                                           (void **)&item),
          "CreateForWindow");

    CHECK(wgc_graphics_capture_item_get__display_name(item, &displayName),
          "get DisplayName");
    CHECK(wgc_graphics_capture_item_get__size(item, &size), "get Size");

    /* 2. Hardware Direct3D11 device; BGRA support required for capture. */
    CHECK(D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL,
                            D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
                            D3D11_SDK_VERSION, &d3dDevice, NULL, NULL),
          "D3D11CreateDevice");
    CHECK(d3dDevice->lpVtbl->QueryInterface(d3dDevice, &IID_IDXGIDevice,
                                            (void **)&dxgiDevice),
          "QI IDXGIDevice");
    CHECK(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice, &inspectable),
          "CreateDirect3D11DeviceFromDXGIDevice");
    /* Project the IInspectable into the WinRT IDirect3DDevice the pool wants. */
    CHECK(cwinrt_query(inspectable, &CWINRT_IID_WGRDIDI_IDirect3DDevice,
                       (void **)&d3dWinrtDevice),
          "cwinrt_query IDirect3DDevice");

    /* 3. Free-threaded frame pool (2 buffers, BGRA8, item's size). This is a
       static-method-style helper: it takes the device, not a pool instance. */
    CHECK(wgc_direct3_d11_capture_frame_pool_create_free_threaded(
              d3dWinrtDevice,
              WGRDI_DirectXPixelFormat_B8G8R8A8UIntNormalized,
              2, size, &pool),
          "CreateFreeThreaded");

    /* 4. Session from pool + item, then start. */
    CHECK(wgc_direct3_d11_capture_frame_pool_create_capture_session(pool, item, &session),
          "CreateCaptureSession");
    CHECK(wgc_graphics_capture_session_start_capture(session), "StartCapture");

    if (cwinrt_hstring_to_utf8(displayName, nameBuf, (int)sizeof(nameBuf)) < 0)
        nameBuf[0] = '\0';
    printf("PASS capture: item=\"%s\" size=%dx%d, capturing...\n",
           nameBuf, size.Width, size.Height);

    /* Pump messages briefly so frames can arrive on a real desktop. */
    {
        MSG msg;
        DWORD start = GetTickCount();
        while (GetTickCount() - start < 500) {
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            Sleep(10);
        }
    }
    rc = 0;

cleanup:
    if (session)
        ((IUnknown *)session)->lpVtbl->Release((IUnknown *)session);
    if (pool)
        ((IUnknown *)pool)->lpVtbl->Release((IUnknown *)pool);
    if (d3dWinrtDevice)
        ((IUnknown *)d3dWinrtDevice)->lpVtbl->Release((IUnknown *)d3dWinrtDevice);
    if (inspectable)
        inspectable->lpVtbl->Release(inspectable);
    if (dxgiDevice)
        dxgiDevice->lpVtbl->Release(dxgiDevice);
    if (d3dDevice)
        d3dDevice->lpVtbl->Release(d3dDevice);
    if (item)
        ((IUnknown *)item)->lpVtbl->Release((IUnknown *)item);
    if (interop)
        interop->lpVtbl->Release(interop);
    cwinrt_hstring_free(displayName);
    cwinrt_uninit();
    return rc;
}
