#include "catalog_store.h"

#include "../platform/fs.h"
#include "../platform/log.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <type_traits>
#include <utility>

namespace {

    // ------------------------------------------------------------------ little-endian IO with a running FNV
    // Every payload byte feeds an incremental FNV-1a; the last 8 bytes of the file hold it (written raw,
    // not fed back). On load the same running value is recomputed over the bytes read so far and compared
    // with the trailer: a torn or corrupt file is detected even if it happens to be well formed.

    struct Writer {
        FILE *f = nullptr;
        uint64_t fnv = 1469598103934665603ull;

        void feed(const void *p, size_t n) {
            const unsigned char *c = static_cast<const unsigned char *>(p);
            for (size_t i = 0; i < n; i++) {
                fnv ^= c[i];
                fnv *= 1099511628211ull;
            }
        }

        void bytes(const void *p, size_t n) {
            if (f == nullptr) {
                return;
            }
            if (std::fwrite(p, 1, n, f) != n) {
                std::fclose(f);
                f = nullptr;
                return;
            }
            feed(p, n);
        }

        void bytesRaw(const void *p, size_t n) {   // payload-independent (trailer): not fed to the checksum
            if (f == nullptr) {
                return;
            }
            if (std::fwrite(p, 1, n, f) != n) {
                std::fclose(f);
                f = nullptr;
            }
        }

        void u8(uint8_t v) { bytes(&v, 1); }

        void u32(uint32_t v) {
            uint8_t b[4] = {(uint8_t) (v & 0xff), (uint8_t) ((v >> 8) & 0xff), (uint8_t) ((v >> 16) & 0xff),
                            (uint8_t) ((v >> 24) & 0xff)};
            bytes(b, 4);
        }

        void i32(int32_t v) { u32((uint32_t) v); }

        void i64(int64_t v) {
            uint8_t b[8];
            uint64_t x = (uint64_t) v;
            for (int i = 0; i < 8; i++) {
                b[i] = (uint8_t) ((x >> (8 * i)) & 0xff);
            }
            bytes(b, 8);
        }

        void u64(uint64_t v) {
            uint8_t b[8];
            for (int i = 0; i < 8; i++) {
                b[i] = (uint8_t) ((v >> (8 * i)) & 0xff);
            }
            bytes(b, 8);
        }

        void f64(double v) {
            uint64_t bits;
            std::memcpy(&bits, &v, 8);
            u64(bits);
        }

        bool str(const std::string &s) {
            if (s.size() > CatalogStore::MAX_STRING) {
                std::fclose(f);
                f = nullptr;
                return false;
            }
            u32((uint32_t) s.size());
            if (!s.empty()) {
                bytes(s.data(), s.size());
            }
            return f != nullptr;
        }

        bool pair(const std::pair<std::string, int> &p) {
            if (!str(p.first)) {
                return false;
            }
            i32(p.second);
            return f != nullptr;
        }
    };

    struct Reader {
        FILE *f = nullptr;
        uint64_t fnv = 1469598103934665603ull;
        int64_t fileBytes = 0;
        int64_t at = 0;

        void fail() {
            if (f != nullptr) {
                std::fclose(f);
                f = nullptr;
            }
        }

        bool bytes(void *p, size_t n) {
            if (f == nullptr) {
                return false;
            }
            if ((int64_t) n > fileBytes - at) {
                fail();
                return false;
            }
            if (n != 0 && std::fread(p, 1, n, f) != n) {
                fail();
                return false;
            }
            at += (int64_t) n;
            const unsigned char *c = static_cast<const unsigned char *>(p);
            for (size_t i = 0; i < n; i++) {
                fnv ^= c[i];
                fnv *= 1099511628211ull;
            }
            return true;
        }

        bool u8(uint8_t &v) { return bytes(&v, 1); }

        bool u32(uint32_t &v) {
            uint8_t b[4];
            if (!bytes(b, 4)) {
                return false;
            }
            v = (uint32_t) b[0] | ((uint32_t) b[1] << 8) | ((uint32_t) b[2] << 16) | ((uint32_t) b[3] << 24);
            return true;
        }

        bool i64(int64_t &v) {
            uint8_t b[8];
            if (!bytes(b, 8)) {
                return false;
            }
            uint64_t r = 0;
            for (int i = 7; i >= 0; i--) {
                r = (r << 8) | b[i];
            }
            v = (int64_t) r;
            return true;
        }

