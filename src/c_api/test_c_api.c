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

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: %s <cache-path>\n", argv[0]);
        return 2;
    }

    printf("opening %s\n", argv[1]);
    nxt_cache *c = nxt_cache_open_local(argv[1]);
    if (!c)
    {
        fprintf(stderr, "open failed: %s\n", nxt_last_error());
        return 1;
    }

    int failures = 0;
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
