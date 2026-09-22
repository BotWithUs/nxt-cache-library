#define NXTCACHE_BUILDING

#include "c_api/nxtcache_c.h"

#include "config_types/GameVal.h"
#include "config_types/InterfaceTypes.h"
#include "config_types/ModelType.h"
#include "config_types/SpriteType.h"
#include "config_types/Types.h"
#include "config_types/VarPlayerType.h"
#include "core/Archive.h"
#include "core/CacheSource.h"
#include "core/DbRowProvider.h"
#include "core/RSCache.h"
#include "core/TypeMappings.h"
#include "dumper/Json.h"
#include "maps/MapSquare.h"
#include "network/Js5Cache.h"
#include "network/Js5Compression.h"
#include "network/Js5Config.h"
#include "network/Js5Index.h"
#include "network/Js5Socket.h"
#include "render/IconRenderer.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

using json = nlohmann::json;

struct Cache
{
    std::unique_ptr<CacheSource> source;
    RSCache *local{nullptr};   // non-owning, only set if `source` is RSCache
    std::unique_ptr<DbRowProvider> dbProvider;
    std::unique_ptr<maps::DefCache> mapDefs;   // memoizes loc/overlay defs across clip calls
};

thread_local std::string g_lastError;

void setError(const char *msg) { g_lastError = msg ? msg : ""; }
void setError(const std::string &msg) { g_lastError = msg; }

char *dupString(const std::string &s)
{
    auto *buf = static_cast<char *>(std::malloc(s.size() + 1));
    if (!buf) return nullptr;
    std::memcpy(buf, s.data(), s.size());
    buf[s.size()] = '\0';
    return buf;
}

// Look up the file matching `id` for the given mapping; return null if missing.
// Uses CacheSource virtuals so it works for both local and live caches.
template <typename T>
std::optional<T> decodeOne(CacheSource &cs, const nxt::TypeMapping &m, int id)
{
    int aid = m.archiveId;
    int file = id;
    if (aid == -1)
    {
        aid = id >> m.shift;
        file = id & ((1 << m.shift) - 1);
    }
    auto &archive = cs.archive(m.indexId, aid);
    if (archive.id == -1) return std::nullopt;
    auto buffer = archive.readFile(file);
    if (buffer.buffer == nullptr || buffer.remaining() == 0) return std::nullopt;
    T type{};
    type.id = id;
    type.decode(buffer);
    return type;
}

// Decode a single entry by its registered type name (resolves the TypeMapping).
template <typename T>
std::optional<T> decodeByName(CacheSource &cs, const char *name, int id)
{
    const auto &defs = nxt::typeDefaults();
    auto it = defs.find(name);
    if (it == defs.end()) return std::nullopt;
    return decodeOne<T>(cs, it->second, id);
}

template <typename T>
nxt_result getJsonGeneric(nxt_cache *handle, const std::string &typeName, int id,
                          char **out_json, size_t *out_len)
{
    if (!handle || !out_json || !out_len)
    {
        setError("invalid argument: null handle or out parameter");
        return NXT_ERR_INVALID;
    }
    auto *c = reinterpret_cast<Cache *>(handle);
    const auto &defs = nxt::typeDefaults();
    auto it = defs.find(typeName);
    if (it == defs.end())
    {
        setError("unknown type: " + typeName);
        return NXT_ERR_INVALID;
    }
    try
    {
        auto entry = decodeOne<T>(*c->source, it->second, id);
        if (!entry)
        {
            setError(typeName + " " + std::to_string(id) + " not found");
            return NXT_ERR_NOT_FOUND;
        }
        json doc = nxtdump::toJson(*entry);
        std::string s = doc.dump();
        char *buf = dupString(s);
        if (!buf)
        {
            setError("malloc failed");
            return NXT_ERR_INTERNAL;
        }
        *out_json = buf;
        *out_len = s.size();
        return NXT_OK;
    }
    catch (const std::exception &e)
    {
        setError(std::string("decode failed: ") + e.what());
        return NXT_ERR_DECODE;
    }
}

// dbrow goes through DbRowProvider rather than the generic shard loader.
nxt_result getDbRowJson(nxt_cache *handle, int id, char **out_json, size_t *out_len)
{
    if (!handle || !out_json || !out_len)
    {
        setError("invalid argument");
        return NXT_ERR_INVALID;
    }
    auto *c = reinterpret_cast<Cache *>(handle);
    try
    {
        if (!c->dbProvider)
        {
            c->dbProvider = std::make_unique<DbRowProvider>(c->source.get());
            c->dbProvider->load();
        }
        DbRowType *row = c->dbProvider->get(id);
        if (!row || row->id == -1)
        {
            setError("dbrow " + std::to_string(id) + " not found");
            return NXT_ERR_NOT_FOUND;
        }
        json doc = nxtdump::toJson(*row);
        std::string s = doc.dump();
        char *buf = dupString(s);
        if (!buf)
        {
            setError("malloc failed");
            return NXT_ERR_INTERNAL;
        }
        *out_json = buf;
        *out_len = s.size();
        return NXT_OK;
    }
    catch (const std::exception &e)
    {
        setError(std::string("dbrow decode failed: ") + e.what());
        return NXT_ERR_DECODE;
    }
}

// Iterate every archive/file for the given type, accumulate JSON entries.
template <typename T>
json dumpAll(CacheSource &cs, const nxt::TypeMapping &m, int limit)
{
    json out = json::array();
    auto wantMore = [&]() { return limit < 0 || static_cast<int>(out.size()) < limit; };

    if (m.archiveId == -1)
    {
        std::vector<int> aids = cs.archiveIds(m.indexId);
        for (int aid : aids)
        {
            if (!wantMore()) break;
            auto &archive = cs.archive(m.indexId, aid);
            if (archive.id == -1) continue;
            for (auto &[fid, fh] : archive.files)
            {
                if (!wantMore()) break;
                int actualId = (aid << m.shift) | fid;
                auto buffer = archive.readFile(fid);
                if (buffer.buffer == nullptr || buffer.remaining() == 0) continue;
                T type{};
                type.id = actualId;
                type.decode(buffer);
                out.push_back(nxtdump::toJson(type));
            }
        }
    }
    else
    {
        auto &archive = cs.archive(m.indexId, m.archiveId);
        if (archive.id != -1)
        {
            for (auto &[fid, fh] : archive.files)
            {
                if (!wantMore()) break;
                auto buffer = archive.readFile(fid);
                if (buffer.buffer == nullptr || buffer.remaining() == 0) continue;
                T type{};
                type.id = fid;
                type.decode(buffer);
                out.push_back(nxtdump::toJson(type));
            }
        }
    }
    return out;
}