        // floats are stored as their 64-bit bit pattern; on load they round-trip exactly
        bool u64(uint64_t &v) {
            uint8_t b[8];
            if (!bytes(b, 8)) {
                return false;
            }
            uint64_t r = 0;
            for (int i = 7; i >= 0; i--) {
                r = (r << 8) | b[i];
            }
            v = r;
            return true;
        }

        bool f64(double &v) {
            uint64_t bits = 0;
            if (!u64(bits)) {
                return false;
            }
            std::memcpy(&v, &bits, 8);
            return true;
        }

        bool str(std::string &s) {
            uint32_t len = 0;
            if (!u32(len)) {
                return false;
            }
            if ((int64_t) len > fileBytes - at || len > CatalogStore::MAX_STRING) {
                fail();
                return false;
            }
            s.clear();
            if (len == 0) {
                return true;
            }
            s.reserve(len);
            char buf[8192];
            int64_t left = len;
            while (left > 0) {
                size_t chunk = (size_t) std::min<int64_t>(left, sizeof(buf));
                if (!bytes(buf, chunk)) {
                    return false;
                }
                s.append(buf, chunk);
                left -= (int64_t) chunk;
            }
            return true;
        }

        bool pair(std::pair<std::string, int> &p) {
            if (!str(p.first)) {
                return false;
            }
            uint32_t v = 0;
            if (!u32(v)) {
                return false;
            }
            p.second = (int) (int32_t) v;
            return true;
        }

        // sanity cap for an element count: minItemBytes bounds what each element costs on disk
        bool count(uint32_t &v, int64_t minItemBytes) {
            if (!u32(v)) {
                return false;
            }
            if (v > CatalogStore::MAX_COUNT || (int64_t) v * minItemBytes > fileBytes - at) {
                fail();
                return false;
            }
            return true;
        }
    };

    const char MAGIC[4] = {'Z', 'T', 'C', '1'};

