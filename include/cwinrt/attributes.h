#pragma once

/*
 * Portable [[nodiscard]] for the hand-written cwinrt runtime headers. Generated
 * namespace headers carry their own CWINRT_MAYBE_UNUSED; this covers the runtime
 * API. Emit the attribute only where the active standard accepts the [[...]]
 * syntax (C23, or C++17); in a C17/C11 toolchain probe it degrades to nothing so
 * including the header never fails.
 */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
#  ifdef __has_c_attribute
#    if __has_c_attribute(nodiscard)
#      define CWINRT_NODISCARD [[nodiscard]]
#    endif
#  endif
#elif defined(__cplusplus) && __cplusplus >= 201703L
#  define CWINRT_NODISCARD [[nodiscard]]
#endif
#ifndef CWINRT_NODISCARD
#  define CWINRT_NODISCARD
#endif
