/*
 * reaper_casc.c — Native CASC compatibility extension for REAPER
 * ==============================================================
 *
 *   Builds to reaper_casc.dll / reaper_casc.dylib / reaper_casc.so — one source,
 *   identical behaviour on Windows, macOS and Linux.
 *
 * WHAT IT DOES
 * ------------
 * REAPER already hosts CLAP plugins natively. The CASC project ships a CLAP
 * bridge (casc-clap-bridge.clap) that exposes every installed .casc file as a
 * native CLAP plugin. This extension makes that pipeline first-class inside
 * REAPER:
 *
 *   1. On load, scans the standard CASC install directories (via the CASC DAW
 *      SDK) and logs what's available, so the user can confirm discovery.
 *   2. Registers REAPER actions (Actions list / can be bound to keys/toolbar):
 *        - "CASC: Rescan plugin folder"
 *        - "CASC: List installed CASC plugins (console)"
 *        - "CASC: Open CASC plugins folder"
 *   3. Ensures the CASC CLAP bridge is discoverable by REAPER by reporting the
 *      bridge location and the per-OS install folder REAPER scans for CLAPs.
 *
 * Because all GUI hosting (WKWebView / WebView2 / WebKitGTK) and DSP execution
 * live in libcasc + the CLAP bridge, this extension stays tiny and 100% portable.
 *
 * SPDX-License-Identifier: MIT
 */

#include "reaper_plugin_min.h"

#define CASC_DAW_SDK_IMPLEMENTATION
#include "casc_daw_sdk.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

#if defined(_WIN32)
#  include <windows.h>
#elif defined(__APPLE__)
#  include <unistd.h>
#else
#  include <unistd.h>
#endif

/* ---- REAPER API function pointers we resolve at load time ---- */
static void  (*RPR_ShowConsoleMsg)(const char* msg) = NULL;
static int   (*RPR_AddExtensionsMainMenu)(void) = NULL;
static reaper_plugin_info_t* g_rec = NULL;

/* ---- our session ---- */
static casc_sdk_session* g_session = NULL;

/* command ids assigned by REAPER at registration */
static int g_cmd_rescan = 0;
static int g_cmd_list   = 0;
static int g_cmd_folder = 0;

/* ------------------------------------------------------------------------- */
static void casc_log(const char* fmt, ...) {
    char buf[2048];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (RPR_ShowConsoleMsg) RPR_ShowConsoleMsg(buf);
}

/* The folder REAPER + the CASC CLAP bridge scan for .casc plugins. */
static void casc_plugins_folder(char* out, size_t n) {
#if defined(__APPLE__)
    const char* home = getenv("HOME");
    snprintf(out, n, "%s/Library/Audio/Plug-Ins/CASC", home ? home : "");
#elif defined(_WIN32)
    const char* ad = getenv("APPDATA");
    snprintf(out, n, "%s\\CASC", ad ? ad : "");
#else
    const char* home = getenv("HOME");
    snprintf(out, n, "%s/.local/lib/casc", home ? home : "");
#endif
}

static void casc_open_folder_in_os(const char* path) {
    char cmd[1200];
#if defined(__APPLE__)
    snprintf(cmd, sizeof(cmd), "open \"%s\"", path);
#elif defined(_WIN32)
    snprintf(cmd, sizeof(cmd), "explorer \"%s\"", path);
#else
    snprintf(cmd, sizeof(cmd), "xdg-open \"%s\" >/dev/null 2>&1", path);
#endif
    int rc = system(cmd);
    (void)rc;
}

/* ------------------------------------------------------------------------- */
static void action_list_plugins(void) {
    if (!g_session) { casc_log("[CASC] no session\n"); return; }
    int n = casc_sdk_plugin_count(g_session);
    casc_log("[CASC] %d plugin(s) installed (runtime %s):\n",
             n, casc_sdk_runtime_version());
    for (int i = 0; i < n; i++) {
        casc_sdk_plugin* p = casc_sdk_plugin_at(g_session, i);
        casc_log("  - %s  (%s v%s) %s%s\n",
                 casc_sdk_plugin_name(p),
                 casc_sdk_plugin_vendor(p),
                 casc_sdk_plugin_version(p),
                 casc_sdk_plugin_is_instrument(p) ? "[instrument] " : "[fx] ",
                 casc_sdk_plugin_has_gui(p) ? "[gui]" : "");
    }
    casc_log("[CASC] These are exposed to REAPER as CLAP plugins via "
             "casc-clap-bridge.clap.\n");
}

static void action_rescan(void) {
    if (!g_session) return;
    casc_sdk_rescan(g_session);
    casc_log("[CASC] Rescanned. %d plugin(s) now visible.\n",
             casc_sdk_plugin_count(g_session));
}

static void action_open_folder(void) {
    char folder[1024];
    casc_plugins_folder(folder, sizeof(folder));
    casc_log("[CASC] Plugins folder: %s\n", folder);
    casc_open_folder_in_os(folder);
}

/* REAPER calls this for every action; we match on our assigned command ids. */
static int on_command(int command, int flag) {
    (void)flag;
    if (command && command == g_cmd_rescan) { action_rescan();      return 1; }
    if (command && command == g_cmd_list)   { action_list_plugins(); return 1; }
    if (command && command == g_cmd_folder) { action_open_folder();  return 1; }
    return 0;  /* not ours */
}

/* ------------------------------------------------------------------------- */
static int register_action(const char* id_str, const char* name) {
    custom_action_register_t ca = {0};
    ca.uniqueSectionId = 0;       /* main section */
    ca.id_str = id_str;
    ca.name   = name;
    /* "custom_action" returns the command id (>0) on success. */
    return g_rec->Register("custom_action", &ca);
}

REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(
        REAPER_PLUGIN_HINSTANCE hInstance, reaper_plugin_info_t* rec) {
    (void)hInstance;

    if (!rec) {
        /* Unload. */
        if (g_session) { casc_sdk_close(g_session); g_session = NULL; }
        return 0;
    }
    if (rec->caller_version != REAPER_PLUGIN_VERSION || !rec->GetFunc)
        return 0;

    g_rec = rec;
    RPR_ShowConsoleMsg = (void(*)(const char*))rec->GetFunc("ShowConsoleMsg");

    /* Discover installed CASC plugins through the portable SDK. */
    g_session = casc_sdk_open(NULL);

    /* Register actions, then hook the command dispatcher. */
    g_cmd_rescan = register_action("CASC_RESCAN", "CASC: Rescan plugin folder");
    g_cmd_list   = register_action("CASC_LIST",   "CASC: List installed CASC plugins (console)");
    g_cmd_folder = register_action("CASC_FOLDER", "CASC: Open CASC plugins folder");
    rec->Register("hookcommand", (void*)on_command);

    char folder[1024];
    casc_plugins_folder(folder, sizeof(folder));
    casc_log("[CASC] reaper_casc loaded. Runtime %s. %d plugin(s) in %s\n",
             casc_sdk_runtime_version(),
             casc_sdk_plugin_count(g_session), folder);
    casc_log("[CASC] Drop .casc files there; they appear in REAPER's FX browser "
             "(CLAP) via casc-clap-bridge.clap.\n");
    return 1;  /* keep loaded */
}
