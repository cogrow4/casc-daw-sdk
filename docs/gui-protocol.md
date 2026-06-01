# CASC GUI Protocol (`window.casc`)

A CASC plugin may ship a `ui.html` inside its `.casc` archive. The host runtime
(libcasc) loads it into a **platform-native, sandboxed WebView**:

| OS      | WebView      |
|---------|--------------|
| macOS   | `WKWebView`  |
| Windows | `WebView2`   |
| Linux   | `WebKitGTK`  |

The page never talks to the OS or the DSP directly. Instead, libcasc injects a
single global object, **`window.casc`**, before the page's own scripts run. All
parameter traffic flows through it, so the *same* `ui.html` works on every OS
and in every host (standalone, the CLAP bridge, or a DAW using `casc_daw_sdk`).

## The injected API

```js
// Set a parameter (UI → DSP). value is normalised 0.0–1.0.
window.casc.setParam(id, value);

// Read the last known value of a parameter (cached; normalised 0.0–1.0).
const v = window.casc.getParam(id);

// Subscribe to parameter changes (DSP/automation → UI).
// cb is called as cb(id, value) whenever a value changes outside the UI.
window.casc.subscribe((id, value) => { /* update your knob */ });
```

- `id` is the integer parameter id from the manifest (`params[].id`).
- Values are always **normalised 0..1** across the bridge; map to engineering
  units (dB, Hz, …) in your page using the manifest's `min`/`max`/`unit`.
- `subscribe` lets the panel stay in sync when the user automates a parameter
  in the DAW or loads a preset.

## Lifecycle (host side)

The host drives the GUI through libcasc:

```
casc_open_ui(inst, parent)   // attach WebView to a native parent (or NULL = floating)
casc_set_ui_size(inst, w, h) // logical pixels
casc_ui_tick(inst)           // pump the WebView event loop, ~30–60 Hz, UI thread
casc_ui_notify_param(...)    // push automation changes into the page (→ subscribe)
casc_close_ui(inst)          // tear down
```

Through the CLAP bridge this is mapped onto `CLAP_EXT_GUI`
(`create`/`set_parent`/`set_size`/`show`/`hide`/`destroy`), and `casc_ui_tick`
is pumped from the host's `on_main_thread` callback. Through `casc_daw_sdk` the
same steps are `casc_sdk_voice_open_gui` / `_gui_tick` / `_set_gui_size` / etc.

## Manifest declaration

```json
{
  "gui": {
    "type": "html",
    "entry": "ui.html",
    "width": 460,
    "height": 320,
    "resizable": false
  }
}
```

If `gui` is absent (or its WebView backend is unavailable on the target OS), the
host falls back to a generic parameter panel built from the manifest — the
plugin still works, it just uses default sliders.

## Sandboxing

The page runs inside the OS WebView with no Node, no filesystem, and no network
beyond what the WebView allows. The **only** bridge to the outside world is the
`window.casc` message channel, which is restricted to parameter get/set/subscribe.
This keeps third-party plugin UIs contained.
