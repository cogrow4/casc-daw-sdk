/*
 * casc_daw_sdk.h — Single-header DAW integration SDK for the CASC format
 * ============================================================================
 *
 *   Community Audio Source Container (CASC) — host integration helpers
 *   SPDX-License-Identifier: MIT
 *   Version 0.1.0
 *
 * WHAT THIS IS
 * ------------
 * A single-header, dependency-free (other than libcasc) convenience layer that
 * lets a DAW add *native* CASC plugin support in a few lines of code. It wraps
 * the lower-level libcasc.h C API with a higher-level "host session" model:
 *
 *     casc_sdk_session   — scans directories, enumerates available plugins
 *     casc_sdk_plugin    — a discovered .casc (metadata, ports, params, GUI)
 *     casc_sdk_voice     — a live instance bound to a track/slot (audio + GUI)
 *
 * The same source compiles and behaves identically on macOS, Windows and Linux
 * — there is no platform-specific code in this header. All OS-specific work
 * (the sandboxed WebView GUI: WKWebView / WebView2 / WebKitGTK) lives inside
 * libcasc and is selected at libcasc build time. From the DAW's point of view,
 * one API works everywhere.
 *
 * HOW TO USE
 * ----------
 * In exactly ONE translation unit:
 *
 *     #define CASC_DAW_SDK_IMPLEMENTATION
 *     #include "casc_daw_sdk.h"
 *
 * Everywhere else just #include "casc_daw_sdk.h".
 *
 * You must link against libcasc (and its deps: wasmtime, miniz, cjson). The
 * SDK does not re-implement the runtime; it orchestrates it. See README.md.
 *
 * MINIMAL EXAMPLE
 * ---------------
 *     casc_sdk_session* s = casc_sdk_open(NULL);          // default dirs
 *     for (int i = 0; i < casc_sdk_plugin_count(s); i++) {
 *         casc_sdk_plugin* p = casc_sdk_plugin_at(s, i);
 *         printf("%s by %s\n", casc_sdk_plugin_name(p),
 *                              casc_sdk_plugin_vendor(p));
 *     }
 *     casc_sdk_voice* v = casc_sdk_voice_new(casc_sdk_plugin_at(s, 0),
 *                                            48000.0, 512);
 *     // per audio block:
 *     casc_sdk_voice_process(v, in, out, nframes);
 *     // to show its GUI embedded in your track view:
 *     casc_sdk_voice_open_gui(v, native_parent_view_handle);
 *     // call ~60Hz on the UI thread:
 *     casc_sdk_voice_gui_tick(v);
 *     ...
 *     casc_sdk_voice_free(v);
 *     casc_sdk_close(s);
 *
 * THREADING
 * ---------
 *  - *_process / *_send_midi / *_set_param / *_get_param : audio thread safe.
 *  - *_open_gui / *_close_gui / *_gui_tick / *_set_gui_size : UI/main thread.
 *  - Discovery (*_open, *_rescan) : main thread, before instantiating voices.
 */

#ifndef CASC_DAW_SDK_H
#define CASC_DAW_SDK_H

#include "libcasc.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CASC_SDK_API
#  define CASC_SDK_API extern
#endif

#define CASC_SDK_VERSION_STRING "0.1.0"

/* Opaque handles. */
typedef struct casc_sdk_session casc_sdk_session;
typedef struct casc_sdk_plugin  casc_sdk_plugin;
typedef struct casc_sdk_voice   casc_sdk_voice;

/* --------------------------------------------------------------------------
 * Session / discovery
 * -------------------------------------------------------------------------- */

/*
 * Open a host session and scan for installed .casc plugins.
 *
 * search_paths : NULL-terminated array of directories to scan. Pass NULL to
 *                use the conventional per-OS locations:
 *                  macOS   : /Library/Audio/Plug-Ins/CASC,
 *                            ~/Library/Audio/Plug-Ins/CASC
 *                  Windows : %COMMONPROGRAMFILES%\CASC, %APPDATA%\CASC
 *                  Linux   : /usr/lib/casc, ~/.local/lib/casc, $CASC_PATH
 * Returns NULL only on allocation failure (an empty session is still valid).
 */