    bool safeName(const std::string &s) {
        if (s.empty() || s.size() > 64) {
            return false;
        }
        for (char c: s) {
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
                return false;
            }
        }
        return true;
    }

    bool putParse(Writer &w, const iptv::ParseStats &p) {
        w.u32((uint32_t) p.raw);
        w.u32((uint32_t) p.parsed);
        w.u32((uint32_t) p.rejectedNotObject);
        w.u32((uint32_t) p.rejectedMissingId);
        w.u32((uint32_t) p.rejectedDuplicateId);
        w.u32((uint32_t) p.missingName);
        w.u32((uint32_t) p.missingCategory);
        w.u32((uint32_t) p.missingPoster);
        w.u32((uint32_t) p.missingExtension);
        w.u32((uint32_t) p.titleYearAliases);
        return w.f != nullptr;
    }

    bool getParse(Reader &r, iptv::ParseStats &p, uint32_t &v) {
        if (!r.u32(v)) { return false; } p.raw = (int) v;
        if (!r.u32(v)) { return false; } p.parsed = (int) v;
        if (!r.u32(v)) { return false; } p.rejectedNotObject = (int) v;
        if (!r.u32(v)) { return false; } p.rejectedMissingId = (int) v;
        if (!r.u32(v)) { return false; } p.rejectedDuplicateId = (int) v;
        if (!r.u32(v)) { return false; } p.missingName = (int) v;
        if (!r.u32(v)) { return false; } p.missingCategory = (int) v;
        if (!r.u32(v)) { return false; } p.missingPoster = (int) v;
        if (!r.u32(v)) { return false; } p.missingExtension = (int) v;
        if (!r.u32(v)) { return false; } p.titleYearAliases = (int) v;
        return true;
    }

    bool putCategory(Writer &w, const iptv::Category &c) {
        return w.str(c.id) && w.str(c.name) && w.str(c.parentId);
    }

    bool getCategory(Reader &r, iptv::Category &c) {
        return r.str(c.id) && r.str(c.name) && r.str(c.parentId);
    }

    bool putLive(Writer &w, const iptv::LiveChannel &ch) {
        if (!w.str(ch.id) || !w.str(ch.name) || !w.str(ch.categoryId) || !w.str(ch.icon) || !w.str(ch.epgId)
            || !w.str(ch.url) || !w.str(ch.userAgent) || !w.str(ch.cmd)) {
            return false;
        }
        w.i64(ch.added);
        w.i32(ch.num);
        w.u8(ch.archive ? 1 : 0);
        return w.f != nullptr;
    }

    bool getLive(Reader &r, iptv::LiveChannel &ch) {
        uint8_t arch = 0;
        uint32_t num = 0;
        if (!r.str(ch.id) || !r.str(ch.name) || !r.str(ch.categoryId) || !r.str(ch.icon) || !r.str(ch.epgId)
            || !r.str(ch.url) || !r.str(ch.userAgent) || !r.str(ch.cmd) || !r.i64(ch.added)) {
            return false;
        }
        if (!r.u32(num)) {
            return false;
        }
        ch.num = (int) (int32_t) num;
        if (!r.u8(arch)) {
            return false;
        }
        ch.archive = arch != 0;
        return true;
    }

    bool putMovie(Writer &w, const iptv::Movie &m) {
        if (!w.str(m.streamId) || !w.str(m.name) || !w.str(m.title) || !w.str(m.categoryId) || !w.str(m.icon)
            || !w.str(m.extension) || !w.str(m.url)) {
            return false;
        }
        w.f64((double) m.rating);
        w.i64(m.year);
        w.i64(m.added);
        return w.f != nullptr;
    }

    bool getMovie(Reader &r, iptv::Movie &m) {
        double rating = 0;
        int64_t year = 0;
        if (!r.str(m.streamId) || !r.str(m.name) || !r.str(m.title) || !r.str(m.categoryId) || !r.str(m.icon)
            || !r.str(m.extension) || !r.str(m.url) || !r.f64(rating) || !r.i64(year) || !r.i64(m.added)) {
            return false;
        }
        m.rating = (float) rating;
        m.year = (int) year;
        return true;
    }

    bool putMedia(Writer &w, const iptv::MediaSummary &m) {
        w.u32((uint32_t) (int32_t) m.width);
        w.u32((uint32_t) (int32_t) m.height);
        if (!w.str(m.videoCodec) || !w.str(m.audioCodec)) {
            return false;
        }
        w.u32((uint32_t) (int32_t) m.audioChannels);
        if (!w.str(m.audioLanguage)) {
            return false;
        }
        w.u32((uint32_t) (int32_t) m.bitrateKbps);
        return w.f != nullptr;
    }

    bool getMedia(Reader &r, iptv::MediaSummary &m) {
        uint32_t w2 = 0, h = 0, ch = 0, br = 0;
        if (!r.u32(w2) || !r.u32(h)) {
            return false;
        }
        m.width = (int) (int32_t) w2;
        m.height = (int) (int32_t) h;
        if (!r.str(m.videoCodec) || !r.str(m.audioCodec) || !r.u32(ch)) {
            return false;
        }
        m.audioChannels = (int) (int32_t) ch;
        if (!r.str(m.audioLanguage) || !r.u32(br)) {
            return false;
        }
        m.bitrateKbps = (int) (int32_t) br;
        return true;
    }

    bool putSeries(Writer &w, const iptv::Series &s) {
        if (!w.str(s.seriesId) || !w.str(s.name) || !w.str(s.title) || !w.str(s.categoryId) || !w.str(s.cover)
            || !w.str(s.plot) || !w.str(s.cast) || !w.str(s.director) || !w.str(s.genre) || !w.str(s.releaseDate)) {
            return false;
        }
        w.f64((double) s.rating);
        w.i64(s.year);
        w.i64(s.runtimeMinutes);
        w.i64(s.lastModified);
        return w.str(s.backdrop);
    }

    bool getSeries(Reader &r, iptv::Series &s) {
        double rating = 0;
        int64_t year = 0, runtime = 0;
        if (!r.str(s.seriesId) || !r.str(s.name) || !r.str(s.title) || !r.str(s.categoryId) || !r.str(s.cover)
            || !r.str(s.plot) || !r.str(s.cast) || !r.str(s.director) || !r.str(s.genre) || !r.str(s.releaseDate)
            || !r.f64(rating) || !r.i64(year) || !r.i64(runtime) || !r.i64(s.lastModified) || !r.str(s.backdrop)) {
            return false;
        }
        s.rating = (float) rating;
        s.year = (int) year;
        s.runtimeMinutes = (int) runtime;
        return true;
    }
}

