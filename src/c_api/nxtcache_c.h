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

/* Open a cache backed by the BETA Jagex JS5 endpoint
   (https://world1.runescape.com/jav_config_beta.ws?binaryType=3). */
NXT_API nxt_cache *nxt_cache_open_live_beta(void);

/* Enable transparent live-JS5 fallback on a local cache. After this call,
   any read against the local sqlite that finds no blob will be fetched
   from the network. No-op for caches opened with nxt_cache_open_live. */
NXT_API nxt_result nxt_cache_enable_live_fallback(nxt_cache *cache);

/* Same as nxt_cache_enable_live_fallback, but uses the BETA jav_config + JS5
   endpoint. Must be called BEFORE any read that needs the fallback. */
NXT_API nxt_result nxt_cache_enable_live_fallback_beta(nxt_cache *cache);

/* Release a cache handle. Safe to pass NULL. */
NXT_API void nxt_cache_close(nxt_cache *cache);

/* Returns the last error message produced on this thread, or empty string
   if there has been none. Pointer is owned by the library — do not free. */
NXT_API const char *nxt_last_error(void);

/* ---- Memory ------------------------------------------------------------- */

/* Free a buffer returned by any getter (uint8_t* or char*). Safe to pass NULL. */
NXT_API void nxt_free(void *ptr);

/* ---- Master reference table CRCs --------------------------------------- */

/* Connect to the live JS5 server, fetch the master reference table
   (file 255,255), and return the per-major CRC list in major-id order.
   *out_crcs is a freshly allocated array of *out_count uint32 values
   (caller frees with nxt_free). The CRCs are intended for the rs2client
   login packet (state 80's LM+24+106232..+106240 array). */
NXT_API nxt_result nxt_fetch_master_crcs(uint32_t **out_crcs, size_t *out_count);

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

/* ---- GameVals (JS5 index 67) -------------------------------------------
 *
 * Symbolic id<->name tables for scriptable types (loc, npc, obj, component,
 * var_player, ...), staged on the beta for Jagex's Lua plugin system. Each
 * index-67 archive is one type's whole table; the type name maps to a fixed
 * archive id (see config_types/GameVal.h). Names are stored UPPERCASE. These
 * require an index-67-bearing cache — open with nxt_cache_open_live_beta() or a
 * local/openrs2 beta cache. Full format: docs/gameval-index67.md.
 */

/* One group's table as JSON: { "type", "archive", "count", "entries": { id:name } }.
   group_name is a canonical name ("loc", "npc", "var_player", ...). Returns
   NXT_ERR_INVALID for an unknown name, NXT_ERR_NOT_FOUND if the group archive is
   absent. NUL-terminated UTF-8; free *out_json with nxt_free. */
NXT_API nxt_result nxt_get_gameval_group_json(nxt_cache *cache, const char *group_name,
                                              char **out_json, size_t *out_len);

/* Every gameval group in one document:
   { "index":67, "total":N, "groups": { "loc": {archive,count,entries}, ... } }.
   NUL-terminated UTF-8; free *out_json with nxt_free. */
NXT_API nxt_result nxt_dump_gamevals_json(nxt_cache *cache, char **out_json, size_t *out_len);

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

/* Decode one map square's clip grid AND its interactable crossings in a single
   pass. Both nxt_get_mapsquare_clip and nxt_get_mapsquare_crossings run the
   whole landscape decode internally, so a consumer that wants both (a
   navigation baker does) pays for that decode twice; this entry runs it once
   and publishes both results. Output and error conventions are exactly those of
   the two single-result entries: *out_clip is a freshly allocated packed
   [4][64][64] uint32 array (*out_clip_count == 4*64*64), *out_crossings is a
   freshly allocated array of *out_crossing_count records (NULL / 0 for a square
   with none), both freed with nxt_free, out_plane_mask is optional, and an
   absent square yields NXT_ERR_NOT_FOUND. On any failure no buffer is published
   and the caller has nothing to free. */
NXT_API nxt_result nxt_get_mapsquare_clip_and_crossings(nxt_cache *cache, int square_x, int square_y,
                                                       uint32_t **out_clip, size_t *out_clip_count,
                                                       uint8_t *out_plane_mask,
                                                       nxt_crossing **out_crossings,
                                                       size_t *out_crossing_count);

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

