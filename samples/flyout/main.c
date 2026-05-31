/*
 * cwinrt sample: pop a real WinRT flyout (Windows.UI.Popups.MessageDialog),
 * await the user's choice asynchronously, and print the chosen label.
 *
 * Exercises the async + HSTRING + window-association path of cwinrt:
 *   factory -> create -> IInitializeWithWindow association -> ShowAsync ->
 *   cwinrt_async_get (await + GetResults) -> read IUICommand.Label as UTF-8.
 *
 * RUNTIME requires an interactive Windows desktop session (a dialog is shown
 * and the call blocks on the user). CI only compile-verifies this file; it is
 * not run headless. Console app; build as a normal C17 executable.
 */
#include <cwinrt/init.h>
#include <cwinrt/cast.h>
#include <cwinrt/async.h>
#include <cwinrt/hstring.h>
#include <cwinrt/factory.h>
#include <cwinrt/Windows.UI.Popups.h>
#include <stdio.h>

/* Shell interop: associate a WinRT UI object with an owner HWND. Not part of
   the cwinrt projection (it's a classic COM interop interface), so declare it
   inline. The MessageDialog QIs to this in a desktop (non-UWP) process. */
#ifndef __IInitializeWithWindow_DEFINED
#define __IInitializeWithWindow_DEFINED
typedef struct IInitializeWithWindow IInitializeWithWindow;
typedef struct IInitializeWithWindowVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IInitializeWithWindow*, REFIID, void**);
    ULONG   (STDMETHODCALLTYPE *AddRef)(IInitializeWithWindow*);
    ULONG   (STDMETHODCALLTYPE *Release)(IInitializeWithWindow*);
    HRESULT (STDMETHODCALLTYPE *Initialize)(IInitializeWithWindow*, HWND);
} IInitializeWithWindowVtbl;
struct IInitializeWithWindow { const IInitializeWithWindowVtbl* lpVtbl; };
static const GUID IID_IInitializeWithWindow =
    { 0x3E68D4BD, 0x7135, 0x4D10, { 0x80, 0x18, 0x9F, 0xB6, 0xD9, 0xF3, 0x3F, 0xA1 } };
#endif

#define CHECK(expr, what)                                                \
    do {                                                                 \
        hr = (expr);                                                     \
        if (FAILED(hr)) {                                                \
            printf("FAIL %s: hr=0x%08lX\n", (what), (unsigned long)hr);  \
            goto cleanup;                                                \
        }                                                                \
    } while (0)

/* Pick an owner window for the dialog. Prefer the console window; if this
   process has none (e.g. launched detached), create a tiny hidden top-level
   window to own the popup. Returns NULL only if even that fails. */
static HWND owner_window(void)
{
    HWND hwnd = GetConsoleWindow();
    if (hwnd)
        return hwnd;
    /* HWND_MESSAGE windows cannot own a visible dialog, so make a real (but
       never-shown) overlapped window. The default WndProc is fine here. */
    return CreateWindowExW(0, L"STATIC", L"cwinrt-flyout-owner",
                           WS_OVERLAPPED, 0, 0, 0, 0,
                           NULL, NULL, GetModuleHandleW(NULL), NULL);
}

int main(void)
{
    WUIPO_IMessageDialogFactory     *fac    = NULL;
    WUIPO_MessageDialog             *dlg    = NULL;
    IInitializeWithWindow           *iiww   = NULL;
    WF_IAsyncOperation_WUIPO_IUICommand *op  = NULL;
    WUIPO_IUICommand                *chosen = NULL;
    cwinrt_hstring                   content = NULL;
    cwinrt_hstring                   title   = NULL;
    cwinrt_hstring                   label   = NULL;
    HWND                             hwnd    = NULL;
    char                             utf8[512];
    int                              rc = 1;
    HRESULT                          hr;

    hr = cwinrt_init(RO_INIT_SINGLETHREADED);
    if (FAILED(hr)) {
        printf("FAIL cwinrt_init: 0x%08lX\n", (unsigned long)hr);
        return 1;
    }

    CHECK(cwinrt_hstring_from(L"Choose an option", &content), "hstring content");
    CHECK(cwinrt_hstring_from(L"cwinrt flyout", &title), "hstring title");

    /* MessageDialog has no default activation ctor; build via its factory. */
    CHECK(cwinrt_factory_get_statics(L"Windows.UI.Popups.MessageDialog",
                                     &CWINRT_IID_WUIPO_IMessageDialogFactory,
                                     (void **)&fac), "get MessageDialogFactory");
    CHECK(wuipo_message_dialog_factory_create(fac, content, &dlg), "MessageDialog.Create");
    CHECK(wuipo_message_dialog_put__title(dlg, title), "MessageDialog.put_Title");

    /* Desktop process: the dialog must be associated with an owner HWND or
       ShowAsync fails. QI the dialog for IInitializeWithWindow and bind it. */
    hwnd = owner_window();
    if (!hwnd) {
        printf("FAIL owner_window: no HWND available\n");
        goto cleanup;
    }
    CHECK(cwinrt_query(dlg, &IID_IInitializeWithWindow, (void **)&iiww),
          "QI IInitializeWithWindow");
    CHECK(iiww->lpVtbl->Initialize(iiww, hwnd), "IInitializeWithWindow.Initialize");

    /* Show, then block until the user clicks. cwinrt_async_get waits and casts
       the result to IUICommand (releasing the intermediate reference). */
    CHECK(wuipo_message_dialog_show_async(dlg, &op), "MessageDialog.ShowAsync");
    CHECK(cwinrt_async_get((void *)op, &CWINRT_IID_WUIPO_IUICommand, (void **)&chosen),
          "await ShowAsync result");

    /* IUICommand and the UICommand class share the get_Label vtable slot, so the
       generated getter accepts the awaited IUICommand* via a plain cast. */
    CHECK(wuipo_u_i_command_get__label((WUIPO_UICommand *)chosen, &label),
          "UICommand.get_Label");
    if (cwinrt_hstring_to_utf8(label, utf8, (int)sizeof(utf8)) < 0) {
        printf("FAIL hstring_to_utf8\n");
        goto cleanup;
    }
    printf("Chosen command: %s\n", utf8);
    rc = 0;

cleanup:
    cwinrt_hstring_free(label);
    if (chosen)
        ((IUnknown *)chosen)->lpVtbl->Release((IUnknown *)chosen);
    if (op)
        ((IUnknown *)op)->lpVtbl->Release((IUnknown *)op);
    if (iiww)
        iiww->lpVtbl->Release(iiww);
    if (dlg)
        ((IUnknown *)dlg)->lpVtbl->Release((IUnknown *)dlg);
    if (fac)
        ((IUnknown *)fac)->lpVtbl->Release((IUnknown *)fac);
    cwinrt_hstring_free(title);
    cwinrt_hstring_free(content);
    cwinrt_uninit();
    return rc;
}