CASC_SDK_API casc_sdk_session* casc_sdk_open(const char* const* search_paths);

/* Add one directory and scan it (deduplicated against already-known plugins). */
CASC_SDK_API int  casc_sdk_add_path(casc_sdk_session* s, const char* dir);

/* Re-scan all known paths (e.g. after the user installs a plugin). */
CASC_SDK_API void casc_sdk_rescan(casc_sdk_session* s);

/* Destroy the session, unload all plugins. All voices must be freed first. */
CASC_SDK_API void casc_sdk_close(casc_sdk_session* s);

/* Number of discovered plugins. */
CASC_SDK_API int  casc_sdk_plugin_count(casc_sdk_session* s);

/* Discovered plugin by index [0, count). NULL if out of range. */
CASC_SDK_API casc_sdk_plugin* casc_sdk_plugin_at(casc_sdk_session* s, int index);

/* Discovered plugin by unique id (e.g. "com.acme.reverb"). NULL if not found. */
CASC_SDK_API casc_sdk_plugin* casc_sdk_plugin_by_id(casc_sdk_session* s,
                                                    const char* id);

/* The CASC runtime version string. */
CASC_SDK_API const char* casc_sdk_runtime_version(void);

/* --------------------------------------------------------------------------
 * Plugin metadata (stable for the session lifetime)
 * -------------------------------------------------------------------------- */

CASC_SDK_API const char* casc_sdk_plugin_id(casc_sdk_plugin* p);
CASC_SDK_API const char* casc_sdk_plugin_name(casc_sdk_plugin* p);
CASC_SDK_API const char* casc_sdk_plugin_vendor(casc_sdk_plugin* p);
CASC_SDK_API const char* casc_sdk_plugin_version(casc_sdk_plugin* p);
CASC_SDK_API const char* casc_sdk_plugin_description(casc_sdk_plugin* p);
CASC_SDK_API const char* casc_sdk_plugin_path(casc_sdk_plugin* p);

CASC_SDK_API int  casc_sdk_plugin_is_instrument(casc_sdk_plugin* p);
CASC_SDK_API int  casc_sdk_plugin_audio_inputs(casc_sdk_plugin* p);
CASC_SDK_API int  casc_sdk_plugin_audio_outputs(casc_sdk_plugin* p);
CASC_SDK_API int  casc_sdk_plugin_accepts_midi(casc_sdk_plugin* p);

CASC_SDK_API int  casc_sdk_plugin_param_count(casc_sdk_plugin* p);
CASC_SDK_API int  casc_sdk_plugin_param_info(casc_sdk_plugin* p, int index,
                                             casc_param_info_t* out);

/* GUI metadata. has_gui==0 means the DAW should draw a generic param panel. */
CASC_SDK_API int  casc_sdk_plugin_has_gui(casc_sdk_plugin* p);
CASC_SDK_API int  casc_sdk_plugin_gui_width(casc_sdk_plugin* p);
CASC_SDK_API int  casc_sdk_plugin_gui_height(casc_sdk_plugin* p);
CASC_SDK_API int  casc_sdk_plugin_gui_resizable(casc_sdk_plugin* p);

/* Direct access to the underlying libcasc plugin, for advanced callers. */
CASC_SDK_API casc_plugin_t* casc_sdk_plugin_raw(casc_sdk_plugin* p);

/* --------------------------------------------------------------------------
 * Voice = a live instance bound to a track/slot
 * -------------------------------------------------------------------------- */

/* Instantiate a plugin. Returns NULL on error. */
CASC_SDK_API casc_sdk_voice* casc_sdk_voice_new(casc_sdk_plugin* p,
                                                double sample_rate,
                                                int max_block_size);

/* Destroy a voice (closes its GUI first if open). */
CASC_SDK_API void casc_sdk_voice_free(casc_sdk_voice* v);

/* Re-prepare for a new sample rate / block size; preserves parameter values. */
CASC_SDK_API void casc_sdk_voice_reset(casc_sdk_voice* v,
                                       double sample_rate, int max_block_size);

/* Audio-thread: process one block. inputs may be NULL for instruments. */
CASC_SDK_API void casc_sdk_voice_process(casc_sdk_voice* v,
                                         const float** inputs, float** outputs,
                                         int frames);

