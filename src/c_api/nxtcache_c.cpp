#define NXTCACHE_BUILDING

#include "c_api/nxtcache_c.h"

#include "config_types/Types.h"
#include "core/Archive.h"
#include "core/CacheSource.h"
#include "core/DbRowProvider.h"
#include "core/RSCache.h"
#include "core/TypeMappings.h"
#include "dumper/Json.h"
#include "network/Js5Cache.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <optional>
#include <string>

namespace {

using json = nlohmann::json;

struct Cache
{
    std::unique_ptr<CacheSource> source;
    RSCache *local{nullptr};   // non-owning, only set if `source` is RSCache
    std::unique_ptr<DbRowProvider> dbProvider;
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
        wrap->source = std::make_unique<js5::Js5Cache>();
        return reinterpret_cast<nxt_cache *>(wrap.release());
    }
    catch (const std::exception &e)
    {
        setError(std::string("open_live failed: ") + e.what());
        return nullptr;
    }
}

nxt_result nxt_cache_enable_live_fallback(nxt_cache *handle)
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
        c->local->enableLiveFallback();
        return NXT_OK;
    }
    catch (const std::exception &e)
    {
        setError(std::string("enable_live_fallback failed: ") + e.what());
        return NXT_ERR_IO;
    }
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

#undef NXT_GETTER

nxt_result nxt_get_dbrow_json(nxt_cache *h, int id, char **out, size_t *len)
{
    return getDbRowJson(h, id, out, len);
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
    if (t == "enum")     return nxt_get_enum_json(handle, id, out_json, out_len);
    if (t == "struct")   return nxt_get_struct_json(handle, id, out_json, out_len);
    if (t == "inv")      return nxt_get_inv_json(handle, id, out_json, out_len);
    if (t == "param")    return nxt_get_param_json(handle, id, out_json, out_len);
    if (t == "quest")    return nxt_get_quest_json(handle, id, out_json, out_len);
    if (t == "underlay") return nxt_get_underlay_json(handle, id, out_json, out_len);
    if (t == "overlay")  return nxt_get_overlay_json(handle, id, out_json, out_len);
    if (t == "worldmap") return nxt_get_worldmap_json(handle, id, out_json, out_len);
    if (t == "dbrow")    return nxt_get_dbrow_json(handle, id, out_json, out_len);
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
        else if (t == "enum")     arr = dumpAll<EnumType>(*c->source, it->second, limit_or_neg1);
        else if (t == "struct")   arr = dumpAll<StructType>(*c->source, it->second, limit_or_neg1);
        else if (t == "inv")      arr = dumpAll<InventoryType>(*c->source, it->second, limit_or_neg1);
        else if (t == "param")    arr = dumpAll<ParamType>(*c->source, it->second, limit_or_neg1);
        else if (t == "quest")    arr = dumpAll<QuestType>(*c->source, it->second, limit_or_neg1);
        else if (t == "underlay") arr = dumpAll<UnderlayType>(*c->source, it->second, limit_or_neg1);
        else if (t == "overlay")  arr = dumpAll<OverlayType>(*c->source, it->second, limit_or_neg1);
        else if (t == "worldmap") arr = dumpAll<WorldMapElementType>(*c->source, it->second, limit_or_neg1);
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

}  // extern "C"
