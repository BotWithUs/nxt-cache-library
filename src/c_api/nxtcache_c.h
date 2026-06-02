/*
 * NXTCache C ABI — stable extern "C" surface intended for FFI consumers
 * (Java + Project Panama, .NET P/Invoke, Python ctypes, etc.).
 *
 * Conventions
 * -----------
 *  - Lifetimes: opaque handles created by nxt_cache_open_* must be released
 *    with nxt_cache_close. Output buffers from getters are allocated by the
 *    library and must be released with nxt_free.
 *  - Strings: char* outputs are NUL-terminated UTF-8. *_len fields contain
 *    the byte length excluding the NUL.
 *  - Errors: functions returning nxt_result return 0 on success, non-zero
 *    on failure. Call nxt_last_error() (thread-local) to get a human-readable
 *    message.
 *  - Threading: a single nxt_cache handle is NOT safe for concurrent use.
 *    nxt_last_error()'s buffer is thread-local.
 */

#ifndef NXTCACHE_C_H
#define NXTCACHE_C_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
  #if defined(NXTCACHE_BUILDING)
    #define NXT_API __declspec(dllexport)
  #else
    #define NXT_API __declspec(dllimport)
  #endif
#else
  #define NXT_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nxt_cache nxt_cache;
typedef int             nxt_result;

#define NXT_OK            0
#define NXT_ERR_INVALID   1   /* invalid argument or handle */
#define NXT_ERR_NOT_FOUND 2   /* requested entry doesn't exist */
#define NXT_ERR_DECODE    3   /* archive/file decoded but type-decode failed */
#define NXT_ERR_IO        4   /* sqlite or network I/O failure */
#define NXT_ERR_INTERNAL  5   /* unexpected exception */

/* ---- Lifecycle ---------------------------------------------------------- */

/* Open a cache backed by local sqlite jcache files in the given directory.
   Returns NULL on failure (call nxt_last_error). */
NXT_API nxt_cache *nxt_cache_open_local(const char *cache_path);

/* Open a cache backed entirely by the live Jagex JS5 servers.
   Performs jav_config.ws fetch + handshake before returning. */
NXT_API nxt_cache *nxt_cache_open_live(void);

/* Enable transparent live-JS5 fallback on a local cache. After this call,
   any read against the local sqlite that finds no blob will be fetched
   from the network. No-op for caches opened with nxt_cache_open_live. */
NXT_API nxt_result nxt_cache_enable_live_fallback(nxt_cache *cache);

/* Release a cache handle. Safe to pass NULL. */
NXT_API void nxt_cache_close(nxt_cache *cache);

/* Returns the last error message produced on this thread, or empty string
   if there has been none. Pointer is owned by the library — do not free. */
NXT_API const char *nxt_last_error(void);

/* ---- Memory ------------------------------------------------------------- */

/* Free a buffer returned by any getter (uint8_t* or char*). Safe to pass NULL. */
NXT_API void nxt_free(void *ptr);

/* ---- Raw archive access ------------------------------------------------- */

/* Read a single file from (index_id, archive_id, file_id) into a freshly
   allocated buffer. *out_data is owned by caller; release with nxt_free.
   Sets *out_size to the byte length. */
NXT_API nxt_result nxt_read_file_raw(nxt_cache *cache,
                                     int index_id, int archive_id, int file_id,
                                     uint8_t **out_data, size_t *out_size);

/* List every archive id present in an index. *out_ids is a freshly allocated
   array of *out_count ints (caller frees with nxt_free); *out_count may be 0
   (the pointer is still non-NULL and must be freed). For the map index (5)
   each id encodes a square: square_x = id & 0x7F, square_y = id >> 7. */
NXT_API nxt_result nxt_list_archive_ids(nxt_cache *cache, int index_id,
                                        int **out_ids, size_t *out_count);

/* ---- Map collision ------------------------------------------------------
 *
 * Decode the directional clip grid for one map square (cache index 5). The
 * square is addressed by its square coordinates (world tile / 64). The output
 * is a freshly allocated, packed [4][64][64] array of uint32 clip words:
 * plane-major, then x (west-east, 0..63), then y (south-north, 0..63).
 * *out_count is the word count (always 4*64*64 = 16384). The caller frees
 * *out_clip with nxt_free. When out_plane_mask is non-NULL it receives a mask
 * with bit p set if plane p holds any non-zero tile. Returns NXT_ERR_NOT_FOUND
 * if the square is absent from the cache. Clip flag bit meanings are documented
 * in maps/MapSquare.h (ClipFlag).
 */
NXT_API nxt_result nxt_get_mapsquare_clip(nxt_cache *cache, int square_x, int square_y,
                                          uint32_t **out_clip, size_t *out_count,
                                          uint8_t *out_plane_mask);