CatalogStore::CatalogStore(std::string dataDir) : root(fs::join(dataDir, "cache")) {}

std::string CatalogStore::dir(const std::string &profileId) const {
    return fs::join(root, profileId);
}

std::string CatalogStore::path(const std::string &profileId) const {
    return fs::join(dir(profileId), FILE_NAME);
}

std::string CatalogStore::pathForRead(const std::string &profileId) const {
    return path(profileId);
}

bool CatalogStore::remove(const std::string &profileId) const {
    if (!safeName(profileId)) {
        return false;
    }
    // a stray temp file from a crashed save is removed too
    fs::removeFile(path(profileId) + ".tmp");
    return fs::removeFile(path(profileId));
}

// ==================================================================== writing
bool CatalogStore::save(const std::string &profileId, uint64_t fingerprint, int64_t savedAt,
                        const iptv::LiveCatalog &live, const iptv::MovieCatalog &movies,
                        const iptv::SeriesCatalog &series,
                        const std::unordered_map<std::string, std::shared_ptr<const iptv::SeriesInfo>> &seriesInfo,
                        const Diagnostics &diag, std::string *error) const {
    if (!safeName(profileId)) {
        if (error) {
            *error = "invalid profile id";
        }
        return false;
    }
    std::string d = dir(profileId);
    if (!fs::ensureDir(d, error)) {
        return false;
    }
    std::string target = path(profileId);
    std::string tmp = target + ".tmp";
    fs::removeFile(tmp);

    Writer w;
    w.f = std::fopen(tmp.c_str(), "wb");
    if (w.f == nullptr) {
        if (error) {
            *error = "cannot create the store file";
        }
        return false;
    }

    w.bytes(MAGIC, 4);
    w.u32((uint32_t) FORMAT_VERSION);
    w.u64(fingerprint);
    w.i64(savedAt);
    if (!w.str(diag.urlTvg)) {   // a >4 MiB url-tvg is not persisted (str() already closed the file)
        fs::removeFile(tmp);
        if (error) {
            *error = "store write failed";
        }
        return false;
    }
    bool ok = w.f != nullptr;

    // diagnostics block (counts and timings only; never URLs or names)
    const m3u::Stats &st = diag.stats;
    w.i64((int64_t) st.bytes);
    w.i64((int64_t) st.lines);
    w.u8(st.header ? 1 : 0);
    w.u32((uint32_t) st.extinf);
    w.u32((uint32_t) st.plainUrls);
    w.u32((uint32_t) st.channels);
    w.u32((uint32_t) st.groups);
    w.u32((uint32_t) st.uncategorized);
    w.u32((uint32_t) st.withLogo);
    w.u32((uint32_t) st.withoutLogo);
    w.u32((uint32_t) st.withTvgId);
    w.u32((uint32_t) st.userAgents);
    w.u32((uint32_t) st.hls);
    w.u32((uint32_t) st.https);
    w.u32((uint32_t) st.otherProtocols);
    w.u32((uint32_t) st.duplicateIds);
    w.u32((uint32_t) st.missingUrl);
    w.u32((uint32_t) st.invalidUrl);
    w.u32((uint32_t) st.overLimit);
    w.u32((uint32_t) st.malformedExtinf);
    w.u32((uint32_t) st.unknownDirectives);

    putParse(w, diag.movieParse);
    putParse(w, diag.seriesParse);
    w.f64(diag.movieParseMs);
    w.f64(diag.seriesParseMs);

    const m3u::Classification &cf = diag.classify;
    w.u32((uint32_t) cf.live);
    w.u32((uint32_t) cf.movie);
    w.u32((uint32_t) cf.series);
    w.u32((uint32_t) cf.seriesFound);
    w.u32((uint32_t) cf.seriesUnmarked);
    auto putGroups = [&w](const std::vector<std::pair<std::string, int>> &groups) {
        w.u32((uint32_t) groups.size());
        for (const auto &g: groups) {
            if (!w.pair(g)) {
                return false;
            }
        }
        return true;
    };
    ok = ok && putGroups(cf.movieGroups) && putGroups(cf.seriesGroups) && putGroups(cf.liveGroups);
    w.u32((uint32_t) std::max(0, diag.categories));

    // sections
    auto putSection = [&w](const std::vector<iptv::Category> &cats, const auto &items,
                           auto &&putItem) {
        w.u32((uint32_t) cats.size());
        w.u32((uint32_t) items.size());
        for (const auto &c: cats) {
            if (!putCategory(w, c)) {
                return false;
            }
        }
        for (const auto &it: items) {
            if (!putItem(w, it)) {
                return false;
            }
        }
        return w.f != nullptr;
    };

    ok = ok && putSection(live.categories(), live.channels(), putLive);
    ok = ok && putSection(movies.categories(), movies.items(), putMovie);
    ok = ok && putSection(series.categories(), series.items(), putSeries);

    // series info: counts written first so a corrupt file fails before allocating anything
    {
        uint64_t episodeTotal = 0;
        for (const auto &kv: seriesInfo) {
            for (const iptv::Season &s: kv.second->seasons) {
                episodeTotal += s.episodes.size();
            }
        }
        w.u32((uint32_t) seriesInfo.size());
        w.u64(episodeTotal);
        for (const auto &kv: seriesInfo) {
            const iptv::SeriesInfo &si = *kv.second;
            if (!w.str(si.seriesId) || !putSeries(w, si.series)) {
                ok = false;
                break;
            }
            w.u32((uint32_t) si.seasons.size());
            for (const iptv::Season &season: si.seasons) {
                w.u32((uint32_t) (int32_t) season.number);
                if (!w.str(season.name) || !w.str(season.cover)) {
                    ok = false;
                    break;
                }
                w.u32((uint32_t) season.episodes.size());
                for (const iptv::Episode &ep: season.episodes) {
                    bool a = w.str(ep.id);
                    w.u32((uint32_t) (int32_t) ep.season);
                    w.u32((uint32_t) (int32_t) ep.number);
                    bool b = w.str(ep.title) && w.str(ep.extension) && w.str(ep.image) && w.str(ep.plot);
                    w.u32((uint32_t) (int32_t) ep.durationSeconds);
                    w.f64((double) ep.rating);
                    bool c = putMedia(w, ep.media) && w.str(ep.cmd);
                    w.u32((uint32_t) (int32_t) ep.linkSeries);
                    bool d = w.str(ep.url);
                    if (!(a && b && c && d) || w.f == nullptr) {
                        ok = false;
                        break;
                    }
                }
                if (!ok) {
                    break;
                }
            }
            if (!ok) {
                break;
            }
        }
    }

    if (ok && w.f != nullptr) {
        uint64_t sum = w.fnv;   // everything before the trailer
        uint8_t trailer[8];
        for (int i = 0; i < 8; i++) {
            trailer[i] = (uint8_t) ((sum >> (8 * i)) & 0xff);
        }
        w.bytesRaw(trailer, 8);
    }

    if (!ok || w.f == nullptr || std::fclose(w.f) != 0) {
        fs::removeFile(tmp);
        if (error) {
            *error = "store write failed";
        }
        return false;
    }
    if (std::rename(tmp.c_str(), target.c_str()) != 0) {
        fs::removeFile(tmp);
        if (error) {
            *error = "cannot rename the store into place";
        }
        return false;
    }
    return true;
}