/* Audio-thread: queue MIDI for the next process() call. */
CASC_SDK_API void casc_sdk_voice_send_midi(casc_sdk_voice* v,
                                           const casc_midi_event_t* ev, int n);

/* Parameters (normalised 0..1), thread-safe. */
CASC_SDK_API void   casc_sdk_voice_set_param(casc_sdk_voice* v, int id, double val);
CASC_SDK_API double casc_sdk_voice_get_param(casc_sdk_voice* v, int id);

/* State (DAW project save/load). Blob is heap-allocated; free with free(). */
CASC_SDK_API void* casc_sdk_voice_save_state(casc_sdk_voice* v, size_t* out_size);
CASC_SDK_API int   casc_sdk_voice_load_state(casc_sdk_voice* v,
                                             const void* data, size_t size);

CASC_SDK_API int casc_sdk_voice_latency(casc_sdk_voice* v);
CASC_SDK_API int casc_sdk_voice_tail(casc_sdk_voice* v);

/* Access the raw libcasc instance for advanced callers. */
CASC_SDK_API casc_instance_t* casc_sdk_voice_raw(casc_sdk_voice* v);

/* --------------------------------------------------------------------------
 * GUI hosting (UI/main thread)
 * -------------------------------------------------------------------------- */

/*
 * Callback invoked when the plugin's own GUI edits a parameter. The DAW should
 * record automation / refresh its parameter view. Optional.
 */
typedef void (*casc_sdk_param_edit_cb)(void* user, int param_id, double value);

CASC_SDK_API void casc_sdk_voice_on_param_edit(casc_sdk_voice* v,
                                               casc_sdk_param_edit_cb cb,
                                               void* user);

/*
 * Open the plugin's GUI embedded in a native parent:
 *   parent = NSView* (macOS) / HWND (Windows) / X11 Window or GtkWidget* (Linux)
 * Pass NULL to create a standalone top-level window (floating editor).
 * Returns CASC_OK, or CASC_ERR_IO if the plugin has no hostable GUI (the DAW
 * should then fall back to its generic parameter panel).
 */
CASC_SDK_API int  casc_sdk_voice_open_gui(casc_sdk_voice* v, void* parent);
CASC_SDK_API void casc_sdk_voice_close_gui(casc_sdk_voice* v);
CASC_SDK_API int  casc_sdk_voice_gui_is_open(casc_sdk_voice* v);
CASC_SDK_API int  casc_sdk_voice_set_gui_size(casc_sdk_voice* v, int w, int h);

/* Pump the WebView event loop; call ~30-60Hz on the UI thread. */
CASC_SDK_API void casc_sdk_voice_gui_tick(casc_sdk_voice* v);

/* Tell the GUI a parameter changed elsewhere (automation/preset) so it redraws. */
CASC_SDK_API void casc_sdk_voice_gui_notify_param(casc_sdk_voice* v,
                                                  int param_id, double value);

#ifdef __cplusplus
} /* extern "C" */
#endif

/* ==========================================================================
 *  IMPLEMENTATION
 * ========================================================================== */
#ifdef CASC_DAW_SDK_IMPLEMENTATION

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#if defined(_WIN32)
#  include <windows.h>
#  define CASC_SDK_PATHSEP '\\'
#else
#  include <dirent.h>
#  include <sys/stat.h>
#  define CASC_SDK_PATHSEP '/'
#endif

#ifndef CASC_SDK_MAX_PLUGINS
#  define CASC_SDK_MAX_PLUGINS 512
#endif
#ifndef CASC_SDK_MAX_PATHS
#  define CASC_SDK_MAX_PATHS 32
#endif

struct casc_sdk_plugin {
    char           path[1024];
    char           id[256];
    casc_plugin_t* loaded;     /* owned by the session */
};

struct casc_sdk_session {
    casc_sdk_plugin plugins[CASC_SDK_MAX_PLUGINS];
    int             count;
    char            paths[CASC_SDK_MAX_PATHS][1024];
    int             path_count;
};

struct casc_sdk_voice {
    casc_sdk_plugin* owner;
    casc_instance_t* inst;
    double           sample_rate;
    int              max_block;
};

