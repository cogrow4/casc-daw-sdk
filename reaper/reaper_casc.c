/*
 * reaper_casc.c — Native CASC compatibility extension for REAPER
 * ==============================================================
 *
 *   Builds to reaper_casc.dll / reaper_casc.dylib / reaper_casc.so — one source,
 *   identical behaviour on Windows, macOS and Linux.
 *
 * WHAT IT DOES — "drag it in and CASC just works"
 * -----------------------------------------------
 * REAPER's public extension API has no hook to register a brand-new FX *format*
 * (VST/CLAP/JS loaders are internal). REAPER does, however, natively host CLAP
 * plugins. The CASC project ships a CLAP bridge that exposes every installed
 * .casc file as a native CLAP plugin (with its real WebView GUI).
 *
 * So this extension's job is to make that automatic: drop reaper_casc into
 * REAPER's UserPlugins folder and on load it
 *
 *   1. ensures the CASC plugins folder exists (where you drop .casc files),
 *   2. finds the CASC CLAP bridge and installs it into REAPER's CLAP scan path
 *      if it isn't already there, and
 *   3. asks REAPER to rescan, so .casc plugins appear in the FX browser with
 *      no manual CLAP setup.
 *
 * It also registers convenience actions (rescan / list / open folder).
 *
 * The bridge is located, in order:
 *   - $CASC_CLAP_BRIDGE                       (explicit path to the .clap)
 *   - next to this extension                  (UserPlugins/casc-clap-bridge.clap)
 *   - the standard CASC install location      (per-OS Audio Plug-Ins/CLAP)
 *
 * All DSP + GUI hosting lives in libcasc + the bridge, so this stays tiny and
 * 100% portable.
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
#  include <direct.h>
#  define CASC_DIRSEP "\\"
#else
#  include <unistd.h>
#  include <sys/stat.h>
#  include <dirent.h>
#  define CASC_DIRSEP "/"
#endif

/* ---- REAPER API function pointers resolved at load time ---- */
static void (*RPR_ShowConsoleMsg)(const char* msg) = NULL;
/* GetExePath() -> REAPER resource/exe path; used to locate UserPlugins + CLAP. */
static const char* (*RPR_GetResourcePath)(void) = NULL;
static reaper_plugin_info_t* g_rec = NULL;

static casc_sdk_session* g_session = NULL;

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

static int casc_file_exists(const char* path) {
#if defined(_WIN32)
    DWORD a = GetFileAttributesA(path);
    return (a != INVALID_FILE_ATTRIBUTES);
#else
    struct stat st;
    return stat(path, &st) == 0;
#endif
}

static void casc_mkdir_p(const char* path) {
    /* shallow recursive mkdir */
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char c = *p; *p = 0;
#if defined(_WIN32)
            _mkdir(tmp);
#else
            mkdir(tmp, 0755);
#endif
            *p = c;
        }
    }
#if defined(_WIN32)
    _mkdir(tmp);
#else
    mkdir(tmp, 0755);
#endif
}

/* The folder where the user drops .casc files. */
static void casc_plugins_folder(char* out, size_t n) {
#if defined(__APPLE__)
    const char* home = getenv("HOME");
    snprintf(out, n, "%s/Library/Audio/Plug-Ins/CASC", home ? home : "");
#elif defined(_WIN32)
    const char* home = getenv("APPDATA");
    snprintf(out, n, "%s\\CASC", home ? home : "");
#else
    const char* home = getenv("HOME");
    snprintf(out, n, "%s/.local/lib/casc", home ? home : "");
#endif
}

/* REAPER's CLAP scan path (under the resource path). */
static void casc_reaper_clap_dir(char* out, size_t n) {
    const char* res = RPR_GetResourcePath ? RPR_GetResourcePath() : NULL;
    if (res && res[0])
        snprintf(out, n, "%s" CASC_DIRSEP "UserPlugins" CASC_DIRSEP "FX", res);
    else
        out[0] = 0;
}

