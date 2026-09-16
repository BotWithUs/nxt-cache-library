# NXTCacheLibrary

A C++20 library for reading the RuneScape **NXT** game cache — the
sqlite-backed `js5-*.jcache` files the NXT client stores on disk, and the live
JS5 binary protocol the client uses to fetch them.

It decodes the cache's config types into plain structs (and JSON), exposes a
flat C ABI so non-C++ hosts can consume it, and ships two command-line tools
built on top.

## What it does

- **Read a local cache** — open a directory of `js5-*.jcache` files and pull
  archives, files, and decoded config types out of it.
- **Read the live cache** — fetch `jav_config`, run the JS5 handshake, and
  stream archives straight from Jagex's content servers. Usable standalone, or
  as a transparent fallback for archives missing from a local cache.
- **Decode config types** — npc, item, loc, seq, varbit, enum, struct, inv,
  param, quest, underlay, overlay, worldmap, dbrow, interfaces, sprites,
  models, gamevals, and map/loc spawns.
- **Build map-square clip grids** — directional per-tile collision flags plus
  the interactable scenery crossings that sit alongside them, for pathfinding
  consumers.
- **Render item icons** — rasterise an item's inventory model to RGBA/BMP with
  supersampling and optional background compositing.

## Layout

| Path | What lives there |
|---|---|
| `src/core/` | Cache/archive/index primitives, buffer, container decode |
| `src/network/` | jav_config fetch, JS5 socket protocol, compression |
| `src/config_types/` | Per-type decoders (item, npc, loc, interfaces, ...) |
| `src/maps/` | Map-square clip grids, loc spawns, crossings |
| `src/render/` | Item icon rasteriser |
| `src/c_api/` | Flat C ABI (`nxtcache_c.h`) + tests |
| `src/dumper/` | `nxtcache-dumper` CLI |
| `src/mirror/` | `nxtcache-mirror` CLI |
| `src/gzip/` | Vendored mapbox/gzip-hpp (see THIRD_PARTY_NOTICES.md) |

## Building

Requires CMake >= 3.15 and a C++20 compiler. Dependencies (sqlite3, zlib,
bzip2, liblzma, curl) are fetched automatically by vcpkg at configure time;
nlohmann/json comes in via FetchContent. No manual dependency setup.

```sh
cmake -B build
cmake --build build --config Release
```

The build produces `NXTCache` (shared library) plus `nxtcache-dumper` and
`nxtcache-mirror`. Turn the tools off with `-DNXTCACHE_BUILD_DUMPER=OFF` /
`-DNXTCACHE_BUILD_MIRROR=OFF`.

On Windows the static-md vcpkg triplet is used so static dependencies are baked
into `NXTCache.dll` while the dynamic CRT is kept — required for loading the
library from a JVM without CRT heap-ownership conflicts.

## Tools

### `nxtcache-dumper`

Decodes cache types to JSON on stdout or to a file.

```sh
# every item in a local cache, pretty-printed
nxtcache-dumper --type item --cache /path/to/jcache --pretty

# one npc, straight from the live servers
nxtcache-dumper --type npc --id 1 --source live

# one interface JSON file per archive (streams; required for bulk dumps)
nxtcache-dumper --type if --cache /path/to/jcache --out-dir ./interfaces

# render an item's inventory icon to a BMP
nxtcache-dumper --type itemicon --id 4151 --cache /path/to/jcache \
                --width 64 --height 64 --out whip.bmp
```

`--help` lists every type and option.

### `nxtcache-mirror`

Pulls the full cache over the JS5 protocol into local `js5-<N>.jcache` sqlite
files. Every blob is stored as the **raw JS5 container exactly as it arrived** —
never decompressed or re-packed — so the result reproduces the source
byte-for-byte. Resumable (archives already present are skipped), retries through
network hiccups, and commits in per-index transactions.

```sh
nxtcache-mirror --out ./cache
nxtcache-mirror --out ./cache --indices 5,255
```

## C ABI

`src/c_api/nxtcache_c.h` is a flat C interface for consumers that can't link
C++ (JVM/.NET/FFI hosts). Open a cache, call getters, free what you get back:

```c
nxt_cache *cache = nxt_cache_open_local("/path/to/jcache");
if (!cache) { fprintf(stderr, "%s\n", nxt_last_error()); return 1; }

char  *json = NULL;
size_t len  = 0;
if (nxt_get_json(cache, "item", 4151, &json, &len) == NXT_OK) {
    printf("%.*s\n", (int)len, json);
    nxt_free(json);
}
nxt_cache_close(cache);
```

Every entry point returns an `nxt_result`; on failure `nxt_last_error()` carries
the message. Anything handed back through an out-parameter is owned by the
caller and must be released with `nxt_free`.

## License

MIT — see [LICENSE](LICENSE).

Third-party components are listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

This project is an independent, clean-room effort for reading cache files the
NXT client has already downloaded. It is not affiliated with, endorsed by, or
sponsored by Jagex Ltd. RuneScape is a trademark of Jagex Ltd. No game assets
are distributed in this repository.