// Enumerate every entry id of a config type from the reference tables alone
// (no archive blob decode). Sharded layouts combine (archiveId, fileId) into the
// entry id; single-archive layouts use the file ids directly; interfaces key one
// archive per id. Result is ascending.
std::vector<int> enumerateTypeIds(CacheSource &cs, const std::string &typeName,
                                  const nxt::TypeMapping &m)
{
    std::vector<int> ids;
    if (typeName == "if")
    {
        // One archive == one interface id; files are components, not entries.
        ids = cs.archiveIds(m.indexId);
    }
    else if (m.archiveId == -1)
    {
        for (int aid : cs.archiveIds(m.indexId))
        {
            for (int fid : cs.fileIds(m.indexId, aid))
            {
                ids.push_back((aid << m.shift) | fid);
            }
        }
    }
    else
    {
        ids = cs.fileIds(m.indexId, m.archiveId);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

// Build the {archive,count,entries} JSON object for one gameval group (index-67
// archive). Returns null json if the group's archive or single file is absent.
// Shared by the single-group and dump-all C-ABI getters.
json buildGameValGroup(CacheSource &cs, int archiveId)
{
    auto &archive = cs.archive(nxt::kGameValIndex, archiveId);
    if (archive.id == -1)
    {
        return nullptr;
    }
    auto buffer = archive.readFile(0);
    if (buffer.buffer == nullptr || buffer.remaining() == 0)
    {
        return nullptr;
    }
    auto entries = nxt::decodeGameVals(buffer);
    json e = json::object();
    for (const auto &en : entries)
    {
        e[std::to_string(en.id)] = en.name;
    }
    return json{
        {"archive", archiveId},
        {"count", static_cast<int>(entries.size())},
        {"entries", std::move(e)},
    };
}

enum class VarpLookup
{
    Found,
    NotFound,
    IoError,
    DecodeError,
};

// Resolve one varp against config group 60. Distinguishes "group unreadable"
// (IoError: nothing is known about the id) from "id not in the group's file
// table" (NotFound) and "present but malformed" (DecodeError). Membership is
// checked on fileIds BEFORE readFile, because Archive::readFile inserts a
// default FileHeader for an unknown id. outType may be null (presence only).
VarpLookup lookupVarp(CacheSource &cs, int id, VarPlayerType *outType, std::string &outError)
{
    const nxt::TypeMapping &m = nxt::typeDefaults().at("varp");
    Archive &archive = cs.archive(m.indexId, m.archiveId);
    if (archive.id == -1 || !archive.loaded)
    {
        outError = "varp group (index " + std::to_string(m.indexId) + ", group " +
                   std::to_string(m.archiveId) + ") could not be read";
        return VarpLookup::IoError;
    }
    if (id < 0 || archive.fileIds.find(id) == archive.fileIds.end())
    {
        outError = "varp " + std::to_string(id) + " does not exist";
        return VarpLookup::NotFound;
    }
    if (outType == nullptr)
    {
        return VarpLookup::Found;
    }
    RSBuffer buffer = archive.readFile(id);
    VarPlayerType type{};
    type.id = id;
    const auto *bytes = reinterpret_cast<const uint8_t *>(buffer.buffer);
    if (bytes == nullptr || !type.decodeStrict(bytes + buffer.readPosition, buffer.remaining()))
    {
        outError = "varp " + std::to_string(id) + " exists but failed to decode";
        return VarpLookup::DecodeError;
    }
    *outType = type;
    return VarpLookup::Found;
}

nxt_result varpLookupResult(VarpLookup lookup)
{
    switch (lookup)
    {
        case VarpLookup::Found:
            return NXT_OK;
        case VarpLookup::NotFound:
            return NXT_ERR_NOT_FOUND;
        case VarpLookup::IoError:
            return NXT_ERR_IO;
        case VarpLookup::DecodeError:
            return NXT_ERR_DECODE;
    }
    return NXT_ERR_INTERNAL;
}

int32_t varpBaseType(const VarpDefault &def)
{
    if (!def.hasBaseType)
    {
        return NXT_VAR_BASE_UNKNOWN;
    }
    switch (def.baseType)
    {
        case BaseVarType::INTEGER:
            return NXT_VAR_BASE_INTEGER;
        case BaseVarType::LONG:
            return NXT_VAR_BASE_LONG;
        case BaseVarType::STRING:
            return NXT_VAR_BASE_STRING;
        case BaseVarType::COORDFINE:
            return NXT_VAR_BASE_COORDFINE;
    }
    return NXT_VAR_BASE_UNKNOWN;
}

int32_t varpDefaultRule(VarpDefaultRule rule)
{
    switch (rule)
    {
        case VarpDefaultRule::None:
            return NXT_VARP_DEFAULT_NONE;
        case VarpDefaultRule::Type:
            return NXT_VARP_DEFAULT_TYPE;
        case VarpDefaultRule::Domain:
            return NXT_VARP_DEFAULT_DOMAIN;
    }
    return NXT_VARP_DEFAULT_NONE;
}

nxt_varp_info toVarpInfo(const VarPlayerType &type)
{
    const VarpDefault def = type.resolveDefault();
    nxt_varp_info info{};
    info.struct_size = static_cast<uint32_t>(sizeof(nxt_varp_info));
    info.version = NXT_VARP_INFO_VERSION;
    info.id = type.id;
    info.type_id = type.typeId;
    info.base_type = varpBaseType(def);
    info.default_rule = varpDefaultRule(def.rule);
    info.default_value = def.value;
    info.op7_absent = type.flagOp7 ? 1 : 0;
    info.op8_present = type.flagOp8 ? 1 : 0;
    info.has_op4 = type.hasOp4 ? 1 : 0;
    info.op4 = type.op4;
    info.has_op5 = type.hasOp5 ? 1 : 0;
    info.op5 = type.op5;
    info.op110 = type.op110;
    return info;
}

// nxt_varp_info layout pins live in nxtcache_c.h (NXT_VARP_LAYOUT_ASSERT).

}  // namespace

extern "C" {

const char *nxt_last_error(void)
{
    return g_lastError.c_str();
}

void nxt_free(void *ptr)
{
    std::free(ptr);
}

nxt_result nxt_fetch_master_crcs(uint32_t **out_crcs, size_t *out_count)
{
    if (!out_crcs || !out_count)
    {
        setError("nxt_fetch_master_crcs: null output");
        return NXT_ERR_INVALID;
    }
    *out_crcs  = nullptr;
    *out_count = 0;
    try
    {
        // The master index (file 255,255) doesn't share the format of a
        // regular reference table — so we don't use Js5Index here. Instead
        // we fetch (255,255) raw, decompress, and walk the master format:
        // <count u32 BE>? then per-entry <crc u32 BE><version u32 BE> ...
        // The format varies — fall back to "the bytes ARE a u32 CRC array
        // alternating with versions" if there's no count header.
        js5::ServerConfig config = js5::fetchServerConfig();
        js5::Js5Socket socket(config);
        auto raw = socket.getFile(255, 255);
        auto bytes = js5::decompress(raw.data(), raw.size());

        // NXT master index format (from OpenNXT's ChecksumTable.decode):
        //   byte count
        //   for each i in 0..count-1:
        //     u32 BE crc
        //     u32 BE version
        //     u32 BE files
        //     u32 BE size
        //     byte[64] whirlpool
        // → 1 + count * 80 bytes (any trailing bytes are RSA-signed hash + slop)
        if (bytes.empty())
        {
            setError("nxt_fetch_master_crcs: empty master index");
            return NXT_ERR_DECODE;
        }
        constexpr std::size_t entrySize = 4 + 4 + 4 + 4 + 64;
        const std::size_t count = static_cast<std::size_t>(bytes[0]);
        if (1 + count * entrySize > bytes.size())
        {
            setError("nxt_fetch_master_crcs: master count=" +
                     std::to_string(count) + " but only " +
                     std::to_string(bytes.size()) + " bytes available");
            return NXT_ERR_DECODE;
        }
        auto *buf = static_cast<uint32_t *>(std::malloc(count * sizeof(uint32_t)));
        if (!buf)
        {
            setError("nxt_fetch_master_crcs: out of memory");
            return NXT_ERR_INTERNAL;
        }
        for (std::size_t i = 0; i < count; ++i)
        {
            const std::uint8_t *p = bytes.data() + 1 + i * entrySize;
            buf[i] = (std::uint32_t(p[0]) << 24) |
                     (std::uint32_t(p[1]) << 16) |
                     (std::uint32_t(p[2]) <<  8) |
                      std::uint32_t(p[3]);
        }
        *out_crcs  = buf;
        *out_count = count;
        return NXT_OK;
    }
    catch (const std::exception &e)
    {
        setError(std::string("nxt_fetch_master_crcs: ") + e.what());
        return NXT_ERR_IO;
    }
}

nxt_cache *nxt_cache_open_local(const char *cache_path)
{
    if (!cache_path)
    {
        setError("nxt_cache_open_local: null path");
        return nullptr;
    }
    try
    {
        auto wrap = std::make_unique<Cache>();
        auto local = std::make_unique<RSCache>(std::string(cache_path));
        wrap->local = local.get();
        wrap->source = std::move(local);
        return reinterpret_cast<nxt_cache *>(wrap.release());
    }
    catch (const std::exception &e)
    {
        setError(std::string("open_local failed: ") + e.what());
        return nullptr;
    }
}

nxt_cache *nxt_cache_open_live(void)
{
    try
    {
        auto wrap = std::make_unique<Cache>();
        wrap->source = std::make_unique<js5::Js5Cache>(/*beta=*/false);
        return reinterpret_cast<nxt_cache *>(wrap.release());
    }
    catch (const std::exception &e)
    {
        setError(std::string("open_live failed: ") + e.what());
        return nullptr;
    }
}

nxt_cache *nxt_cache_open_live_beta(void)
{
    try
    {
        auto wrap = std::make_unique<Cache>();
        wrap->source = std::make_unique<js5::Js5Cache>(/*beta=*/true);
        return reinterpret_cast<nxt_cache *>(wrap.release());
    }
    catch (const std::exception &e)
    {
        setError(std::string("open_live_beta failed: ") + e.what());
        return nullptr;
    }
}

namespace {

nxt_result enableLiveFallbackImpl(nxt_cache *handle, bool beta)
{
    if (!handle)
    {
        setError("null handle");
        return NXT_ERR_INVALID;
    }
    auto *c = reinterpret_cast<Cache *>(handle);
    if (!c->local)
    {
        // Already a live cache — fallback is a no-op.
        return NXT_OK;
    }
    try
    {
        c->local->enableLiveFallback(beta);
        return NXT_OK;
    }
    catch (const std::exception &e)
    {
        setError(std::string("enable_live_fallback failed: ") + e.what());
        return NXT_ERR_IO;
    }
}

}  // namespace

nxt_result nxt_cache_enable_live_fallback(nxt_cache *handle)
{
    return enableLiveFallbackImpl(handle, /*beta=*/false);
}

nxt_result nxt_cache_enable_live_fallback_beta(nxt_cache *handle)
{
    return enableLiveFallbackImpl(handle, /*beta=*/true);
}

void nxt_cache_close(nxt_cache *handle)
{
    if (!handle) return;
    auto *c = reinterpret_cast<Cache *>(handle);
    delete c;
}

nxt_result nxt_read_file_raw(nxt_cache *handle,
                             int index_id, int archive_id, int file_id,
                             uint8_t **out_data, size_t *out_size)
{
    if (!handle || !out_data || !out_size)
    {
        setError("invalid argument");
        return NXT_ERR_INVALID;
    }
    auto *c = reinterpret_cast<Cache *>(handle);
    try
    {
        auto &archive = c->source->archive(index_id, archive_id);
        if (archive.id == -1)
        {
            setError("archive not found");
            return NXT_ERR_NOT_FOUND;
        }
        auto buffer = archive.readFile(file_id);
        if (buffer.buffer == nullptr || buffer.remaining() == 0)
        {
            setError("file not found");
            return NXT_ERR_NOT_FOUND;
        }
        size_t n = buffer.remaining();
        auto *buf = static_cast<uint8_t *>(std::malloc(n));
        if (!buf)
        {
            setError("malloc failed");
            return NXT_ERR_INTERNAL;
        }
        std::memcpy(buf, buffer.buffer + buffer.readPosition, n);
        *out_data = buf;
        *out_size = n;
        return NXT_OK;
    }
    catch (const std::exception &e)
    {
        setError(std::string("read_file_raw failed: ") + e.what());
        return NXT_ERR_IO;
    }
}

nxt_result nxt_list_archive_ids(nxt_cache *handle, int index_id,
                                int **out_ids, size_t *out_count)
{
    if (!handle || !out_ids || !out_count)
    {
        setError("invalid argument: null handle or out parameter");
        return NXT_ERR_INVALID;
    }
    auto *c = reinterpret_cast<Cache *>(handle);
    try
    {
        std::vector<int> ids = c->source->archiveIds(index_id);
        size_t n = ids.size();
        // malloc(0) may return NULL; allocate at least one slot so the caller
        // always receives a freeable, non-NULL pointer.
        auto *buf = static_cast<int *>(std::malloc((n != 0 ? n : 1) * sizeof(int)));
        if (!buf)
        {
            setError("malloc failed");
            return NXT_ERR_INTERNAL;
        }
        if (n != 0)
        {
            std::memcpy(buf, ids.data(), n * sizeof(int));
        }
        *out_ids = buf;
        *out_count = n;
        return NXT_OK;
    }
    catch (const std::exception &e)
    {
        setError(std::string("list_archive_ids failed: ") + e.what());
        return NXT_ERR_IO;
    }
}

nxt_result nxt_get_gameval_group_json(nxt_cache *handle, const char *group_name,
                                      char **out_json, size_t *out_len)
{
    if (!handle || !group_name || !out_json || !out_len)
    {
        setError("invalid argument");
        return NXT_ERR_INVALID;
    }
    int aid = nxt::gameValGroupArchive(group_name);
    if (aid < 0)
    {
        setError(std::string("unknown gameval group: ") + group_name);
        return NXT_ERR_INVALID;
    }
    auto *c = reinterpret_cast<Cache *>(handle);
    try
    {
        json group = buildGameValGroup(*c->source, aid);
        if (group.is_null())
        {
            setError(std::string("gameval group not found: ") + group_name);
            return NXT_ERR_NOT_FOUND;
        }
        group["type"] = group_name;
        std::string s = group.dump();
        char *buf = dupString(s);
        if (!buf)
        {
            setError("malloc failed");
            return NXT_ERR_INTERNAL;
        }
        *out_json = buf;
        *out_len = s.size();
        return NXT_OK;
    }
    catch (const std::exception &ex)
    {
        setError(std::string("gameval decode failed: ") + ex.what());
        return NXT_ERR_DECODE;
    }
}

nxt_result nxt_dump_gamevals_json(nxt_cache *handle, char **out_json, size_t *out_len)
{
    if (!handle || !out_json || !out_len)
    {
        setError("invalid argument");
        return NXT_ERR_INVALID;
    }
    auto *c = reinterpret_cast<Cache *>(handle);
    try
    {
        json groups = json::object();
        long long total = 0;
        for (const auto &[aid, name] : nxt::gameValGroups())
        {
            json group = buildGameValGroup(*c->source, aid);
            if (group.is_null())
            {
                continue;
            }
            total += group.value("count", 0);
            groups[name] = std::move(group);
        }
        json doc = {
            {"index", nxt::kGameValIndex},
            {"total", total},
            {"groups", std::move(groups)},
        };
        std::string s = doc.dump();
        char *buf = dupString(s);
        if (!buf)
        {
            setError("malloc failed");
            return NXT_ERR_INTERNAL;
        }
        *out_json = buf;
        *out_len = s.size();
        return NXT_OK;
    }
    catch (const std::exception &ex)
    {
        setError(std::string("gameval dump failed: ") + ex.what());
        return NXT_ERR_DECODE;
    }
}

nxt_result nxt_get_mapsquare_clip(nxt_cache *handle, int square_x, int square_y,
                                  uint32_t **out_clip, size_t *out_count,
                                  uint8_t *out_plane_mask)
{
    if (!handle || !out_clip || !out_count)
    {
        setError("invalid argument: null handle or out parameter");
        return NXT_ERR_INVALID;
    }
    auto *c = reinterpret_cast<Cache *>(handle);
    try
    {
        if (!c->mapDefs)
        {
            c->mapDefs = std::make_unique<maps::DefCache>();
        }
        maps::MapSquareClip clip;
        if (!maps::buildMapSquareClip(*c->source, square_x, square_y, *c->mapDefs, clip))
        {
            setError("map square " + std::to_string(square_x) + "," + std::to_string(square_y)
                     + " not present");
            return NXT_ERR_NOT_FOUND;
        }
        const size_t words = static_cast<size_t>(maps::MapSquareClip::PLANES)
                             * maps::MapSquareClip::SIZE * maps::MapSquareClip::SIZE;
        auto *buf = static_cast<uint32_t *>(std::malloc(words * sizeof(uint32_t)));
        if (!buf)
        {
            setError("malloc failed");
            return NXT_ERR_INTERNAL;
        }
        std::memcpy(buf, clip.flags, words * sizeof(uint32_t));
        *out_clip = buf;
        *out_count = words;
        if (out_plane_mask)
        {
            *out_plane_mask = clip.planeMask;
        }
        return NXT_OK;
    }
    catch (const std::exception &e)
    {
        setError(std::string("get_mapsquare_clip failed: ") + e.what());
        return NXT_ERR_DECODE;
    }
}

static_assert(sizeof(nxt_crossing) == sizeof(maps::Crossing),
              "nxt_crossing must alias maps::Crossing byte-for-byte");

nxt_result nxt_get_mapsquare_crossings(nxt_cache *handle, int square_x, int square_y,
                                       nxt_crossing **out_crossings, size_t *out_count)
{
    if (!handle || !out_crossings || !out_count)
    {
        setError("invalid argument: null handle or out parameter");
        return NXT_ERR_INVALID;
    }
    *out_crossings = nullptr;
    *out_count = 0;
    auto *c = reinterpret_cast<Cache *>(handle);
    try
    {
        if (!c->mapDefs)
        {
            c->mapDefs = std::make_unique<maps::DefCache>();
        }
        maps::MapSquareClip clip;
        std::vector<maps::Crossing> crossings;
        if (!maps::buildMapSquareClip(*c->source, square_x, square_y, *c->mapDefs, clip, &crossings))
        {
            setError("map square " + std::to_string(square_x) + "," + std::to_string(square_y)
                     + " not present");
            return NXT_ERR_NOT_FOUND;
        }
        if (crossings.empty())
        {
            return NXT_OK;
        }
        auto *buf = static_cast<nxt_crossing *>(std::malloc(crossings.size() * sizeof(nxt_crossing)));
        if (!buf)
        {
            setError("malloc failed");
            return NXT_ERR_INTERNAL;
        }
        std::memcpy(buf, crossings.data(), crossings.size() * sizeof(nxt_crossing));
        *out_crossings = buf;
        *out_count = crossings.size();
        return NXT_OK;
    }
    catch (const std::exception &e)
    {
        setError(std::string("get_mapsquare_crossings failed: ") + e.what());
        return NXT_ERR_DECODE;
    }
}

nxt_result nxt_get_mapsquare_clip_and_crossings(nxt_cache *handle, int square_x, int square_y,
                                                uint32_t **out_clip, size_t *out_clip_count,
                                                uint8_t *out_plane_mask,
                                                nxt_crossing **out_crossings,
                                                size_t *out_crossing_count)
{
    if (!handle || !out_clip || !out_clip_count || !out_crossings || !out_crossing_count)
    {
        setError("invalid argument: null handle or out parameter");
        return NXT_ERR_INVALID;
    }
    *out_crossings = nullptr;
    *out_crossing_count = 0;
    auto *c = reinterpret_cast<Cache *>(handle);
    try
    {
        if (!c->mapDefs)
        {
            c->mapDefs = std::make_unique<maps::DefCache>();
        }
        maps::MapSquareClip clip;
        std::vector<maps::Crossing> crossings;
        // One decode, both products: buildMapSquareClip already fills the
        // crossing sink alongside the clip grid when one is supplied.
        if (!maps::buildMapSquareClip(*c->source, square_x, square_y, *c->mapDefs, clip, &crossings))
        {
            setError("map square " + std::to_string(square_x) + "," + std::to_string(square_y)
                     + " not present");
            return NXT_ERR_NOT_FOUND;
        }
        const size_t words = static_cast<size_t>(maps::MapSquareClip::PLANES)
                             * maps::MapSquareClip::SIZE * maps::MapSquareClip::SIZE;
        auto *clipBuf = static_cast<uint32_t *>(std::malloc(words * sizeof(uint32_t)));
        if (!clipBuf)
        {
            setError("malloc failed");
            return NXT_ERR_INTERNAL;
        }
        nxt_crossing *crossingBuf = nullptr;
        if (!crossings.empty())
        {
            crossingBuf =
                static_cast<nxt_crossing *>(std::malloc(crossings.size() * sizeof(nxt_crossing)));
            if (!crossingBuf)
            {
                // Publish neither buffer: the caller frees only what a
                // successful call handed it, so a half-published pair would
                // leak the clip grid on every failure path.
                std::free(clipBuf);
                setError("malloc failed");
                return NXT_ERR_INTERNAL;
            }
            std::memcpy(crossingBuf, crossings.data(), crossings.size() * sizeof(nxt_crossing));
        }
        std::memcpy(clipBuf, clip.flags, words * sizeof(uint32_t));
        *out_clip = clipBuf;
        *out_clip_count = words;
        if (out_plane_mask)
        {
            *out_plane_mask = clip.planeMask;
        }
        *out_crossings = crossingBuf;
        *out_crossing_count = crossings.size();
        return NXT_OK;
    }
    catch (const std::exception &e)
    {
        setError(std::string("get_mapsquare_clip_and_crossings failed: ") + e.what());
        return NXT_ERR_DECODE;
    }
}

#define NXT_GETTER(suffix, T) \
    nxt_result nxt_get_##suffix##_json(nxt_cache *h, int id, char **out, size_t *len) \
    { return getJsonGeneric<T>(h, #suffix, id, out, len); }

NXT_GETTER(npc,      NpcType)
NXT_GETTER(item,     ItemType)
NXT_GETTER(loc,      LocationType)
NXT_GETTER(seq,      SequenceType)
NXT_GETTER(varbit,   VarbitType)
NXT_GETTER(enum,     EnumType)
NXT_GETTER(struct,   StructType)
NXT_GETTER(inv,      InventoryType)
NXT_GETTER(param,    ParamType)
NXT_GETTER(quest,    QuestType)
NXT_GETTER(underlay, UnderlayType)
NXT_GETTER(overlay,  OverlayType)
NXT_GETTER(worldmap, WorldMapElementType)
NXT_GETTER(sprite,   SpriteType)
NXT_GETTER(model,    ModelType)

#undef NXT_GETTER

nxt_result nxt_get_dbrow_json(nxt_cache *h, int id, char **out, size_t *len)
{
    return getDbRowJson(h, id, out, len);
}

// Interfaces are addressed differently from every other config type: one
// archive in JS5 index 3 is one interface, and each file in that archive is
// a component. We sweep the archive ourselves rather than going through
// decodeOne<T>() (which assumes one file = one config entry).
nxt_result nxt_get_if_json(nxt_cache *handle, int id, char **out_json, size_t *out_len)
{
    if (!handle || !out_json || !out_len)
    {
        setError("invalid argument: null handle or out parameter");
        return NXT_ERR_INVALID;
    }
    auto *c = reinterpret_cast<Cache *>(handle);
    try
    {
        auto &archive = c->source->archive(3, id);
        if (archive.id == -1)
        {
            setError("interface " + std::to_string(id) + " not found");
            return NXT_ERR_NOT_FOUND;
        }
        InterfaceDef def;
        def.id = id;
        for (auto &[fid, fh] : archive.files)
        {
            auto buffer = archive.readFile(fid);
            if (buffer.buffer == nullptr || buffer.remaining() == 0) continue;
            InterfaceComponentDef comp;
            comp.id = fid;
            comp.decode(buffer);
            def.components.emplace(fid, std::move(comp));
        }
        if (def.components.empty())
        {
            setError("interface " + std::to_string(id) + " has no components");
            return NXT_ERR_NOT_FOUND;
        }
        json doc = nxtdump::toJson(def);
        std::string s = doc.dump();
        char *buf = dupString(s);
        if (!buf)
        {
            setError("malloc failed");
            return NXT_ERR_INTERNAL;
        }
        *out_json = buf;
        *out_len = s.size();
        return NXT_OK;
    }
    catch (const std::exception &e)
    {
        setError(std::string("decode failed: ") + e.what());
        return NXT_ERR_DECODE;
    }
}

/* ---- Sprite raw getters ------------------------------------------------- */

nxt_result nxt_get_sprite_info(nxt_cache *handle, int id,
                               int32_t *out_frame_count, int32_t *out_canvas_w,
                               int32_t *out_canvas_h, int32_t *out_palette_count)
{
    if (!handle)
    {
        setError("invalid argument: null handle");
        return NXT_ERR_INVALID;
    }
    if (out_frame_count)   *out_frame_count = 0;
    if (out_canvas_w)      *out_canvas_w = 0;
    if (out_canvas_h)      *out_canvas_h = 0;
    if (out_palette_count) *out_palette_count = 0;
    auto *c = reinterpret_cast<Cache *>(handle);
    try
    {
        auto sprite = decodeByName<SpriteType>(*c->source, "sprite", id);
        if (!sprite)
        {
            setError("sprite " + std::to_string(id) + " not found");
            return NXT_ERR_NOT_FOUND;
        }
        if (out_frame_count)   *out_frame_count = static_cast<int32_t>(sprite->frames.size());
        if (out_canvas_w)      *out_canvas_w = sprite->canvasWidth;
        if (out_canvas_h)      *out_canvas_h = sprite->canvasHeight;
        if (out_palette_count) *out_palette_count = sprite->paletteCount;
        return NXT_OK;
    }
    catch (const std::exception &e)
    {
        setError(std::string("get_sprite_info failed: ") + e.what());
        return NXT_ERR_DECODE;
    }
}

nxt_result nxt_get_sprite_frame_rgba(nxt_cache *handle, int id, int frame_index,
                                     uint8_t **out_rgba, size_t *out_count,
                                     int32_t *out_width, int32_t *out_height,
                                     int32_t *out_offset_x, int32_t *out_offset_y)
{
    if (!handle || !out_rgba || !out_count)
    {
        setError("invalid argument: null handle or out parameter");
        return NXT_ERR_INVALID;
    }
    *out_rgba = nullptr;
    *out_count = 0;
    if (out_width)    *out_width = 0;
    if (out_height)   *out_height = 0;
    if (out_offset_x) *out_offset_x = 0;
    if (out_offset_y) *out_offset_y = 0;
    auto *c = reinterpret_cast<Cache *>(handle);
    try
    {
        auto sprite = decodeByName<SpriteType>(*c->source, "sprite", id);
        if (!sprite)
        {
            setError("sprite " + std::to_string(id) + " not found");
            return NXT_ERR_NOT_FOUND;
        }
        if (frame_index < 0 || frame_index >= static_cast<int>(sprite->frames.size()))
        {
            setError("sprite frame index out of range");
            return NXT_ERR_INVALID;
        }
        const SpriteFrame &f = sprite->frames[static_cast<size_t>(frame_index)];
        const size_t n = f.rgba.size();
        // malloc(0) may return NULL; hand back a freeable pointer regardless.
        auto *buf = static_cast<uint8_t *>(std::malloc(n != 0 ? n : 1));
        if (!buf)
        {
            setError("malloc failed");
            return NXT_ERR_INTERNAL;
        }
        if (n != 0) std::memcpy(buf, f.rgba.data(), n);
        *out_rgba = buf;
        *out_count = n;
        if (out_width)    *out_width = f.width;
        if (out_height)   *out_height = f.height;
        if (out_offset_x) *out_offset_x = f.offsetX;
        if (out_offset_y) *out_offset_y = f.offsetY;
        return NXT_OK;
    }
    catch (const std::exception &e)
    {
        setError(std::string("get_sprite_frame_rgba failed: ") + e.what());
        return NXT_ERR_DECODE;
    }
}

/* ---- Model raw getters -------------------------------------------------- */

nxt_result nxt_get_model_info(nxt_cache *handle, int id, nxt_model_info *out_info)
{
    if (!handle || !out_info)
    {
        setError("invalid argument: null handle or out parameter");
        return NXT_ERR_INVALID;
    }
    std::memset(out_info, 0, sizeof(*out_info));
    auto *c = reinterpret_cast<Cache *>(handle);
    try
    {
        auto model = decodeByName<ModelType>(*c->source, "model", id);
        if (!model)
        {
            setError("model " + std::to_string(id) + " not found");
            return NXT_ERR_NOT_FOUND;
        }
        out_info->format       = model->format;
        out_info->version      = model->version;
        out_info->mesh_count   = static_cast<int32_t>(model->renders.size());
        out_info->vertex_count = model->vertexCount;
        out_info->face_count   = model->totalFaces;
        out_info->min_x = model->minX; out_info->max_x = model->maxX;
        out_info->min_y = model->minY; out_info->max_y = model->maxY;
        out_info->min_z = model->minZ; out_info->max_z = model->maxZ;
        out_info->has_skins  = model->hasSkin ? 1 : 0;
        out_info->has_colors = model->vertexColors.empty() ? 0 : 1;
        return NXT_OK;
    }
    catch (const std::exception &e)
    {
        setError(std::string("get_model_info failed: ") + e.what());
        return NXT_ERR_DECODE;
    }
}

nxt_result nxt_get_model_vertices(nxt_cache *handle, int id,
                                  int32_t **out_xyz, size_t *out_count)
{
    if (!handle || !out_xyz || !out_count)
    {
        setError("invalid argument: null handle or out parameter");
        return NXT_ERR_INVALID;
    }
    *out_xyz = nullptr;
    *out_count = 0;
    auto *c = reinterpret_cast<Cache *>(handle);
    try
    {
        auto model = decodeByName<ModelType>(*c->source, "model", id);
        if (!model)
        {
            setError("model " + std::to_string(id) + " not found");
            return NXT_ERR_NOT_FOUND;
        }
        const size_t n = static_cast<size_t>(model->vertexCount) * 3;
        auto *buf = static_cast<int32_t *>(std::malloc((n != 0 ? n : 1) * sizeof(int32_t)));
        if (!buf)
        {
            setError("malloc failed");
            return NXT_ERR_INTERNAL;
        }
        for (int i = 0; i < model->vertexCount; ++i)
        {
            buf[i * 3 + 0] = model->vx[static_cast<size_t>(i)];
            buf[i * 3 + 1] = model->vy[static_cast<size_t>(i)];
            buf[i * 3 + 2] = model->vz[static_cast<size_t>(i)];
        }
        *out_xyz = buf;
        *out_count = n;
        return NXT_OK;
    }
    catch (const std::exception &e)
    {
        setError(std::string("get_model_vertices failed: ") + e.what());
        return NXT_ERR_DECODE;
    }
}

nxt_result nxt_get_model_normals(nxt_cache *handle, int id,
                                 int8_t **out_xyz, size_t *out_count)
{
    if (!handle || !out_xyz || !out_count)
    {
        setError("invalid argument: null handle or out parameter");
        return NXT_ERR_INVALID;
    }
    *out_xyz = nullptr;
    *out_count = 0;
    auto *c = reinterpret_cast<Cache *>(handle);
    try
    {
        auto model = decodeByName<ModelType>(*c->source, "model", id);
        if (!model)
        {
            setError("model " + std::to_string(id) + " not found");
            return NXT_ERR_NOT_FOUND;
        }
        const size_t n = static_cast<size_t>(model->vertexCount) * 3;
        auto *buf = static_cast<int8_t *>(std::malloc(n != 0 ? n : 1));
        if (!buf)
        {
            setError("malloc failed");
            return NXT_ERR_INTERNAL;
        }
        const bool haveNormals = !model->nx.empty();
        for (int i = 0; i < model->vertexCount; ++i)
        {
            buf[i * 3 + 0] = haveNormals ? static_cast<int8_t>(model->nx[static_cast<size_t>(i)]) : 0;
            buf[i * 3 + 1] = haveNormals ? static_cast<int8_t>(model->ny[static_cast<size_t>(i)]) : 0;
            buf[i * 3 + 2] = haveNormals ? static_cast<int8_t>(model->nz[static_cast<size_t>(i)]) : 0;
        }
        *out_xyz = buf;
        *out_count = n;
        return NXT_OK;
    }
    catch (const std::exception &e)
    {
        setError(std::string("get_model_normals failed: ") + e.what());
        return NXT_ERR_DECODE;
    }
}

nxt_result nxt_get_model_uvs(nxt_cache *handle, int id,
                             float **out_uv, size_t *out_count)
{
    if (!handle || !out_uv || !out_count)
    {
        setError("invalid argument: null handle or out parameter");
        return NXT_ERR_INVALID;
    }
    *out_uv = nullptr;
    *out_count = 0;
    auto *c = reinterpret_cast<Cache *>(handle);
    try
    {
        auto model = decodeByName<ModelType>(*c->source, "model", id);
        if (!model)
        {
            setError("model " + std::to_string(id) + " not found");
            return NXT_ERR_NOT_FOUND;
        }
        const size_t n = static_cast<size_t>(model->vertexCount) * 2;
        auto *buf = static_cast<float *>(std::malloc((n != 0 ? n : 1) * sizeof(float)));
        if (!buf)
        {
            setError("malloc failed");
            return NXT_ERR_INTERNAL;
        }
        const bool haveUv = !model->u.empty();
        for (int i = 0; i < model->vertexCount; ++i)
        {
            buf[i * 2 + 0] = haveUv ? model->u[static_cast<size_t>(i)] : 0.0f;
            buf[i * 2 + 1] = haveUv ? model->v[static_cast<size_t>(i)] : 0.0f;
        }
        *out_uv = buf;
        *out_count = n;
        return NXT_OK;
    }
    catch (const std::exception &e)
    {
        setError(std::string("get_model_uvs failed: ") + e.what());
        return NXT_ERR_DECODE;
    }
}

nxt_result nxt_get_model_colors(nxt_cache *handle, int id,
                                uint32_t **out_colors, size_t *out_count)
{
    if (!handle || !out_colors || !out_count)
    {
        setError("invalid argument: null handle or out parameter");
        return NXT_ERR_INVALID;
    }
    *out_colors = nullptr;
    *out_count = 0;
    auto *c = reinterpret_cast<Cache *>(handle);
    try
    {
        auto model = decodeByName<ModelType>(*c->source, "model", id);
        if (!model)
        {
            setError("model " + std::to_string(id) + " not found");
            return NXT_ERR_NOT_FOUND;
        }
        if (model->vertexColors.empty())
        {
            return NXT_OK;   // no per-vertex colours
        }
        const size_t n = model->vertexColors.size();
        auto *buf = static_cast<uint32_t *>(std::malloc(n * sizeof(uint32_t)));
        if (!buf)
        {
            setError("malloc failed");
            return NXT_ERR_INTERNAL;
        }
        for (size_t i = 0; i < n; ++i)
        {
            uint32_t col = static_cast<uint32_t>(model->vertexColors[i] & 0xFFFF);
            uint32_t a = (i < model->vertexAlphas.size())
                             ? static_cast<uint32_t>(model->vertexAlphas[i] & 0xFF) : 0xFF;
            buf[i] = (col << 8) | a;
        }
        *out_colors = buf;
        *out_count = n;
        return NXT_OK;
    }
    catch (const std::exception &e)
    {
        setError(std::string("get_model_colors failed: ") + e.what());
        return NXT_ERR_DECODE;
    }
}

nxt_result nxt_get_model_faces(nxt_cache *handle, int id,
                               nxt_model_face **out_faces, size_t *out_count)
{
    if (!handle || !out_faces || !out_count)
    {
        setError("invalid argument: null handle or out parameter");
        return NXT_ERR_INVALID;
    }
    *out_faces = nullptr;
    *out_count = 0;
    auto *c = reinterpret_cast<Cache *>(handle);
    try
    {
        auto model = decodeByName<ModelType>(*c->source, "model", id);
        if (!model)
        {
            setError("model " + std::to_string(id) + " not found");
            return NXT_ERR_NOT_FOUND;
        }
        std::vector<nxt_model_face> faces;
        faces.reserve(static_cast<size_t>(model->totalFaces));
        for (const auto &render : model->renders)
        {
            const size_t tris = render.indices.size() / 3;
            for (size_t t = 0; t < tris; ++t)
            {
                nxt_model_face f;
                f.a = render.indices[t * 3 + 0];
                f.b = render.indices[t * 3 + 1];
                f.c = render.indices[t * 3 + 2];
                f.material = render.materialArgument;
                faces.push_back(f);
            }
        }
        const size_t n = faces.size();
        auto *buf = static_cast<nxt_model_face *>(
            std::malloc((n != 0 ? n : 1) * sizeof(nxt_model_face)));
        if (!buf)
        {
            setError("malloc failed");
            return NXT_ERR_INTERNAL;
        }
        if (n != 0) std::memcpy(buf, faces.data(), n * sizeof(nxt_model_face));
        *out_faces = buf;
        *out_count = n;
        return NXT_OK;
    }
    catch (const std::exception &e)
    {
        setError(std::string("get_model_faces failed: ") + e.what());
        return NXT_ERR_DECODE;
    }
}

/* ---- Inventory icon rendering ------------------------------------------- */

nxt_result nxt_render_item_icon(nxt_cache *handle, int id,
                                int width, int height, int supersample,
                                uint8_t **out_rgba, size_t *out_count)
{
    if (!handle || !out_rgba || !out_count)
    {
        setError("invalid argument: null handle or out parameter");
        return NXT_ERR_INVALID;
    }
    *out_rgba = nullptr;
    *out_count = 0;
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096)
    {
        setError("invalid icon dimensions (expected 1..4096)");
        return NXT_ERR_INVALID;
    }
    auto *c = reinterpret_cast<Cache *>(handle);
    try
    {
        auto item = decodeByName<ItemType>(*c->source, "item", id);
        if (!item)
        {
            setError("item " + std::to_string(id) + " not found");
            return NXT_ERR_NOT_FOUND;
        }
        if (item->modelID <= 0)
        {
            setError("item " + std::to_string(id) + " has no inventory model");
            return NXT_ERR_NOT_FOUND;
        }
        auto model = decodeByName<ModelType>(*c->source, "model", item->modelID);
        if (!model)
        {
            setError("inventory model " + std::to_string(item->modelID) + " not found");
            return NXT_ERR_NOT_FOUND;
        }

        nxtrender::IconParams p;
        p.zoom2d    = item->modelZoom;
        p.xan2d     = item->modelRotationX;
        p.yan2d     = item->modelRotationY;
        p.zan2d     = item->modelAngleZ;
        p.offsetX2d = item->modelOffsetX;
        p.offsetY2d = item->modelOffsetY;
        p.resizeX   = item->resizeX;
        p.resizeY   = item->resizeY;
        p.resizeZ   = item->resizeZ;
        p.ambient   = item->ambient;
        p.contrast  = item->contrast;
        p.origColors = item->originalColors;
        p.replColors = item->replacementColors;
        p.supersample = supersample <= 0 ? 4 : supersample;

        auto icon = nxtrender::renderModelIcon(*model, p, width, height);
        const size_t n = icon.rgba.size();
        auto *buf = static_cast<uint8_t *>(std::malloc(n != 0 ? n : 1));
        if (!buf)
        {
            setError("malloc failed");
            return NXT_ERR_INTERNAL;
        }
        if (n != 0) std::memcpy(buf, icon.rgba.data(), n);
        *out_rgba = buf;
        *out_count = n;
        return NXT_OK;
    }
    catch (const std::exception &e)
    {
        setError(std::string("render_item_icon failed: ") + e.what());
        return NXT_ERR_DECODE;
    }
}

nxt_result nxt_get_json(nxt_cache *handle, const char *type_name, int id,
                        char **out_json, size_t *out_len)
{
    if (!type_name)
    {
        setError("null type_name");
        return NXT_ERR_INVALID;
    }
    std::string t(type_name);
    if (t == "npc")      return nxt_get_npc_json(handle, id, out_json, out_len);
    if (t == "item")     return nxt_get_item_json(handle, id, out_json, out_len);
    if (t == "loc")      return nxt_get_loc_json(handle, id, out_json, out_len);
    if (t == "seq")      return nxt_get_seq_json(handle, id, out_json, out_len);
    if (t == "varbit")   return nxt_get_varbit_json(handle, id, out_json, out_len);
    if (t == "varp")     return nxt_get_varp_json(handle, id, out_json, out_len);
    if (t == "enum")     return nxt_get_enum_json(handle, id, out_json, out_len);
    if (t == "struct")   return nxt_get_struct_json(handle, id, out_json, out_len);
    if (t == "inv")      return nxt_get_inv_json(handle, id, out_json, out_len);
    if (t == "param")    return nxt_get_param_json(handle, id, out_json, out_len);
    if (t == "quest")    return nxt_get_quest_json(handle, id, out_json, out_len);
    if (t == "underlay") return nxt_get_underlay_json(handle, id, out_json, out_len);
    if (t == "overlay")  return nxt_get_overlay_json(handle, id, out_json, out_len);
    if (t == "worldmap") return nxt_get_worldmap_json(handle, id, out_json, out_len);
    if (t == "dbrow")    return nxt_get_dbrow_json(handle, id, out_json, out_len);
    if (t == "if")       return nxt_get_if_json(handle, id, out_json, out_len);
    if (t == "sprite")   return nxt_get_sprite_json(handle, id, out_json, out_len);
    if (t == "model")    return nxt_get_model_json(handle, id, out_json, out_len);
    setError("unknown type: " + t);
    return NXT_ERR_INVALID;
}

nxt_result nxt_dump_all_json(nxt_cache *handle, const char *type_name, int limit_or_neg1,
                             char **out_json, size_t *out_len)
{
    if (!handle || !type_name || !out_json || !out_len)
    {
        setError("invalid argument");
        return NXT_ERR_INVALID;
    }
    auto *c = reinterpret_cast<Cache *>(handle);
    std::string t(type_name);
    const auto &defs = nxt::typeDefaults();
    auto it = defs.find(t);
    if (it == defs.end())
    {
        setError("unknown type: " + t);
        return NXT_ERR_INVALID;
    }

    try
    {
        json arr;
        if      (t == "npc")      arr = dumpAll<NpcType>(*c->source, it->second, limit_or_neg1);
        else if (t == "item")     arr = dumpAll<ItemType>(*c->source, it->second, limit_or_neg1);
        else if (t == "loc")      arr = dumpAll<LocationType>(*c->source, it->second, limit_or_neg1);
        else if (t == "seq")      arr = dumpAll<SequenceType>(*c->source, it->second, limit_or_neg1);
        else if (t == "varbit")   arr = dumpAll<VarbitType>(*c->source, it->second, limit_or_neg1);
        else if (t == "varp")     arr = dumpAll<VarPlayerType>(*c->source, it->second, limit_or_neg1);
        else if (t == "enum")     arr = dumpAll<EnumType>(*c->source, it->second, limit_or_neg1);
        else if (t == "struct")   arr = dumpAll<StructType>(*c->source, it->second, limit_or_neg1);
        else if (t == "inv")      arr = dumpAll<InventoryType>(*c->source, it->second, limit_or_neg1);
        else if (t == "param")    arr = dumpAll<ParamType>(*c->source, it->second, limit_or_neg1);
        else if (t == "quest")    arr = dumpAll<QuestType>(*c->source, it->second, limit_or_neg1);
        else if (t == "underlay") arr = dumpAll<UnderlayType>(*c->source, it->second, limit_or_neg1);
        else if (t == "overlay")  arr = dumpAll<OverlayType>(*c->source, it->second, limit_or_neg1);
        else if (t == "worldmap") arr = dumpAll<WorldMapElementType>(*c->source, it->second, limit_or_neg1);
        else if (t == "sprite")   arr = dumpAll<SpriteType>(*c->source, it->second, limit_or_neg1);
        else if (t == "model")    arr = dumpAll<ModelType>(*c->source, it->second, limit_or_neg1);
        else if (t == "if")
        {
            // Interfaces are archive-keyed, not file-keyed; sweep each archive
            // in index 3, build an InterfaceDef per archive.
            arr = json::array();
            std::vector<int> aids = c->source->archiveIds(it->second.indexId);
            for (int aid : aids)
            {
                if (limit_or_neg1 >= 0 && static_cast<int>(arr.size()) >= limit_or_neg1) break;
                auto &archive = c->source->archive(it->second.indexId, aid);
                if (archive.id == -1) continue;
                InterfaceDef def;
                def.id = aid;
                for (auto &[fid, fh] : archive.files)
                {
                    auto buffer = archive.readFile(fid);
                    if (buffer.buffer == nullptr || buffer.remaining() == 0) continue;
                    InterfaceComponentDef comp;
                    comp.id = fid;
                    comp.decode(buffer);
                    def.components.emplace(fid, std::move(comp));
                }
                if (!def.components.empty()) arr.push_back(nxtdump::toJson(def));
            }
        }
        else if (t == "dbrow")
        {
            // dbrow uses DbRowProvider for its tableId/col0Key index; surface raw rows.
            if (!c->dbProvider)
            {
                c->dbProvider = std::make_unique<DbRowProvider>(c->source.get());
                c->dbProvider->load();
            }
            arr = json::array();
            auto &archive = c->source->archive(it->second.indexId, it->second.archiveId);
            if (archive.id != -1)
            {
                for (auto &[fid, fh] : archive.files)
                {
                    if (limit_or_neg1 >= 0 && static_cast<int>(arr.size()) >= limit_or_neg1) break;
                    DbRowType *row = c->dbProvider->get(fid);
                    if (row && row->id != -1) arr.push_back(nxtdump::toJson(*row));
                }
            }
        }

        std::string s = arr.dump();
        char *buf = dupString(s);
        if (!buf)
        {
            setError("malloc failed");
            return NXT_ERR_INTERNAL;
        }
        *out_json = buf;
        *out_len = s.size();
        return NXT_OK;
    }
    catch (const std::exception &e)
    {
        setError(std::string("dump_all failed: ") + e.what());
        return NXT_ERR_DECODE;
    }
}

nxt_result nxt_list_type_ids(nxt_cache *handle, const char *type_name,
                             int **out_ids, size_t *out_count)
{
    if (!handle || !type_name || !out_ids || !out_count)
    {
        setError("invalid argument: null handle or out parameter");
        return NXT_ERR_INVALID;
    }
    *out_ids = nullptr;
    *out_count = 0;
    auto *c = reinterpret_cast<Cache *>(handle);
    std::string t(type_name);
    const auto &defs = nxt::typeDefaults();
    auto it = defs.find(t);
    if (it == defs.end())
    {
        setError("unknown type: " + t);
        return NXT_ERR_INVALID;
    }
    try
    {
        std::vector<int> ids = enumerateTypeIds(*c->source, t, it->second);
        size_t n = ids.size();
        // malloc(0) may return NULL; allocate at least one slot so the caller
        // always receives a freeable, non-NULL pointer.
        auto *buf = static_cast<int *>(std::malloc((n != 0 ? n : 1) * sizeof(int)));
        if (!buf)
        {
            setError("malloc failed");
            return NXT_ERR_INTERNAL;
        }
        if (n != 0) std::memcpy(buf, ids.data(), n * sizeof(int));
        *out_ids = buf;
        *out_count = n;
        return NXT_OK;
    }
    catch (const std::exception &e)
    {
        setError(std::string("list_type_ids failed: ") + e.what());
        return NXT_ERR_IO;
    }
}

nxt_result nxt_varp_exists(nxt_cache *handle, int id, int32_t *out_exists)
{
    if (!handle || !out_exists)
    {
        setError("invalid argument: null handle or out parameter");
        return NXT_ERR_INVALID;
    }
    auto *c = reinterpret_cast<Cache *>(handle);
    try
    {
        std::string error;
        const VarpLookup lookup = lookupVarp(*c->source, id, nullptr, error);
        if (lookup == VarpLookup::Found || lookup == VarpLookup::NotFound)
        {
            *out_exists = lookup == VarpLookup::Found ? 1 : 0;
            return NXT_OK;
        }
        setError(error);
        return varpLookupResult(lookup);
    }
    catch (const std::exception &e)
    {
        setError(std::string("varp_exists failed: ") + e.what());
        return NXT_ERR_IO;
    }
}

nxt_result nxt_get_varp_info(nxt_cache *handle, int id, nxt_varp_info *io_info)
{
    if (!handle || !io_info)
    {
        setError("invalid argument: null handle or out parameter");
        return NXT_ERR_INVALID;
    }
    if (io_info->struct_size < sizeof(nxt_varp_info))
    {
        setError("nxt_varp_info.struct_size " + std::to_string(io_info->struct_size) +
                 " is smaller than " + std::to_string(sizeof(nxt_varp_info)));
        return NXT_ERR_INVALID;
    }
    auto *c = reinterpret_cast<Cache *>(handle);
    try
    {
        std::string error;
        VarPlayerType type{};
        const VarpLookup lookup = lookupVarp(*c->source, id, &type, error);
        if (lookup != VarpLookup::Found)
        {
            setError(error);
            return varpLookupResult(lookup);
        }
        *io_info = toVarpInfo(type);
        return NXT_OK;
    }
    catch (const std::exception &e)
    {
        setError(std::string("get_varp_info failed: ") + e.what());
        return NXT_ERR_IO;
    }
}

nxt_result nxt_get_varp_json(nxt_cache *handle, int id, char **out_json, size_t *out_len)
{
    if (!handle || !out_json || !out_len)
    {
        setError("invalid argument: null handle or out parameter");
        return NXT_ERR_INVALID;
    }
    auto *c = reinterpret_cast<Cache *>(handle);
    try
    {
        std::string error;
        VarPlayerType type{};
        const VarpLookup lookup = lookupVarp(*c->source, id, &type, error);
        if (lookup != VarpLookup::Found)
        {
            setError(error);
            return varpLookupResult(lookup);
        }
        const std::string text = nxtdump::toJson(type).dump();
        char *buf = dupString(text);
        if (!buf)
        {
            setError("malloc failed");
            return NXT_ERR_INTERNAL;
        }
        *out_json = buf;
        *out_len = text.size();
        return NXT_OK;
    }
    catch (const std::exception &e)
    {
        setError(std::string("get_varp_json failed: ") + e.what());
        return NXT_ERR_IO;
    }
}

}  // extern "C"
