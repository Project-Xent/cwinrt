/*
 * WinRT base interface GUIDs.
 *
 * mingw-w64 ships IID_IInspectable and IID_IActivationFactory *only* inside
 * libwinstorecompat.a. That library is meant for Windows Store / UWP app
 * containers: alongside the GUIDs it also REPLACES Win32 desktop APIs
 * (GetStartupInfo, CreateFileW, getenv, CryptGenRandom, ...) with stubs that
 * call abort(). Linking it into a normal desktop app makes the C runtime's
 * GUI startup (crtexewin.c -> GetStartupInfo) abort before WinMain ever runs.
 *
 * Defining the two GUIDs here lets cwinrt provide them to desktop consumers
 * without dragging in winstorecompat's poisonous API overrides. MSVC supplies
 * these from its SDK, so this TU is only needed on mingw, but defining them
 * unconditionally is harmless (one weak-less definition, included nowhere else
 * with INITGUID).
 */
#define INITGUID
#include <guiddef.h>

/* IInspectable: AF86E2E0-B12D-4C6A-9C5A-D7AA65101E90 */
DEFINE_GUID(IID_IInspectable, 0xAF86E2E0, 0xB12D, 0x4C6A, 0x9C, 0x5A, 0xD7, 0xAA, 0x65, 0x10, 0x1E, 0x90);

/* IActivationFactory: 00000035-0000-0000-C000-000000000046 */
DEFINE_GUID(IID_IActivationFactory, 0x00000035, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