/* The bridge bundle/file name per OS. */
static const char* casc_bridge_name(void) {
#if defined(__APPLE__)
    return "casc-clap-bridge.clap";   /* bundle dir */
#elif defined(_WIN32)
    return "casc-clap-bridge.clap";   /* dll with .clap ext */
#else
    return "casc-clap-bridge.clap";   /* so with .clap ext */
#endif
}

/* Standard CASC bridge install location (sibling CLAP folder). */
static void casc_standard_bridge_path(char* out, size_t n) {
#if defined(__APPLE__)
    const char* home = getenv("HOME");
    snprintf(out, n, "%s/Library/Audio/Plug-Ins/CLAP/%s",
             home ? home : "", casc_bridge_name());
#elif defined(_WIN32)
    const char* cpf = getenv("COMMONPROGRAMFILES");
    snprintf(out, n, "%s\\CLAP\\%s", cpf ? cpf : "", casc_bridge_name());
#else
    const char* home = getenv("HOME");
    snprintf(out, n, "%s/.clap/%s", home ? home : "", casc_bridge_name());
#endif
}

/* Recursively copy a file or directory (for the macOS .clap bundle). */
static int casc_copy_path(const char* src, const char* dst) {
    char cmd[2300];
#if defined(_WIN32)
    /* xcopy handles both file and dir; /E /I /Y for dirs, copy for files. */
    snprintf(cmd, sizeof(cmd),
             "cmd /c (if exist \"%s\\*\" (xcopy \"%s\" \"%s\" /E /I /Y >nul) "
             "else (copy /Y \"%s\" \"%s\" >nul))", src, src, dst, src, dst);
#else
    snprintf(cmd, sizeof(cmd), "cp -R \"%s\" \"%s\"", src, dst);
#endif
    return system(cmd);
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
    int rc = system(cmd); (void)rc;
}

/* ---- locate the CASC CLAP bridge on disk -------------------------------- */
static int casc_find_bridge(char* out, size_t n) {
    /* 1. explicit env override */
    const char* env = getenv("CASC_CLAP_BRIDGE");
    if (env && env[0] && casc_file_exists(env)) {
        snprintf(out, n, "%s", env);
        return 1;
    }
    /* 2. next to this extension: <ResourcePath>/UserPlugins/<bridge> */
    if (RPR_GetResourcePath) {
        char cand[1024];
        snprintf(cand, sizeof(cand), "%s" CASC_DIRSEP "UserPlugins" CASC_DIRSEP "%s",
                 RPR_GetResourcePath(), casc_bridge_name());
        if (casc_file_exists(cand)) { snprintf(out, n, "%s", cand); return 1; }
    }
    /* 3. standard CASC install location */
    char std[1024];
    casc_standard_bridge_path(std, sizeof(std));
    if (casc_file_exists(std)) { snprintf(out, n, "%s", std); return 1; }
    return 0;
}

/* Ensure the bridge is present in REAPER's CLAP scan path. */
static int casc_autowire_bridge(void) {
    char clapdir[1024];
    casc_reaper_clap_dir(clapdir, sizeof(clapdir));
    if (!clapdir[0]) { casc_log("[CASC] could not resolve REAPER FX path\n"); return 0; }
    casc_mkdir_p(clapdir);

    char target[1200];
    snprintf(target, sizeof(target), "%s" CASC_DIRSEP "%s", clapdir, casc_bridge_name());
    if (casc_file_exists(target)) {
        casc_log("[CASC] bridge already installed: %s\n", target);
        return 1;
    }
    char src[1024];
    if (!casc_find_bridge(src, sizeof(src))) {
        casc_log("[CASC] CLAP bridge not found. Set $CASC_CLAP_BRIDGE or place "
                 "%s next to the extension / in the CLAP folder.\n",
                 casc_bridge_name());
        return 0;
    }
    int rc = casc_copy_path(src, target);
    if (rc == 0 && casc_file_exists(target)) {
        casc_log("[CASC] installed bridge -> %s\n", target);
        return 1;
    }
    casc_log("[CASC] failed to install bridge from %s (rc=%d)\n", src, rc);
    return 0;
}