// ==================================================================== reading
bool CatalogStore::load(const std::string &profileId, uint64_t fingerprint, Result &out, std::string *error) const {
    auto fail = [&](const char *msg) {
        if (error) {
            *error = msg;
        }
        fs::removeFile(path(profileId));   // never read a broken store again
        return false;
    };

    if (!safeName(profileId)) {
        if (error) {
            *error = "invalid profile id";
        }
        return false;
    }
    std::string target = path(profileId);
    int64_t size = fs::fileSize(target);
    if (size <= 16 || (uint64_t) size > MAX_FILE_BYTES) {
        return fail("size out of range");
    }

    Reader r;
    r.f = std::fopen(target.c_str(), "rb");
    if (r.f == nullptr) {
        return fail("cannot open the store");
    }
    r.fileBytes = size;

    uint8_t magic[4];
    if (!r.bytes(magic, 4)) {
        return fail("read error");
    }
    if (std::memcmp(magic, MAGIC, 4) != 0) {
        return fail("bad magic");
    }
    uint32_t version = 0;
    if (!r.u32(version) || version != (uint32_t) FORMAT_VERSION) {
        return fail("version mismatch");
    }
    uint64_t fp = 0;
    int64_t savedAt = 0;
    if (!r.u64(fp) || !r.i64(savedAt)) {
        return fail("read error");
    }
    if (fp != fingerprint) {
        return fail("the saved copy changed since the store was written");   // full rebuild will refresh it
    }
    out.savedAt = savedAt;
    if (!r.str(out.diag.urlTvg)) {
        return fail("read error (url-tvg)");
    }

    m3u::Stats &st = out.diag.stats;
    uint8_t header = 0;
    int64_t savedBytes = 0, savedLines = 0;
    if (!r.i64(savedBytes) || !r.i64(savedLines) || !r.u8(header)) {
        return fail("read error");
    }
    st.bytes = (size_t) savedBytes;
    st.lines = (int) savedLines;
    st.header = header != 0;
    uint32_t v = 0;
    #define READ_U32(field) do { if (!r.u32(v)) { return fail("read error"); } st.field = (int) v; } while (0)
    READ_U32(extinf); READ_U32(plainUrls); READ_U32(channels); READ_U32(groups); READ_U32(uncategorized);
    READ_U32(withLogo); READ_U32(withoutLogo); READ_U32(withTvgId); READ_U32(userAgents); READ_U32(hls);
    READ_U32(https); READ_U32(otherProtocols); READ_U32(duplicateIds); READ_U32(missingUrl); READ_U32(invalidUrl);
    READ_U32(overLimit); READ_U32(malformedExtinf); READ_U32(unknownDirectives);
    #undef READ_U32

    if (!getParse(r, out.diag.movieParse, v) || !getParse(r, out.diag.seriesParse, v) || !r.f64(out.diag.movieParseMs)
        || !r.f64(out.diag.seriesParseMs)) {
        return fail("read error (parse stats)");
    }

    m3u::Classification &cf = out.diag.classify;
    #define READ_CF(field) do { if (!r.u32(v)) { return fail("read error"); } cf.field = (int) v; } while (0)
    READ_CF(live); READ_CF(movie); READ_CF(series); READ_CF(seriesFound); READ_CF(seriesUnmarked);
    #undef READ_CF
    auto getGroups = [&r](std::vector<std::pair<std::string, int>> &groups) {
        uint32_t n = 0;
        if (!r.u32(n) || n > CatalogStore::MAX_COUNT) {
            return false;
        }
        groups.clear();
        groups.reserve(std::min<uint32_t>(n, 100000));
        for (uint32_t i = 0; i < n; i++) {
            std::pair<std::string, int> g;
            if (!r.pair(g)) {
                return false;
            }
            groups.push_back(std::move(g));
        }
        return true;
    };
    if (!getGroups(cf.movieGroups) || !getGroups(cf.seriesGroups) || !getGroups(cf.liveGroups)) {
        return fail("read error (group tallies)");
    }
    if (!r.u32(v)) {
        return fail("read error");
    }
    out.diag.categories = (int) v;

    auto getSection = [&r](auto &cats, auto &items, auto &&getItem, int64_t minItemBytes) {
        uint32_t catCount = 0, itemCount = 0;
        if (!r.count(catCount, 12) || !r.count(itemCount, minItemBytes)) {
            return false;
        }
        cats.clear();
        items.clear();
        cats.reserve(std::min<uint32_t>(catCount, 50000));
        items.reserve(std::min<uint32_t>(itemCount, 200000));
        for (uint32_t i = 0; i < catCount; i++) {
            iptv::Category c;
            if (!getCategory(r, c)) {
                return false;
            }
            cats.push_back(std::move(c));
        }
        for (uint32_t i = 0; i < itemCount; i++) {
            typename std::remove_reference<decltype(items)>::type::value_type it;
            if (!getItem(r, it)) {
                return false;
            }
            items.push_back(std::move(it));
        }
        return true;
    };

    if (!getSection(out.liveCategories, out.liveChannels, getLive, 24)) {
        return fail("read error (live section)");
    }
    if (!getSection(out.movieCategories, out.movieItems, getMovie, 28)) {
        return fail("read error (movies section)");
    }
    if (!getSection(out.seriesCategories, out.seriesItems, getSeries, 80)) {
        return fail("read error (series section)");
    }

    uint32_t infoCount = 0;
    uint64_t episodeTotal = 0;
    if (!r.count(infoCount, 80) || !r.u64(episodeTotal) || episodeTotal > CatalogStore::MAX_COUNT * 4ull) {
        return fail("read error (series info header)");
    }
    out.seriesInfo.clear();
    out.seriesInfo.reserve(std::min<uint32_t>(infoCount, 50000));
    uint64_t episodesSeen = 0;
    for (uint32_t i = 0; i < infoCount; i++) {
        auto si = std::make_shared<iptv::SeriesInfo>();
        if (!r.str(si->seriesId) || !getSeries(r, si->series)) {
            return fail("read error (series info)");
        }
        uint32_t seasonCount = 0;
        if (!r.count(seasonCount, 4)) {
            return fail("read error (seasons)");
        }
        si->seasons.reserve(std::min<uint32_t>(seasonCount, 256));
        for (uint32_t j = 0; j < seasonCount; j++) {
            iptv::Season season;
            uint32_t episodes = 0;
            if (!r.u32(v)) {
                return fail("read error (season number)");
            }
            season.number = (int) (int32_t) v;
            if (!r.str(season.name) || !r.str(season.cover) || !r.count(episodes, 32)) {
                return fail("read error (season)");
            }
            season.episodes.reserve(std::min<uint32_t>(episodes, 4000));
            for (uint32_t k = 0; k < episodes; k++) {
                iptv::Episode ep;
                if (!r.str(ep.id)) {
                    return fail("read error (episode id)");
                }
                if (!r.u32(v)) {
                    return fail("read error");
                }
                ep.season = (int) (int32_t) v;
                if (!r.u32(v)) {
                    return fail("read error");
                }
                ep.number = (int) (int32_t) v;
                if (!r.str(ep.title) || !r.str(ep.extension) || !r.str(ep.image) || !r.str(ep.plot)) {
                    return fail("read error (episode strings)");
                }
                if (!r.u32(v)) {
                    return fail("read error");
                }
                ep.durationSeconds = (int) (int32_t) v;
                double rating = 0;
                if (!r.f64(rating)) {
                    return fail("read error");
                }
                ep.rating = (float) rating;
                if (!getMedia(r, ep.media) || !r.str(ep.cmd)) {
                    return fail("read error (episode media)");
                }
                if (!r.u32(v)) {
                    return fail("read error");
                }
                ep.linkSeries = (int) (int32_t) v;
                if (!r.str(ep.url)) {
                    return fail("read error (episode url)");
                }
                episodesSeen++;
                if (episodesSeen > episodeTotal * 2 + 1024) {
                    return fail("episode count overflow");
                }
                season.episodes.push_back(std::move(ep));
            }
            si->seasons.push_back(std::move(season));
        }
        if (si->seriesId.empty()) {
            return fail("read error (empty series id)");
        }
        out.seriesInfo.emplace(si->seriesId, std::move(si));
    }
    if (episodesSeen != episodeTotal) {
        return fail("episode count mismatch");
    }

    // trailer: 8 raw bytes holding the FNV-1a of everything before them (read without feeding the hash)
    uint64_t payloadFnv = r.fnv;
    if ((int64_t) (r.at + 8) != r.fileBytes) {
        return fail("trailing or truncated file");
    }
    r.fail();
    // re-open and read the trailer manually without touching the running hash
    FILE *tf = std::fopen(target.c_str(), "rb");
    if (tf == nullptr) {
        return fail("read error (trailer)");
    }
    if (std::fseek(tf, (long) r.at, SEEK_SET) != 0) {
        std::fclose(tf);
        return fail("read error (trailer)");
    }
    uint8_t trailer[8];
    size_t got = std::fread(trailer, 1, 8, tf);
    std::fclose(tf);
    if (got != 8) {
        return fail("read error (trailer)");
    }
    uint64_t stored = 0;
    for (int i = 7; i >= 0; i--) {
        stored = (stored << 8) | trailer[i];
    }
    if (stored != payloadFnv) {
        return fail("checksum mismatch");
    }
    return true;
}