/* ---- Interface getter ---------------------------------------------------
 *
 * Decode one whole interface (cache index 3 archive = interface id, files =
 * components). Returns a JSON object: { "id": N, "components": { fileId: {..} } }.
 * Each component object carries: componentType, debugName, raw position/size,
 * options, event mask + scripts, and a type-specific block (sprite/button/
 * model/list/etc.) decoded by jag::Component::DecodeType + the 22 per-type
 * tails in rs2client.exe. NUL-terminated UTF-8; free *out_json with nxt_free.
 */
NXT_API nxt_result nxt_get_if_json      (nxt_cache *, int id, char **out_json, size_t *out_len);

/* ---- Sprites (JS5 index 8) ----------------------------------------------
 *
 * A sprite "group" (one archive id) holds one or more frames. The *_json getter
 * returns metadata only: { id, canvasWidth, canvasHeight, paletteCount,
 * frameCount, frames:[{offsetX,offsetY,width,height}] }. Actual pixels are
 * fetched per frame as RGBA8888 via nxt_get_sprite_frame_rgba.
 */
NXT_API nxt_result nxt_get_sprite_json(nxt_cache *, int id, char **out_json, size_t *out_len);

/* Quick header info for a sprite group. Any out-param may be NULL. Returns
   NXT_ERR_NOT_FOUND if the group id is absent. */
NXT_API nxt_result nxt_get_sprite_info(nxt_cache *cache, int id,
                                       int32_t *out_frame_count, int32_t *out_canvas_w,
                                       int32_t *out_canvas_h, int32_t *out_palette_count);

/* Decode one frame of a sprite group into a freshly allocated RGBA8888 buffer
   (R,G,B,A bytes, row-major, top-left origin). *out_count is the byte length
   (width*height*4); free *out_rgba with nxt_free. The geometry out-params may be
   NULL. Returns NXT_ERR_NOT_FOUND if the group is absent, NXT_ERR_INVALID if
   frame_index is out of range. */
NXT_API nxt_result nxt_get_sprite_frame_rgba(nxt_cache *cache, int id, int frame_index,
                                             uint8_t **out_rgba, size_t *out_count,
                                             int32_t *out_width, int32_t *out_height,
                                             int32_t *out_offset_x, int32_t *out_offset_y);

/* ---- Models (JS5 index 47) ----------------------------------------------
 *
 * RS3 "NXT" GPU-mesh models (version 5 "meshdata" layout). The *_json getter
 * returns metadata only ({ id, format, version, meshCount, vertexCount,
 * faceCount, hasSkins, hasColors, materialArgs, bbox }). The geometry getters
 * expose one shared vertex pool (positions/normals/UVs/colours) plus a flat
 * triangle list; face indices reference the shared pool directly. All arrays
 * are freshly allocated; free with nxt_free.
 *
 * NOTE: index-47 model groups are LZMA-compressed in the cache. If this build's
 * decompression layer lacks LZMA support these getters return NXT_ERR_DECODE.
 */

typedef struct nxt_model_info
{
    int32_t format;
    int32_t version;
    int32_t mesh_count;          /* render submeshes */
    int32_t vertex_count;        /* shared vertex pool size */
    int32_t face_count;          /* total triangles across submeshes */
    int32_t min_x, max_x, min_y, max_y, min_z, max_z;
    uint8_t has_skins;
    uint8_t has_colors;
    uint8_t _pad[2];
} nxt_model_info;

typedef struct nxt_model_face
{
    int32_t a, b, c;        /* vertex indices into the shared pool */
    int32_t material;       /* render material arg (0 = none; → JS5 index 26) */
} nxt_model_face;

NXT_API nxt_result nxt_get_model_json(nxt_cache *, int id, char **out_json, size_t *out_len);

/* Fixed-size header/summary; out_info must be non-NULL. */
NXT_API nxt_result nxt_get_model_info(nxt_cache *cache, int id, nxt_model_info *out_info);

