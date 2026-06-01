/*
 * reaper_plugin_min.h — Minimal REAPER extension SDK surface
 * ----------------------------------------------------------
 * Just enough of the public REAPER extension ABI to build reaper_casc without
 * vendoring the full SDK. The full SDK lives at:
 *   https://www.reaper.fm/sdk/plugin/  (reaper_plugin.h, reaper_plugin_functions.h)
 *
 * The ABI defined here (reaper_plugin_info_t, REAPER_PLUGIN_ENTRYPOINT,
 * custom_action_register_t, gaccel_register_t) is stable and documented.
 * SPDX-License-Identifier: MIT (this shim)
 */
#ifndef REAPER_PLUGIN_MIN_H
#define REAPER_PLUGIN_MIN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define REAPER_PLUGIN_VERSION 0x20E

/* Platform export macro for the entry point. */
#ifdef _WIN32
#  define REAPER_PLUGIN_DLL_EXPORT __declspec(dllexport)
#  define REAPER_PLUGIN_HINSTANCE  void*    /* HINSTANCE */
#  define REAPER_PLUGIN_HWND        void*   /* HWND */
#else
#  define REAPER_PLUGIN_DLL_EXPORT __attribute__((visibility("default")))
#  define REAPER_PLUGIN_HINSTANCE  void*
#  define REAPER_PLUGIN_HWND       void*
#endif

/* Passed to the entry point. GetFunc resolves any REAPER API by name. */
typedef struct reaper_plugin_info_t {
    int caller_version;                 /* == REAPER_PLUGIN_VERSION */
    REAPER_PLUGIN_HWND hwnd_main;
    int (*Register)(const char* name, void* infostruct);
    void* (*GetFunc)(const char* name);
} reaper_plugin_info_t;

/* Action accelerator/registration record (for "gaccel"). */
typedef struct ACCEL_min {
    uint8_t  fVirt;
    uint16_t key;
    uint16_t cmd;
} ACCEL_min;

typedef struct gaccel_register_t {
    ACCEL_min   accel;     /* accel.cmd is filled by REAPER on register */
    const char* desc;      /* menu/action description */
} gaccel_register_t;

/* Named command registration ("custom_action") for actions with string ids. */
typedef struct custom_action_register_t {
    int         uniqueSectionId;  /* 0 = main */
    const char* id_str;           /* unique string id, e.g. "CASC_RESCAN" */
    const char* name;             /* display name */
    void*       extra;
} custom_action_register_t;

/* Entry point REAPER calls on load (rec != NULL) and unload (rec == NULL). */
#define REAPER_PLUGIN_ENTRYPOINT ReaperPluginEntry
typedef int (*reaper_plugin_entry_fn)(REAPER_PLUGIN_HINSTANCE hInstance,
                                      reaper_plugin_info_t* rec);

#ifdef __cplusplus
}
#endif
#endif /* REAPER_PLUGIN_MIN_H */
