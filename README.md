# casc-daw-sdk

**Single-header DAW integration SDK for the [CASC](https://github.com/cogrow4/CASC) plugin format — plus a native REAPER extension.**

CASC ("Community Audio Source Container") is a `.casc` audio-plugin format: a
single file that bundles a WebAssembly DSP core, a JSON manifest, an optional
HTML/JS GUI, and presets. One `.casc` file runs unmodified on **Windows, macOS
and Linux**.

This repo gives **DAW authors** two things:

1. **`casc_daw_sdk.h`** — one header you drop into your DAW to add native CASC
   support: scan for plugins, read their metadata, instantiate them, process
   audio/MIDI, and host their sandboxed GUI. The same API and the same source
   behave identically on every OS.
2. **`reaper_casc`** — a ready-built REAPER extension that wires CASC into
   REAPER, so `.casc` files show up as native plugins in the FX browser.

---

## The "one file works anywhere" model

A CASC plugin is OS-agnostic because the *DSP* is WebAssembly and the *GUI* is
HTML/JS. The only OS-specific code is the runtime's sandboxed WebView host,
which lives **inside libcasc** and is chosen at libcasc build time:

| OS      | DSP runtime        | GUI host (sandboxed)        |
|---------|--------------------|-----------------------------|
| macOS   | Wasmtime (AOT)     | `WKWebView`                 |
| Windows | Wasmtime (AOT)     | `WebView2` (Edge Chromium)  |
| Linux   | Wasmtime (AOT)     | `WebKitGTK`                 |

From your DAW's point of view there is **one API** (`casc_daw_sdk.h`) and you
never touch platform code. If a platform's WebView is unavailable, libcasc
falls back to a null GUI backend that reports `CASC_ERR_IO`, and your DAW simply
draws its generic parameter panel from the metadata the SDK exposes.

```
            your DAW
               │  #include "casc_daw_sdk.h"   (single header, OS-agnostic)
               ▼
        ┌──────────────┐
        │ casc_daw_sdk │  session / plugin / voice + GUI helpers
        └──────┬───────┘
               │ links
               ▼
        ┌──────────────┐   Wasmtime  ── DSP (.wasm)
        │   libcasc    │   WKWebView / WebView2 / WebKitGTK ── GUI (ui.html)
        └──────────────┘
```

> **Why "single header" and not "single binary"?** The *integration code* you
> write is one header. The *runtime* (Wasm engine + per-OS WebView) is a real
> library you link — it cannot be a header because it embeds Wasmtime. This is
> the same model as STB/Dear ImGui: one header to include, backed by linked
> libraries. The header has no dependencies of its own beyond `libcasc.h`.

---

## Quick start (integrating into a DAW)

In **exactly one** `.c`/`.cpp` file:

```c
#define CASC_DAW_SDK_IMPLEMENTATION
#include "casc_daw_sdk.h"
```

Everywhere else, just `#include "casc_daw_sdk.h"`.

```c
/* 1. Discover installed plugins (NULL = standard per-OS folders). */
casc_sdk_session* s = casc_sdk_open(NULL);

for (int i = 0; i < casc_sdk_plugin_count(s); i++) {
    casc_sdk_plugin* p = casc_sdk_plugin_at(s, i);
    printf("%s by %s — %d params, gui=%d\n",
           casc_sdk_plugin_name(p), casc_sdk_plugin_vendor(p),
           casc_sdk_plugin_param_count(p), casc_sdk_plugin_has_gui(p));
}

/* 2. Instantiate one on a track. */
casc_sdk_voice* v = casc_sdk_voice_new(casc_sdk_plugin_at(s, 0), 48000.0, 512);

/* 3. Audio thread: process a block (inputs may be NULL for instruments). */
casc_sdk_voice_process(v, inputs, outputs, nframes);

/* 3b. Instruments: feed MIDI before processing. */
casc_sdk_voice_send_midi(v, events, n_events);

/* 4. Show the plugin's own GUI embedded in your track/editor view. */
casc_sdk_voice_open_gui(v, native_parent /* NSView*/HWND/X11 */);
/* …call ~60Hz on the UI thread so the WebView pumps: */
casc_sdk_voice_gui_tick(v);
/* record automation when the panel edits a param: */
casc_sdk_voice_on_param_edit(v, my_cb, my_userdata);

/* 5. Project save / load. */
size_t sz; void* blob = casc_sdk_voice_save_state(v, &sz);   /* free(blob) */
casc_sdk_voice_load_state(v, blob, sz);

/* 6. Teardown. */
casc_sdk_voice_free(v);
casc_sdk_close(s);
```

That's the whole integration surface. Full API is documented inline in
[`include/casc_daw_sdk.h`](include/casc_daw_sdk.h).

### Threading contract
- **Audio thread:** `process`, `send_midi`, `set_param`, `get_param`.
- **UI / main thread:** `open_gui`, `close_gui`, `gui_tick`, `set_gui_size`,
  `gui_notify_param`.
- **Main thread, before instantiating:** discovery (`open`, `add_path`,
  `rescan`).

---

## Building

You need a sibling checkout of the CASC runtime. Layout:

```
Projects/
├── CASC/            # the runtime (https://github.com/cogrow4/CASC)
└── casc-daw-sdk/    # this repo
```

```sh
cd casc-daw-sdk
cmake -S . -B build            # auto-finds ../CASC, or pass -DCASC_DIR=/path
cmake --build build
ctest --test-dir build         # runs the SDK self-test
```

Outputs:
- `build/sdk_selftest` — compiles the header in implementation mode and
  exercises discovery + a headless voice lifecycle.
- `build/reaper_casc.{dylib,so,dll}` — the REAPER extension (see below).

CMake options: `-DCASC_DIR=…`, `-DCASC_SDK_BUILD_TESTS=OFF`,
`-DCASC_SDK_BUILD_REAPER=OFF`.

---

## The REAPER extension (`reaper_casc`)

REAPER hosts **CLAP** plugins natively, and CASC ships a CLAP bridge
(`casc-clap-bridge.clap`) that exposes every installed `.casc` as a native CLAP
plugin — including its custom WebView GUI through `CLAP_EXT_GUI`. The
`reaper_casc` extension makes that pipeline first-class inside REAPER:

- Scans the standard CASC folders on load (via this SDK) and logs what's found.
- Adds bindable **actions** (Actions list / toolbar / keys):
  - `CASC: Rescan plugin folder`
  - `CASC: List installed CASC plugins (console)`
  - `CASC: Open CASC plugins folder`

One source file (`reaper/reaper_casc.c`) builds to
`reaper_casc.dll` / `.dylib` / `.so` with identical behaviour everywhere.

### Install
1. Build, then copy the module into REAPER's `UserPlugins` folder:
   - **macOS:** `~/Library/Application Support/REAPER/UserPlugins/`
   - **Windows:** `%APPDATA%\REAPER\UserPlugins\`
   - **Linux:** `~/.config/REAPER/UserPlugins/`
2. Put the CASC CLAP bridge where REAPER scans for CLAPs (or point REAPER's
   CLAP path at it), and drop `.casc` files into the CASC plugins folder
   (`CASC: Open CASC plugins folder` opens it for you).
3. Restart REAPER. `.casc` plugins appear in the FX browser under CLAP, with
   their native GUIs.

---

## License

MIT. See [`LICENSE`](LICENSE).