/* Interleaved x,y,z int32 positions; *out_count == vertex_count*3. */
NXT_API nxt_result nxt_get_model_vertices(nxt_cache *cache, int id,
                                          int32_t **out_xyz, size_t *out_count);

/* Interleaved x,y,z int8 vertex normals; *out_count == vertex_count*3. */
NXT_API nxt_result nxt_get_model_normals(nxt_cache *cache, int id,
                                         int8_t **out_xyz, size_t *out_count);

/* Interleaved u,v float per vertex; *out_count == vertex_count*2. */
NXT_API nxt_result nxt_get_model_uvs(nxt_cache *cache, int id,
                                     float **out_uv, size_t *out_count);

/* Per-vertex colour packed as (raw16 << 8) | alpha8; *out_count == vertex_count.
   Empty (count 0) if the model carries no per-vertex colours. */
NXT_API nxt_result nxt_get_model_colors(nxt_cache *cache, int id,
                                        uint32_t **out_colors, size_t *out_count);

/* Triangle list; *out_count == face_count. */
NXT_API nxt_result nxt_get_model_faces(nxt_cache *cache, int id,
                                       nxt_model_face **out_faces, size_t *out_count);

/* ---- Inventory icon rendering ------------------------------------------
 *
 * Software-render an item's inventory/container icon (the picture shown in a
 * bank/inventory slot) into an RGBA8888 buffer (R,G,B,A, row-major, top-left
 * origin, *out_count == width*height*4; free with nxt_free). The item's
 * inventory model is resolved (base model only — noted/placeholder/stacked
 * variants are NOT combined), recoloured (op40) and lit per the item's 2D
 * parameters (zoom/rotation/offset/ambient/contrast), then rasterised.
 *
 * Fidelity: the real client renders these on the GPU (deferred G-buffer + SSAO
 * + PBR). This CPU renderer targets a *recognisable* icon — correct model,
 * orientation, Jagex-HSL colours and approximate diffuse shading — not a
 * pixel-exact match; framing is auto-fit to the requested size. Transparent
 * background pixels have alpha 0.
 *
 * width/height are the output dimensions (e.g. 36x32 for a slot-sized icon).
 * supersample is the SSAA factor for edge quality (1..8; pass 0 for the
 * default of 4). Returns NXT_ERR_NOT_FOUND if the item or its model is absent. */
NXT_API nxt_result nxt_render_item_icon(nxt_cache *cache, int id,
                                        int width, int height, int supersample,
                                        uint8_t **out_rgba, size_t *out_count);

/* Generic dispatch — useful for languages with reflection. Same return
   contract as the per-type getters. type_name is one of: "npc", "item",
   "loc", "seq", "varbit", "enum", "struct", "inv", "param", "quest",
   "underlay", "overlay", "worldmap", "dbrow", "sprite", "model". */
NXT_API nxt_result nxt_get_json(nxt_cache *cache, const char *type_name, int id,
                                char **out_json, size_t *out_len);

/* Bulk dump — returns a JSON array of all decoded entries of the given
   type. limit_or_neg1 caps the number of entries (use -1 for unbounded). */
NXT_API nxt_result nxt_dump_all_json(nxt_cache *cache, const char *type_name,
                                     int limit_or_neg1,
                                     char **out_json, size_t *out_len);

/* Cheap id enumeration — list every entry id of a config type WITHOUT decoding
   the entries. This is the inexpensive companion to nxt_dump_all_json: ids come
   straight from the index reference tables, so no archive data blob is fetched
   or decompressed. *out_ids is a freshly allocated, ascending array of
   *out_count ints (caller frees with nxt_free); *out_count may be 0 (the pointer
   is still non-NULL and must be freed).

   type_name is any name nxt_get_json accepts ("npc", "item", "loc", "seq",
   "varbit", "enum", "struct", "inv", "param", "quest", "underlay", "overlay",
   "worldmap", "dbrow", "model", "sprite", "if"). For "if" each id is an
   interface (archive) id. Returns NXT_ERR_INVALID for an unknown type_name. */
NXT_API nxt_result nxt_list_type_ids(nxt_cache *cache, const char *type_name,
                                     int **out_ids, size_t *out_count);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /* NXTCACHE_C_H */
