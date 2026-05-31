/*
 * cwinrt sample: ACRYLIC backdrop (sibling of samples/mica).
 *
 * A real Win32 desktop window whose non-client area uses the DWM acrylic
 * (transient) system backdrop, with a WinRT Composition tree wired to the
 * window via the DesktopWindowTarget interop. The sprite visual is filled
 * with a HOST backdrop brush (the acrylic-style brush), as opposed to the
 * plain backdrop brush the mica sibling uses.
 *
 * Build: compiles clean under MSVC /W4 and clang -Wall -Wextra (C17).
 * Runtime: requires a Windows 11 desktop session with a GPU; in CI this is
 * compile-only (no display / GPU), so it is never executed there.
 */
#include <cwinrt/init.h>
#include <cwinrt/cast.h>
#include <cwinrt/bootstrap.h>
#include <cwinrt/Windows.UI.Composition.h>

#include <windows.h>
#include <dwmapi.h>
#include <stdio.h>

/* DesktopWindowTarget is not projected: declare the interop COM interface. */
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

#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
/* DWMSBT_TRANSIENTWINDOW = 3 (Acrylic) */
#define DWMSBT_TRANSIENTWINDOW_ACRYLIC 3

#define CHECK(hr, what)                                                  \
    do {                                                                 \
        HRESULT _hr = (hr);                                              \
        if (FAILED(_hr)) {                                               \
            printf("FAIL %s: hr=0x%08lX\n", (what), (unsigned long)_hr); \
            goto done;                                                   \
        }                                                                \
    } while (0)

/* Global so WM_SIZE can resize the sprite as the window changes. */
static WUC_Sprite *g_sprite = NULL;

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_SIZE:
        if (g_sprite) {
            WFN_Vector_2 size = { (float)LOWORD(lp), (float)HIWORD(lp) };
            (void)wuc_visual_put__size((WUC_Visual *)g_sprite, size);
        }
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
    WUC_Comp                     *comp = NULL;
    WUC_BackdropBrush            *brush = NULL;
    WUC_Sprite                   *sprite = NULL;
    IUnknown                     *dq = NULL;
    ICompositorDesktopInterop    *interop = NULL;
    IUnknown                     *target_unk = NULL;
    WUC_CompositionTarget        *target = NULL;
    HWND                          hwnd = NULL;
    WNDCLASSEXW                   wc;
    MSG                           msg;
    int                           backdrop = DWMSBT_TRANSIENTWINDOW_ACRYLIC;
    int                           rc = 1;

    /* Composition needs an STA thread with a DispatcherQueue. */
    HRESULT hr = cwinrt_init(RO_INIT_SINGLETHREADED);
    if (FAILED(hr)) {
        printf("FAIL cwinrt_init: 0x%08lX\n", (unsigned long)hr);
        return 1;
    }
    CHECK(cwinrt_bootstrap_dispatcher_queue(&dq), "DispatcherQueue bootstrap");

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = wnd_proc;
    wc.hInstance     = GetModuleHandleW(NULL);
    wc.hCursor       = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW); /* MAKEINTRESOURCE picks the ANSI variant under mingw; cast keeps the W call clean */
    wc.lpszClassName = L"cwinrtAcrylicSample";
    if (!RegisterClassExW(&wc)) {
        printf("FAIL RegisterClassExW: 0x%08lX\n", (unsigned long)GetLastError());
        goto done;
    }

    hwnd = CreateWindowExW(0, wc.lpszClassName, L"cwinrt - Acrylic",
                           WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                           800, 600, NULL, NULL, wc.hInstance, NULL);
    if (!hwnd) {
        printf("FAIL CreateWindowExW: 0x%08lX\n", (unsigned long)GetLastError());
        goto done;
    }

    /* DWM acrylic (transient) system backdrop on the window itself. */
    CHECK(DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop)),
          "DwmSetWindowAttribute(acrylic)");

    /* Compositor + acrylic-style HOST backdrop brush on a sprite visual. */
    CHECK(wuc_comp_new(&comp), "Compositor activate");
    CHECK(wuc_comp_create_host_backdrop_brush(comp, &brush), "CreateHostBackdropBrush");
    CHECK(wuc_comp_create_sprite_visual(comp, &sprite), "CreateSpriteVisual");
    /* Opaque fwd-declared structs dispatch via IUnknown vtable; cast is how e2e does it. */
    CHECK(wuc_sprite_put__brush(sprite, (WUC_Brush *)brush), "put_Brush");

    {
        RECT rcClient;
        WFN_Vector_2 size;
        GetClientRect(hwnd, &rcClient);
        size.X = (float)(rcClient.right - rcClient.left);
        size.Y = (float)(rcClient.bottom - rcClient.top);
        CHECK(wuc_visual_put__size((WUC_Visual *)sprite, size), "put_Size");
    }

    /* Bind the visual tree to the HWND through the desktop interop. */
    CHECK(cwinrt_query(comp, &IID_ICompositorDesktopInterop, (void **)&interop),
          "QI ICompositorDesktopInterop");
    CHECK(interop->lpVtbl->CreateDesktopWindowTarget(interop, hwnd, FALSE, (void **)&target_unk),
          "CreateDesktopWindowTarget");
    CHECK(cwinrt_query(target_unk, &CWINRT_IID_WUC_ICompositionTarget, (void **)&target),
          "QI ICompositionTarget");
    CHECK(wuc_composition_target_put__root(target, (WUC_Visual *)sprite), "put_Root");

    g_sprite = sprite;
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    g_sprite = NULL;
    rc = (int)msg.wParam;

done:
    if (target)
        ((IUnknown *)target)->lpVtbl->Release((IUnknown *)target);
    if (target_unk)
        target_unk->lpVtbl->Release(target_unk);
    if (interop)
        interop->lpVtbl->Release(interop);
    if (sprite)
        ((IUnknown *)sprite)->lpVtbl->Release((IUnknown *)sprite);
    if (brush)
        ((IUnknown *)brush)->lpVtbl->Release((IUnknown *)brush);
    if (comp)
        ((IUnknown *)comp)->lpVtbl->Release((IUnknown *)comp);
    if (dq)
        dq->lpVtbl->Release(dq);
    cwinrt_uninit();
    return rc;
}
