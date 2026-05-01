/* Verifies nxt_cache_enable_live_fallback transparently fetches a missing
 * archive from the live JS5 server. Setup: a copy of the user's items
 * jcache with the cache row for archive 16 deleted (where item 4151 lives).
 */

#include "c_api/nxtcache_c.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: %s <gutted-cache-path>\n", argv[0]);
        return 2;
    }

    nxt_cache *c = nxt_cache_open_local(argv[1]);
    if (!c) { fprintf(stderr, "open failed: %s\n", nxt_last_error()); return 1; }

    char *json = NULL; size_t len = 0;

    /* Without fallback: should be NXT_ERR_NOT_FOUND */
    nxt_result rc = nxt_get_item_json(c, 4151, &json, &len);
    printf("before enable_live_fallback: rc=%d err='%s'\n", rc, nxt_last_error());
    if (rc == NXT_OK) nxt_free(json);

    /* Enable fallback */
    rc = nxt_cache_enable_live_fallback(c);
    if (rc != NXT_OK)
    {
        fprintf(stderr, "enable_live_fallback failed: %s\n", nxt_last_error());
        nxt_cache_close(c);
        return 1;
    }
    printf("fallback enabled\n");

    /* With fallback: should succeed */
    rc = nxt_get_item_json(c, 4151, &json, &len);
    if (rc != NXT_OK)
    {
        fprintf(stderr, "after fallback rc=%d err=%s\n", rc, nxt_last_error());
        nxt_cache_close(c);
        return 1;
    }
    int ok = strstr(json, "Abyssal whip") != NULL;
    printf("after fallback: %zu bytes  %s\n", len, ok ? "OK" : "FAIL");
    nxt_free(json);
    nxt_cache_close(c);
    return ok ? 0 : 1;
}
