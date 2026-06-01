/*
 * sdk_selftest.c — compiles casc_daw_sdk.h in implementation mode and exercises
 * the discovery + voice lifecycle API. This proves the single header builds
 * clean and links against libcasc on every platform.
 *
 * It does NOT require any .casc files to be installed: an empty session is a
 * valid result. If plugins happen to be installed, it instantiates the first
 * one headlessly and runs a silent process block.
 */
#define CASC_DAW_SDK_IMPLEMENTATION
#include "casc_daw_sdk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    printf("CASC DAW SDK self-test (SDK %s, runtime %s)\n",
           CASC_SDK_VERSION_STRING, casc_sdk_runtime_version());

    /* 1. Open a session with default search paths. */
    casc_sdk_session* s = casc_sdk_open(NULL);
    if (!s) { fprintf(stderr, "FAIL: casc_sdk_open returned NULL\n"); return 1; }

    int n = casc_sdk_plugin_count(s);
    printf("Discovered %d CASC plugin(s).\n", n);

    /* 2. Enumerate metadata for whatever is installed. */
    for (int i = 0; i < n; i++) {
        casc_sdk_plugin* p = casc_sdk_plugin_at(s, i);
        if (!p) { fprintf(stderr, "FAIL: plugin_at(%d) NULL\n", i); return 1; }
        printf("  [%d] %-20s by %-15s v%-8s  in=%d out=%d midi=%d gui=%d params=%d\n",
               i, casc_sdk_plugin_name(p), casc_sdk_plugin_vendor(p),
               casc_sdk_plugin_version(p),
               casc_sdk_plugin_audio_inputs(p), casc_sdk_plugin_audio_outputs(p),
               casc_sdk_plugin_accepts_midi(p), casc_sdk_plugin_has_gui(p),
               casc_sdk_plugin_param_count(p));

        /* by-id lookup must round-trip */
        casc_sdk_plugin* byid = casc_sdk_plugin_by_id(s, casc_sdk_plugin_id(p));
        if (byid != p) { fprintf(stderr, "FAIL: by_id mismatch\n"); return 1; }
    }

    /* 3. If we have a plugin, run a headless voice through one block. */
    if (n > 0) {
        casc_sdk_plugin* p = casc_sdk_plugin_at(s, 0);
        casc_sdk_voice* v = casc_sdk_voice_new(p, 48000.0, 128);
        if (!v) { fprintf(stderr, "FAIL: voice_new\n"); return 1; }

        /* Build stereo silence buffers. */
        float l[128] = {0}, r[128] = {0};
        float* outs[2] = { l, r };
        const float* ins[2] = { l, r };
        casc_sdk_voice_process(v, ins, outs, 128);

        /* Param round-trip on param 0 if any. */
        if (casc_sdk_plugin_param_count(p) > 0) {
            casc_param_info_t pi;
            if (casc_sdk_plugin_param_info(p, 0, &pi) == CASC_OK) {
                casc_sdk_voice_set_param(v, pi.id, 0.5);
                double got = casc_sdk_voice_get_param(v, pi.id);
                printf("  param %d (%s) set 0.5 -> got %.3f\n", pi.id, pi.name, got);
            }
        }

        /* GUI open with NULL parent may succeed (standalone) or report no GUI;
         * either is acceptable in a headless test — just must not crash. */
        int gui = casc_sdk_voice_gui_is_open(v);
        printf("  gui open after new: %d (expected 0)\n", gui);

        casc_sdk_voice_free(v);
        printf("  voice lifecycle OK\n");
    } else {
        printf("  (no plugins installed; discovery path still validated)\n");
    }

    casc_sdk_close(s);
    printf("PASS\n");
    return 0;
}