/* ---- Map crossings ------------------------------------------------------
 *
 * Interactable scenery crossings (doors, climb-overs, ladders/stairs, agility
 * shortcuts) found in one map square. Unlike the clip grid these carry the loc
 * id + geometry a navigation consumer needs to bake an interactable transition;
 * the CLIP_* bits alone cannot name the loc. Coordinates are absolute world
 * tiles. `option_index` is the 0-based right-click option slot to invoke
 * (0xFF = none). `climb_dir` is meaningful only for NXT_CROSSING_PLANE_CHANGE:
 * bit0 = climbs up, bit1 = climbs down.
 */

#define NXT_CROSSING_DOOR         0
#define NXT_CROSSING_CLIMBOVER    1
#define NXT_CROSSING_PLANE_CHANGE 2
#define NXT_CROSSING_AGILITY      3

#define NXT_CLIMB_UP   0x1
#define NXT_CLIMB_DOWN 0x2

typedef struct nxt_crossing
{
    int32_t  object_id;
    uint16_t world_x;
    uint16_t world_y;
    uint8_t  plane;
    uint8_t  shape;
    uint8_t  rotation;
    uint8_t  kind;          /* NXT_CROSSING_* */
    uint8_t  size_x;
    uint8_t  size_y;
    uint8_t  option_index;  /* 0-based option slot, 0xFF if none */
    uint8_t  climb_dir;     /* PLANE_CHANGE only: bit0 up, bit1 down */
} nxt_crossing;

/* Decode the interactable crossings for one map square (cache index 5). On
   success *out_crossings is a freshly allocated array of *out_count records
   (free with nxt_free); an empty square yields *out_crossings == NULL and
   *out_count == 0 with NXT_OK. Returns NXT_ERR_NOT_FOUND if the square is absent.
   Crossing kind/field meanings are documented above and mirror maps::Crossing. */
NXT_API nxt_result nxt_get_mapsquare_crossings(nxt_cache *cache, int square_x, int square_y,
                                               nxt_crossing **out_crossings, size_t *out_count);

/* ---- Config-type getters (single-entry JSON) ---------------------------
 *
 * Each function returns a NUL-terminated UTF-8 JSON document describing one
 * decoded config entry. The shape matches the nxtcache-dumper output. The
 * caller frees *out_json with nxt_free. *out_len is the byte length minus
 * the trailing NUL. Returns NXT_ERR_NOT_FOUND if the id doesn't resolve.
 */

NXT_API nxt_result nxt_get_npc_json     (nxt_cache *, int id, char **out_json, size_t *out_len);
NXT_API nxt_result nxt_get_item_json    (nxt_cache *, int id, char **out_json, size_t *out_len);
NXT_API nxt_result nxt_get_loc_json     (nxt_cache *, int id, char **out_json, size_t *out_len);
NXT_API nxt_result nxt_get_seq_json     (nxt_cache *, int id, char **out_json, size_t *out_len);
NXT_API nxt_result nxt_get_varbit_json  (nxt_cache *, int id, char **out_json, size_t *out_len);
NXT_API nxt_result nxt_get_enum_json    (nxt_cache *, int id, char **out_json, size_t *out_len);
NXT_API nxt_result nxt_get_struct_json  (nxt_cache *, int id, char **out_json, size_t *out_len);
NXT_API nxt_result nxt_get_inv_json     (nxt_cache *, int id, char **out_json, size_t *out_len);
NXT_API nxt_result nxt_get_param_json   (nxt_cache *, int id, char **out_json, size_t *out_len);
NXT_API nxt_result nxt_get_quest_json   (nxt_cache *, int id, char **out_json, size_t *out_len);
NXT_API nxt_result nxt_get_underlay_json(nxt_cache *, int id, char **out_json, size_t *out_len);
NXT_API nxt_result nxt_get_overlay_json (nxt_cache *, int id, char **out_json, size_t *out_len);
NXT_API nxt_result nxt_get_worldmap_json(nxt_cache *, int id, char **out_json, size_t *out_len);
NXT_API nxt_result nxt_get_dbrow_json   (nxt_cache *, int id, char **out_json, size_t *out_len);

/* Generic dispatch — useful for languages with reflection. Same return
   contract as the per-type getters. type_name is one of: "npc", "item",
   "loc", "seq", "varbit", "enum", "struct", "inv", "param", "quest",
   "underlay", "overlay", "worldmap", "dbrow". */
NXT_API nxt_result nxt_get_json(nxt_cache *cache, const char *type_name, int id,
                                char **out_json, size_t *out_len);

/* Bulk dump — returns a JSON array of all decoded entries of the given
   type. limit_or_neg1 caps the number of entries (use -1 for unbounded). */
NXT_API nxt_result nxt_dump_all_json(nxt_cache *cache, const char *type_name,
                                     int limit_or_neg1,
                                     char **out_json, size_t *out_len);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /* NXTCACHE_C_H */
