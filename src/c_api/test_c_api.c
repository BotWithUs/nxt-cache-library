/* Smoke test for the NXTCache C ABI. Build and run from the project root:
 *   cl /std:c17 /Fe:cmake-build-debug\test_c_api.exe src\c_api\test_c_api.c \
 *      cmake-build-debug\NXTCache.lib /I src
 *   cmake-build-debug\test_c_api.exe <cache-path>
 */

#include "c_api/nxtcache_c.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int check(nxt_cache *c, const char *type, int id, const char *name_hint)
{
    char *json = NULL;
    size_t len = 0;
    nxt_result rc = nxt_get_json(c, type, id, &json, &len);
    if (rc != NXT_OK)
    {
        fprintf(stderr, "FAIL %s %d: rc=%d err=%s\n", type, id, rc, nxt_last_error());
        return 1;
    }
    int ok = name_hint == NULL || strstr(json, name_hint) != NULL;
    printf("  %s %-4d  %zu bytes  %s\n", type, id, len, ok ? "OK" : "FAIL (missing hint)");
    if (!ok) printf("    json: %.200s\n", json);
    nxt_free(json);
    return ok ? 0 : 1;
}

/* Validate the sprite raw getters: pixel buffer size must equal w*h*4 and an
   opaque sprite should carry some non-transparent pixels. Returns failure count. */
static int check_sprite(nxt_cache *c, int id)
{
    int32_t frames = 0, cw = 0, ch = 0, pal = 0;
    nxt_result rc = nxt_get_sprite_info(c, id, &frames, &cw, &ch, &pal);
    if (rc != NXT_OK)
    {
        fprintf(stderr, "FAIL sprite_info %d: rc=%d err=%s\n", id, rc, nxt_last_error());
        return 1;
    }
    printf("  sprite %-4d  frames=%d canvas=%dx%d palette=%d\n", id, frames, cw, ch, pal);
    if (frames <= 0) { fprintf(stderr, "FAIL sprite %d: no frames\n", id); return 1; }

    uint8_t *rgba = NULL;
    size_t count = 0;
    int32_t w = 0, h = 0, ox = 0, oy = 0;
    rc = nxt_get_sprite_frame_rgba(c, id, 0, &rgba, &count, &w, &h, &ox, &oy);
    if (rc != NXT_OK)
    {
        fprintf(stderr, "FAIL sprite_frame_rgba %d: rc=%d err=%s\n", id, rc, nxt_last_error());
        return 1;
    }
    int fails = 0;
    if (count != (size_t)w * (size_t)h * 4)
    {
        fprintf(stderr, "FAIL sprite %d: count=%zu but w*h*4=%d\n", id, count, w * h * 4);
        fails++;
    }
    size_t opaque = 0;
    for (size_t i = 3; i < count; i += 4) if (rgba[i] != 0) opaque++;
    printf("    frame0 %dx%d  %zu bytes  %zu opaque px  off=(%d,%d)  %s\n",
           w, h, count, opaque, ox, oy, opaque > 0 ? "OK" : "FAIL (all transparent)");
    if (opaque == 0) fails++;
    nxt_free(rgba);
    return fails;
}

/* Best-effort model check (non-fatal): every face index must be in range. */
static void check_model(nxt_cache *c, int id)
{
    nxt_model_info info;
    nxt_result rc = nxt_get_model_info(c, id, &info);
    if (rc != NXT_OK)
    {
        printf("  model %-5d  unavailable rc=%d err=%s\n", id, rc, nxt_last_error());
        return;
    }
    printf("  model %-5d  fmt%d v%d  meshes=%d verts=%d faces=%d skins=%d bbox=[%d,%d,%d..%d,%d,%d]\n",
           id, info.format, info.version, info.mesh_count, info.vertex_count, info.face_count,
           info.has_skins, info.min_x, info.min_y, info.min_z, info.max_x, info.max_y, info.max_z);

    nxt_model_face *faces = NULL;
    size_t fn = 0;
    if (nxt_get_model_faces(c, id, &faces, &fn) == NXT_OK)
    {
        size_t bad = 0;
        for (size_t i = 0; i < fn; i++)
        {
            if (faces[i].a < 0 || faces[i].a >= info.vertex_count ||
                faces[i].b < 0 || faces[i].b >= info.vertex_count ||
                faces[i].c < 0 || faces[i].c >= info.vertex_count) bad++;
        }
        printf("    %zu faces, %zu out-of-range indices  %s\n",
               fn, bad, bad == 0 ? "OK" : "FAIL");
        nxt_free(faces);
    }

    float *uv = NULL;
    size_t un = 0;
    if (nxt_get_model_uvs(c, id, &uv, &un) == NXT_OK && un > 0)
    {
        float lo = uv[0], hi = uv[0];
        for (size_t i = 0; i < un; i++) { if (uv[i] < lo) lo = uv[i]; if (uv[i] > hi) hi = uv[i]; }
        printf("    uv range [%.3f, %.3f] over %zu coords\n", lo, hi, un);
        nxt_free(uv);
    }
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: %s <cache-path> | --live\n", argv[0]);
        return 2;
    }

    nxt_cache *c;
    int live = strcmp(argv[1], "--live") == 0;
    if (live)
    {
        printf("opening live JS5\n");
        c = nxt_cache_open_live();
    }
    else
    {
        printf("opening %s\n", argv[1]);
        c = nxt_cache_open_local(argv[1]);
    }
    if (!c)
    {
        fprintf(stderr, "open failed: %s\n", nxt_last_error());
        return 1;
    }

    int failures = 0;

    /* Sprite raw-getter validation (works against live or a full local cache). */
    failures += check_sprite(c, 0);
    failures += check_sprite(c, 2);

    /* Model decode is best-effort: index 7 is LZMA-wrapped and may be absent
       from a partial cache, so these are reported but not counted as failures. */
    check_model(c, 19);
    check_model(c, 131);
    check_model(c, 200);

    failures += check(c, "item",  4151, "Abyssal whip");
    failures += check(c, "npc",   0,    "Hans");
    failures += check(c, "quest", 1,    "Tower of Life");
    failures += check(c, "inv",   93,   "\"capacity\":28");

    /* nxt_get_json with a missing id should report NXT_ERR_NOT_FOUND */
    char *json = NULL;
    size_t len = 0;
    nxt_result rc = nxt_get_json(c, "item", 999999, &json, &len);
    printf("  missing item rc=%d (expected %d) err=%s\n",
           rc, NXT_ERR_NOT_FOUND, nxt_last_error());
    if (rc != NXT_ERR_NOT_FOUND) failures++;

    /* Dump first 5 items via nxt_dump_all_json */
    rc = nxt_dump_all_json(c, "item", 5, &json, &len);
    if (rc == NXT_OK)
    {
        printf("  dump_all item limit=5 -> %zu bytes\n", len);
        nxt_free(json);
    }
    else
    {
        printf("  dump_all FAILED rc=%d err=%s\n", rc, nxt_last_error());
        failures++;
    }

    nxt_cache_close(c);
    printf("%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