/* ---- discovery helpers ---- */

static int casc_sdk__already_known(casc_sdk_session* s, const char* path) {
    for (int i = 0; i < s->count; i++)
        if (strcmp(s->plugins[i].path, path) == 0) return 1;
    return 0;
}

static void casc_sdk__try_add(casc_sdk_session* s, const char* full_path) {
    if (s->count >= CASC_SDK_MAX_PLUGINS) return;
    if (casc_sdk__already_known(s, full_path)) return;

    size_t len = strlen(full_path);
    if (len < 6 || strcmp(full_path + len - 5, ".casc") != 0) return;

    char err[256] = {0};
    casc_plugin_t* pl = casc_load(full_path, err, sizeof(err));
    if (!pl) return;

    casc_sdk_plugin* p = &s->plugins[s->count];
    memset(p, 0, sizeof(*p));
    snprintf(p->path, sizeof(p->path), "%s", full_path);
    snprintf(p->id, sizeof(p->id), "%s", casc_plugin_get_id(pl));
    p->loaded = pl;
    s->count++;
}

static void casc_sdk__scan_dir(casc_sdk_session* s, const char* dir) {
    if (!dir || !dir[0]) return;
#if defined(_WIN32)
    char pattern[1024];
    snprintf(pattern, sizeof(pattern), "%s\\*.casc", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        char full[1024];
        snprintf(full, sizeof(full), "%s\\%s", dir, fd.cFileName);
        casc_sdk__try_add(s, full);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR* d = opendir(dir);
    if (!d) return;
    struct dirent* e;
    while ((e = readdir(d)) != NULL) {
        char full[1024];
        snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);
        casc_sdk__try_add(s, full);
    }
    closedir(d);
#endif
}

static void casc_sdk__remember_path(casc_sdk_session* s, const char* dir) {
    if (!dir || !dir[0] || s->path_count >= CASC_SDK_MAX_PATHS) return;
    for (int i = 0; i < s->path_count; i++)
        if (strcmp(s->paths[i], dir) == 0) return;
    snprintf(s->paths[s->path_count++], 1024, "%s", dir);
}

static void casc_sdk__add_default_paths(casc_sdk_session* s) {
#if defined(__APPLE__)
    casc_sdk__remember_path(s, "/Library/Audio/Plug-Ins/CASC");
    const char* home = getenv("HOME");
    if (home) { char b[1024]; snprintf(b, sizeof(b), "%s/Library/Audio/Plug-Ins/CASC", home); casc_sdk__remember_path(s, b); }
#elif defined(_WIN32)
    const char* cpf = getenv("COMMONPROGRAMFILES");
    if (cpf) { char b[1024]; snprintf(b, sizeof(b), "%s\\CASC", cpf); casc_sdk__remember_path(s, b); }
    const char* ad = getenv("APPDATA");
    if (ad)  { char b[1024]; snprintf(b, sizeof(b), "%s\\CASC", ad); casc_sdk__remember_path(s, b); }
#else
    casc_sdk__remember_path(s, "/usr/lib/casc");
    const char* home = getenv("HOME");
    if (home) { char b[1024]; snprintf(b, sizeof(b), "%s/.local/lib/casc", home); casc_sdk__remember_path(s, b); }
    const char* cp = getenv("CASC_PATH");
    if (cp) {
        char copy[4096]; snprintf(copy, sizeof(copy), "%s", cp);
        char* tok = strtok(copy, ":");
        while (tok) { casc_sdk__remember_path(s, tok); tok = strtok(NULL, ":"); }
    }
#endif
}

/* ---- session ---- */

casc_sdk_session* casc_sdk_open(const char* const* search_paths) {
    casc_sdk_session* s = (casc_sdk_session*)calloc(1, sizeof(*s));
    if (!s) return NULL;

    if (search_paths) {
        for (int i = 0; search_paths[i]; i++)
            casc_sdk__remember_path(s, search_paths[i]);
    } else {
        casc_sdk__add_default_paths(s);
    }
    for (int i = 0; i < s->path_count; i++)
        casc_sdk__scan_dir(s, s->paths[i]);
    return s;
}

