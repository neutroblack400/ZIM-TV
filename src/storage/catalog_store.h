// Processed-catalog cache: <dataDir>/cache/<profileId>/catalog.ztc
//
// The raw saved copy is still kept (the Xtream / M3U refresh paths use it). This file stores the
// catalogs AS BUILT from that body — live / movie / series items, the per-series episode lists and the
// parse / classify diagnostics — so opening an already-loaded profile does not re-scan and re-split the
// (often huge) playlist. Fingerprinted with the saved copy's FNV-1a body checksum: writing the store
// after a refresh replaces it, and a store whose fingerprint does not match the body is never used
// (the caller deletes it and falls back to a full rebuild).
//
// Crash-safe: written to <file>.tmp, flushed and renamed into place. Any verification failure
// (size, magic, version, fingerprint, structure, trailing checksum) deletes the file.
//
// All integers are little-endian. Strings are uint32 length + bytes (no NUL), each capped at
// MAX_STRING bytes and checked against the bytes left in the file.

#ifndef ZIMTV_STORAGE_CATALOG_STORE_H
#define ZIMTV_STORAGE_CATALOG_STORE_H

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "../iptv/catalog.h"
#include "../iptv/m3u.h"
#include "../iptv/models.h"

class CatalogStore {

public:

    static const int FORMAT_VERSION = 1;
    static constexpr const char *FILE_NAME = "catalog.ztc";

    // sanity caps (a corrupt length may point nowhere; nothing is trusted)
    static const uint64_t MAX_FILE_BYTES = 768ull * 1024 * 1024;          // 768 MiB
    static const uint32_t MAX_STRING = 4u * 1024 * 1024;                  // 4 MiB per string
    static const uint32_t MAX_COUNT = 8000000u;                           // items / categories / episodes

    explicit CatalogStore(std::string dataDir);

    struct Diagnostics {
        m3u::Classification classify;
        m3u::Stats stats;
        std::string urlTvg;           // the playlist header's url-tvg (the session's EPG source reads it)
        // per-catalog "what the parser saw" (models.h ParseStats) + the time the original build spent;
        // restored on load so the Catalog info panel reads exactly as after a full build
        iptv::ParseStats movieParse;
        iptv::ParseStats seriesParse;
        double movieParseMs = 0;
        double seriesParseMs = 0;
        int categories = 0;           // Live catalog categories, incl. Uncategorized
    };

    // Persists the built catalogs. Returns false (deleting any partial file) on failure.
    bool save(const std::string &profileId, uint64_t fingerprint, int64_t savedAt,
              const iptv::LiveCatalog &live, const iptv::MovieCatalog &movies, const iptv::SeriesCatalog &series,
              const std::unordered_map<std::string, std::shared_ptr<const iptv::SeriesInfo>> &seriesInfo,
              const Diagnostics &diag, std::string *error = nullptr) const;

    struct Result {
        std::vector<iptv::Category> liveCategories;
        std::vector<iptv::LiveChannel> liveChannels;
        std::vector<iptv::Category> movieCategories;
        std::vector<iptv::Movie> movieItems;
        std::vector<iptv::Category> seriesCategories;
        std::vector<iptv::Series> seriesItems;
        std::unordered_map<std::string, std::shared_ptr<const iptv::SeriesInfo>> seriesInfo;
        Diagnostics diag;
        int64_t savedAt = 0;
    };

    // Validates and reads the store. On any mismatch deletes the file and returns false (nothing is partial).
    bool load(const std::string &profileId, uint64_t fingerprint, Result &out, std::string *error = nullptr) const;

    // deletes the store of one profile (profile deletion)
    bool remove(const std::string &profileId) const;

    // path helpers (exposed for tests)
    std::string path(const std::string &profileId) const;
    std::string pathForRead(const std::string &profileId) const;   // store file (read path)

private:

    std::string dir(const std::string &profileId) const;

    std::string root;
};

#endif // ZIMTV_STORAGE_CATALOG_STORE_H
