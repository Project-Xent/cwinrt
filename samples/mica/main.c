/*
 * samples/mica: a real Win32 top-level window with a Windows 11 Mica system
 * backdrop, plus a cwinrt Windows.UI.Composition backdrop-brush tree attached
 * to the window via the Desktop interop target.
 *
 * Pipeline (UI/STA thread):
 *   cwinrt_init(STA) -> bootstrap DispatcherQueue (Composition requires one)
 *   -> Compositor -> CompositionBackdropBrush -> SpriteVisual(brush, size)
 *   -> ICompositorDesktopInterop::CreateDesktopWindowTarget(hwnd)
 *   -> ICompositionTarget::put_Root(sprite)
 *   -> DwmSetWindowAttribute(DWMWA_SYSTEMBACKDROP_TYPE, Mica) -> message loop.
 *
 * RUNTIME requires a Windows 11 desktop session with a GPU; CI is compile-only.
 * Builds clean under MSVC /W4 and clang -Wall -Wextra (C17).
 */
#include <windows.h>
#include <dwmapi.h>

#include <cwinrt/init.h>
#include <cwinrt/cast.h>
#include <cwinrt/bootstrap.h>
#include <cwinrt/Windows.UI.Composition.h>

#include <stdio.h>

/* Windows.UI.Composition.Desktop interop (not WinRT-projected). */
#ifndef __ICompositorDesktopInterop_DEFINED
#define __ICompositorDesktopInterop_DEFINED
typedef struct ICompositorDesktopInterop ICompositorDesktopInterop;
typedef struct ICompositorDesktopInteropVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ICompositorDesktopInterop*, REFIID, void**);
    ULONG   (STDMETHODCALLTYPE *AddRef)(ICompositorDesktopInterop*);
    ULONG   (STDMETHODCALLTYPE *Release)(ICompositorDesktopInterop*);
    HRESULT (STDMETHODCALLTYPE *CreateDesktopWindowTarget)(ICompositorDesktopInterop*, HWND, BOOL topmost, void** result);
    HRESULT (STDMETHODCALLTYPE *EnsureOnThread)(ICompositorDesktopInterop*, DWORD threadId);
} ICompositorDesktopInteropVtbl;
struct ICompositorDesktopInterop { const ICompositorDesktopInteropVtbl* lpVtbl; };
static const GUID IID_ICompositorDesktopInterop =
    { 0x29E691FA, 0x4567, 0x4DCA, { 0xB3, 0x19, 0xD0, 0xF2, 0x07, 0xEB, 0x68, 0x07 } };
#endif

/* Windows 11 system backdrop; absent in older SDK headers. */
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#define DWMSBT_MAINWINDOW 2 /* Mica */

#define CHECK(hr, what)                                                  \
    do {                                                                 \
        HRESULT _hr = (hr);                                              \
        if (FAILED(_hr)) {                                               \
            fprintf(stderr, "FAIL %s: hr=0x%08lX\n", (what), (unsigned long)_hr); \
            goto done;                                                   \
        }                                                                \
    } while (0)

#define RELEASE(p)                                                       \
    do {                                                                 \
        if (p) {                                                         \
            ((IUnknown *)(p))->lpVtbl->Release((IUnknown *)(p));         \
            (p) = NULL;                                                  \
        }                                                                \
    } while (0)

/* The one live composition object that WM_SIZE needs to resize. */
static WUC_Sprite *g_sprite = NULL;

static void resize_sprite(HWND hwnd)
{
    RECT rc;
    WFN_Vector_2 size;
    if (!g_sprite || !GetClientRect(hwnd, &rc))
        return;
    size.X = (float)(rc.right - rc.left);
    size.Y = (float)(rc.bottom - rc.top);
    wuc_visual_put__size((WUC_Visual *)g_sprite, size);
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_SIZE:
        resize_sprite(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

int main(void)
{
    WUC_Comp                  *comp     = NULL;
    WUC_BackdropBrush         *backdrop = NULL;
    WUC_CompositionTarget     *target   = NULL;
    IUnknown                  *dq       = NULL;
    ICompositorDesktopInterop *interop  = NULL;
    IUnknown                  *targetUnk = NULL;
    HWND                       hwnd     = NULL;
    WNDCLASSEXW                wc;
    MSG                        msg;
    DWORD                      backdropType = DWMSBT_MAINWINDOW;
    HINSTANCE                  hinst = GetModuleHandleW(NULL);
    int                        rc = 1;
    HRESULT                    hr;

    /* Composition needs an STA thread with a DispatcherQueue. */
    hr = cwinrt_init(RO_INIT_SINGLETHREADED);
    if (FAILED(hr)) {
        fprintf(stderr, "FAIL cwinrt_init: 0x%08lX\n", (unsigned long)hr);
        return 1;
    }
    CHECK(cwinrt_bootstrap_dispatcher_queue(&dq), "DispatcherQueue bootstrap");

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = wnd_proc;
    wc.hInstance     = hinst;
    wc.hCursor       = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW); /* MAKEINTRESOURCE picks the ANSI variant under mingw; cast keeps the W call clean */
    wc.lpszClassName = L"CwinrtMicaSample";
    if (!RegisterClassExW(&wc)) {
        fprintf(stderr, "FAIL RegisterClassExW: %lu\n", (unsigned long)GetLastError());
        goto done;
    }

    hwnd = CreateWindowExW(0, wc.lpszClassName, L"cwinrt Mica",
                           WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                           900, 600, NULL, NULL, hinst, NULL);
    if (!hwnd) {
        fprintf(stderr, "FAIL CreateWindowExW: %lu\n", (unsigned long)GetLastError());
        goto done;
    }

    /* Mica system backdrop (Windows 11). */
    CHECK(DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE,
                                &backdropType, sizeof(backdropType)),
          "DwmSetWindowAttribute(Mica)");

    /* Composition backdrop tree. */
    CHECK(wuc_comp_new(&comp), "Compositor activate");
    CHECK(wuc_comp_create_backdrop_brush(comp, &backdrop), "CreateBackdropBrush");
    CHECK(wuc_comp_create_sprite_visual(comp, &g_sprite), "CreateSpriteVisual");

    /* Opaque forward-declared structs dispatch by IUnknown vtable; pointer-cast
       the concrete brush to the WUC_Brush* the slot expects (e2e idiom). */
    CHECK(wuc_sprite_put__brush(g_sprite, (WUC_Brush *)backdrop), "put_Brush");
    resize_sprite(hwnd);

    /* DesktopWindowTarget is interop-only: QI the Compositor for it. */
    CHECK(cwinrt_query(comp, &IID_ICompositorDesktopInterop, (void **)&interop),
          "QI ICompositorDesktopInterop");
    CHECK(interop->lpVtbl->CreateDesktopWindowTarget(interop, hwnd, FALSE,
                                                     (void **)&targetUnk),
          "CreateDesktopWindowTarget");
    CHECK(cwinrt_query(targetUnk, &CWINRT_IID_WUC_ICompositionTarget,
                       (void **)&target),
          "QI ICompositionTarget");
    CHECK(wuc_composition_target_put__root(target, (WUC_Visual *)g_sprite),
          "put_Root");

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    rc = (int)msg.wParam;

done:
    /* Sprite is rooted in the target; drop our window ref before releasing. */
    if (hwnd)
        DestroyWindow(hwnd);
    RELEASE(target);
    RELEASE(targetUnk);
    RELEASE(interop);
    RELEASE(g_sprite);
    RELEASE(backdrop);
    RELEASE(comp);
    RELEASE(dq);
    cwinrt_uninit();
    return rc;
}