int casc_sdk_add_path(casc_sdk_session* s, const char* dir) {
    if (!s || !dir) return CASC_ERR_INVALID_ARG;
    casc_sdk__remember_path(s, dir);
    casc_sdk__scan_dir(s, dir);
    return CASC_OK;
}

void casc_sdk_rescan(casc_sdk_session* s) {
    if (!s) return;
    for (int i = 0; i < s->path_count; i++)
        casc_sdk__scan_dir(s, s->paths[i]);
}

void casc_sdk_close(casc_sdk_session* s) {
    if (!s) return;
    for (int i = 0; i < s->count; i++)
        if (s->plugins[i].loaded) casc_unload(s->plugins[i].loaded);
    free(s);
}

int casc_sdk_plugin_count(casc_sdk_session* s) { return s ? s->count : 0; }

casc_sdk_plugin* casc_sdk_plugin_at(casc_sdk_session* s, int index) {
    if (!s || index < 0 || index >= s->count) return NULL;
    return &s->plugins[index];
}

casc_sdk_plugin* casc_sdk_plugin_by_id(casc_sdk_session* s, const char* id) {
    if (!s || !id) return NULL;
    for (int i = 0; i < s->count; i++)
        if (strcmp(s->plugins[i].id, id) == 0) return &s->plugins[i];
    return NULL;
}

const char* casc_sdk_runtime_version(void) { return casc_version(); }

/* ---- plugin metadata ---- */

const char* casc_sdk_plugin_id(casc_sdk_plugin* p)          { return p ? casc_plugin_get_id(p->loaded) : ""; }
const char* casc_sdk_plugin_name(casc_sdk_plugin* p)        { return p ? casc_plugin_get_name(p->loaded) : ""; }
const char* casc_sdk_plugin_vendor(casc_sdk_plugin* p)      { return p ? casc_plugin_get_vendor(p->loaded) : ""; }
const char* casc_sdk_plugin_version(casc_sdk_plugin* p)     { return p ? casc_plugin_get_version(p->loaded) : ""; }
const char* casc_sdk_plugin_description(casc_sdk_plugin* p) { return p ? casc_plugin_get_description(p->loaded) : ""; }
const char* casc_sdk_plugin_path(casc_sdk_plugin* p)        { return p ? p->path : ""; }

int casc_sdk_plugin_is_instrument(casc_sdk_plugin* p) {
    if (!p) return 0;
    int fc = casc_plugin_get_feature_count(p->loaded);
    for (int i = 0; i < fc; i++)
        if (strcmp(casc_plugin_get_feature(p->loaded, i), "instrument") == 0) return 1;
    return casc_plugin_has_midi_input(p->loaded) ? 1 : 0;
}
int casc_sdk_plugin_audio_inputs(casc_sdk_plugin* p)  { return p ? casc_plugin_get_audio_input_count(p->loaded) : 0; }
int casc_sdk_plugin_audio_outputs(casc_sdk_plugin* p) { return p ? casc_plugin_get_audio_output_count(p->loaded) : 0; }
int casc_sdk_plugin_accepts_midi(casc_sdk_plugin* p)  { return p ? casc_plugin_has_midi_input(p->loaded) : 0; }

int casc_sdk_plugin_param_count(casc_sdk_plugin* p) { return p ? casc_plugin_get_param_count(p->loaded) : 0; }
int casc_sdk_plugin_param_info(casc_sdk_plugin* p, int index, casc_param_info_t* out) {
    if (!p || !out) return CASC_ERR_INVALID_ARG;
    return casc_plugin_get_param_info(p->loaded, index, out);
}

int casc_sdk_plugin_has_gui(casc_sdk_plugin* p)       { return p ? casc_plugin_has_ui(p->loaded) : 0; }
int casc_sdk_plugin_gui_width(casc_sdk_plugin* p)     { return p ? casc_plugin_get_ui_width(p->loaded) : 0; }
int casc_sdk_plugin_gui_height(casc_sdk_plugin* p)    { return p ? casc_plugin_get_ui_height(p->loaded) : 0; }
int casc_sdk_plugin_gui_resizable(casc_sdk_plugin* p) { return p ? casc_plugin_get_ui_resizable(p->loaded) : 0; }