/* ------------------------------------------------------------------------- */
static void action_list_plugins(void) {
    if (!g_session) { casc_log("[CASC] no session\n"); return; }
    int nn = casc_sdk_plugin_count(g_session);
    casc_log("[CASC] %d plugin(s) installed (runtime %s):\n",
             nn, casc_sdk_runtime_version());
    for (int i = 0; i < nn; i++) {
        casc_sdk_plugin* p = casc_sdk_plugin_at(g_session, i);
        casc_log("  - %s  (%s v%s) %s%s\n",
                 casc_sdk_plugin_name(p), casc_sdk_plugin_vendor(p),
                 casc_sdk_plugin_version(p),
                 casc_sdk_plugin_is_instrument(p) ? "[instrument] " : "[fx] ",
                 casc_sdk_plugin_has_gui(p) ? "[gui]" : "");
    }
    casc_log("[CASC] These load in REAPER as native CLAP plugins "
             "(search 'CASC' in the FX browser).\n");
}

static void action_rescan(void) {
    if (g_session) casc_sdk_rescan(g_session);
    casc_autowire_bridge();
    casc_log("[CASC] Rescanned. %d plugin(s). Use FX browser > Scan to refresh "
             "REAPER's CLAP list if needed.\n",
             g_session ? casc_sdk_plugin_count(g_session) : 0);
}

static void action_open_folder(void) {
    char folder[1024];
    casc_plugins_folder(folder, sizeof(folder));
    casc_mkdir_p(folder);
    casc_log("[CASC] Plugins folder: %s\n", folder);
    casc_open_folder_in_os(folder);
}

static int on_command(int command, int flag) {
    (void)flag;
    if (command && command == g_cmd_rescan) { action_rescan();       return 1; }
    if (command && command == g_cmd_list)   { action_list_plugins(); return 1; }
    if (command && command == g_cmd_folder) { action_open_folder();  return 1; }
    return 0;
}

static int register_action(const char* id_str, const char* name) {
    custom_action_register_t ca = {0};
    ca.uniqueSectionId = 0;
    ca.id_str = id_str;
    ca.name   = name;
    return g_rec->Register("custom_action", &ca);
}

/* ------------------------------------------------------------------------- */
REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(
        REAPER_PLUGIN_HINSTANCE hInstance, reaper_plugin_info_t* rec) {
    (void)hInstance;

    if (!rec) {                       /* unload */
        if (g_session) { casc_sdk_close(g_session); g_session = NULL; }
        return 0;
    }
    if (rec->caller_version != REAPER_PLUGIN_VERSION || !rec->GetFunc)
        return 0;

    g_rec = rec;
    RPR_ShowConsoleMsg  = (void(*)(const char*))rec->GetFunc("ShowConsoleMsg");
    RPR_GetResourcePath = (const char*(*)(void))rec->GetFunc("GetResourcePath");

    /* 1. make sure the drop-in folder exists */
    char folder[1024];
    casc_plugins_folder(folder, sizeof(folder));
    casc_mkdir_p(folder);

    /* 2. discover what's installed */
    g_session = casc_sdk_open(NULL);

    /* 3. auto-wire the CLAP bridge into REAPER's scan path */
    int wired = casc_autowire_bridge();

    /* 4. actions */
    g_cmd_rescan = register_action("CASC_RESCAN", "CASC: Rescan + re-wire bridge");
    g_cmd_list   = register_action("CASC_LIST",   "CASC: List installed CASC plugins (console)");
    g_cmd_folder = register_action("CASC_FOLDER", "CASC: Open CASC plugins folder");
    rec->Register("hookcommand", (void*)on_command);

    casc_log("[CASC] reaper_casc loaded (runtime %s). %d plugin(s) in %s\n",
             casc_sdk_runtime_version(),
             casc_sdk_plugin_count(g_session), folder);
    if (wired)
        casc_log("[CASC] Bridge wired. Drop .casc files in the folder above, then "
                 "find them under 'CASC' in REAPER's FX browser (rescan CLAPs once).\n");
    return 1;
}
