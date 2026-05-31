# Conform tests

| Target | What it verifies |
|--------|------------------|
| `test_conform_golden` | Static class dispatch (`GuidHelper`) via runtime IID resolution |
| `test_conform_dispatch` | Foundation.Uri factory + instance vtable |
| `test_conform_dispatch_composition_abi` | Compositor `CreateSpriteVisual` slot 22 (`wuc_comp_create_sprite_visual`) |
| `test_conform_dispatch_composition_slots` | Slots 22 + 9 (`CreateSpriteVisual`, `CreateContainerVisual`) |
| `test_conform_dispatch_compositor` | Real `Compositor` after `DispatcherQueue` bootstrap |
| `test_conform_factory` | `RoGetActivationFactory` |
| `test_conform_async` | `cwinrt_async_wait` on completed mock IAsyncInfo |
| `test_header_compile` | Generated headers compile |
| `header-compile-all` | All 342 `compile_Windows_*.c` shards (declarations only) |
| `impl-compile-all` | All 342 `compile_impl_*.c` shards (header + impl in one TU) |
| `impl-link-all` | Link all impl units via `cwinrt-bindings-all` |

## Compositor runtime (`test_conform_dispatch_compositor`)

Unpackaged Win32 needs **sparse package identity** (see Microsoft “Grant package identity to non-packaged apps”).

1. Build: `xmake build test_conform_dispatch_compositor`
2. Register (once; may require **Developer Mode** if self-signed cert is rejected):
   `powershell -ExecutionPolicy Bypass -File scripts/register_conform_sparse.ps1`
3. Run: `build\windows\x64\release\test_conform_dispatch_compositor.exe`

If activation returns `0x80070005`, the ABI test still proves Composition dispatch codegen.