casc_plugin_t* casc_sdk_plugin_raw(casc_sdk_plugin* p) { return p ? p->loaded : NULL; }

/* ---- voice ---- */

casc_sdk_voice* casc_sdk_voice_new(casc_sdk_plugin* p, double sr, int bs) {
    if (!p || !p->loaded) return NULL;
    casc_sdk_voice* v = (casc_sdk_voice*)calloc(1, sizeof(*v));
    if (!v) return NULL;
    v->owner = p;
    v->sample_rate = sr;
    v->max_block = bs;
    v->inst = casc_instantiate(p->loaded, sr, bs);
    if (!v->inst) { free(v); return NULL; }
    return v;
}

void casc_sdk_voice_free(casc_sdk_voice* v) {
    if (!v) return;
    if (v->inst) {
        casc_close_ui(v->inst);
        casc_destroy_instance(v->inst);
    }
    free(v);
}

void casc_sdk_voice_reset(casc_sdk_voice* v, double sr, int bs) {
    if (!v || !v->inst) return;
    v->sample_rate = sr; v->max_block = bs;
    casc_reset(v->inst, sr, bs);
}

void casc_sdk_voice_process(casc_sdk_voice* v, const float** in, float** out, int frames) {
    if (v && v->inst) casc_process(v->inst, in, out, frames);
}
void casc_sdk_voice_send_midi(casc_sdk_voice* v, const casc_midi_event_t* ev, int n) {
    if (v && v->inst) casc_send_midi(v->inst, ev, n);
}
void   casc_sdk_voice_set_param(casc_sdk_voice* v, int id, double val) { if (v && v->inst) casc_set_param(v->inst, id, val); }
double casc_sdk_voice_get_param(casc_sdk_voice* v, int id) { return (v && v->inst) ? casc_get_param(v->inst, id) : 0.0; }

void* casc_sdk_voice_save_state(casc_sdk_voice* v, size_t* out_size) {
    if (out_size) *out_size = 0;
    return (v && v->inst) ? casc_save_state(v->inst, out_size) : NULL;
}
int casc_sdk_voice_load_state(casc_sdk_voice* v, const void* data, size_t size) {
    return (v && v->inst) ? casc_load_state(v->inst, data, size) : CASC_ERR_INVALID_ARG;
}
int casc_sdk_voice_latency(casc_sdk_voice* v) { return (v && v->inst) ? casc_get_latency(v->inst) : 0; }
int casc_sdk_voice_tail(casc_sdk_voice* v)    { return (v && v->inst) ? casc_get_tail(v->inst) : 0; }
casc_instance_t* casc_sdk_voice_raw(casc_sdk_voice* v) { return v ? v->inst : NULL; }

/* ---- GUI ---- */

void casc_sdk_voice_on_param_edit(casc_sdk_voice* v, casc_sdk_param_edit_cb cb, void* user) {
    if (v && v->inst) casc_set_ui_param_callback(v->inst, (casc_ui_param_cb)cb, user);
}
int casc_sdk_voice_open_gui(casc_sdk_voice* v, void* parent) {
    if (!v || !v->inst) return CASC_ERR_INVALID_ARG;
    return casc_open_ui(v->inst, parent);
}
void casc_sdk_voice_close_gui(casc_sdk_voice* v) { if (v && v->inst) casc_close_ui(v->inst); }
int  casc_sdk_voice_gui_is_open(casc_sdk_voice* v) { return (v && v->inst) ? casc_ui_is_open(v->inst) : 0; }
int  casc_sdk_voice_set_gui_size(casc_sdk_voice* v, int w, int h) {
    return (v && v->inst) ? casc_set_ui_size(v->inst, w, h) : CASC_ERR_INVALID_ARG;
}
void casc_sdk_voice_gui_tick(casc_sdk_voice* v) { if (v && v->inst) casc_ui_tick(v->inst); }
void casc_sdk_voice_gui_notify_param(casc_sdk_voice* v, int id, double val) {
    if (v && v->inst) casc_ui_notify_param(v->inst, id, val);
}

#endif /* CASC_DAW_SDK_IMPLEMENTATION */

#endif /* CASC_DAW_SDK_H */
