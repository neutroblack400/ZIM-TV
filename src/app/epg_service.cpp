#include "epg_service.h"

#include <cstdio>
#include <cstring>
#include <zlib.h>

#include "../i18n/i18n.h"
#include "../network/http.h"
#include "../platform/clock.h"
#include "../platform/fs.h"
#include "../platform/log.h"

namespace {

const char MAGIC[8] = {'Z', 'T', 'V', 'G', 'E', 'P', 'G', '1'};
const uint32_t VERSION = 1;

uint64_t fnv1a64(const void *data, size_t size) {
    const unsigned char *p = static_cast<const unsigned char *>(data);
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < size; i++) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

void put16(std::string &out, uint16_t v) {
    out.push_back((char) (v & 0xFF));
    out.push_back((char) ((v >> 8) & 0xFF));
}

void put32(std::string &out, uint32_t v) {
    for (int i = 0; i < 4; i++) {
        out.push_back((char) ((v >> (8 * i)) & 0xFF));
    }
}

void put64(std::string &out, uint64_t v) {
    for (int i = 0; i < 8; i++) {
        out.push_back((char) ((v >> (8 * i)) & 0xFF));
    }
}

struct Cursor {
    const unsigned char *p, *end;

    bool take(size_t n, const unsigned char **out) {
        if ((size_t) (end - p) < n) {
            p = end;
            return false;
        }
        *out = p;
        p += n;
        return true;
    }

    bool u16(uint16_t &v) {
        const unsigned char *b;
        if (!take(2, &b)) {
            return false;
        }
        v = (uint16_t) b[0] | ((uint16_t) b[1] << 8);
        return true;
    }

    bool u32(uint32_t &v) {
        const unsigned char *b;
        if (!take(4, &b)) {
            return false;
        }
        v = (uint32_t) b[0] | ((uint32_t) b[1] << 8) | ((uint32_t) b[2] << 16) | ((uint32_t) b[3] << 24);
        return true;
    }

    bool u64(uint64_t &v) {
        const unsigned char *b;
        if (!take(8, &b)) {
            return false;
        }
        v = 0;
        for (int i = 0; i < 8; i++) {
            v |= (uint64_t) b[i] << (8 * i);
        }
        return true;
    }

    bool i64(int64_t &v) {
        uint64_t u;
        if (!u64(u)) {
            return false;
        }
        v = (int64_t) u;   // defined modulo 2^64
        return true;
    }

    bool str(size_t n, std::string &out) {
        const unsigned char *b;
        if (!take(n, &b)) {
            return false;
        }
        out.assign(reinterpret_cast<const char *>(b), n);
        return true;
    }
};

std::string serialize(const epg::Guide &guide) {
    std::string out;
    out.reserve(1 << 22);
    out.append(MAGIC, 8);
    put32(out, VERSION);
    put64(out, (uint64_t) guide.savedAt);
    put32(out, (uint32_t) guide.byChannel.size());
    uint64_t total = 0;
    for (const auto &[id, vec] : guide.byChannel) {
        total += vec.size();
    }
    put32(out, (uint32_t) total);
    for (const auto &[id, vec] : guide.byChannel) {
        put16(out, (uint16_t) id.size());
        out.append(id);
        put32(out, (uint32_t) vec.size());
        for (const epg::Program &p : vec) {
            put64(out, (uint64_t) p.start);
            put64(out, (uint64_t) p.stop);
            put16(out, (uint16_t) p.title.size());
            out.append(p.title);
            put16(out, (uint16_t) p.desc.size());
            out.append(p.desc);
        }
    }
    uint64_t sum = fnv1a64(out.data(), out.size());
    put64(out, sum);
    return out;
}

bool deserialize(const std::string &data, epg::Guide &guide) {
    guide = epg::Guide();
    if (data.size() < 8 + 4 + 8 + 4 + 4 + 8) {
        return false;
    }
    const unsigned char *d = reinterpret_cast<const unsigned char *>(data.data());
    if (std::memcmp(d, MAGIC, 8) != 0) {
        return false;
    }
    const size_t bodySize = data.size() - 8;
    uint64_t storedSum = 0;
    for (int i = 0; i < 8; i++) {
        storedSum |= (uint64_t) d[bodySize + i] << (8 * i);
    }
    if (fnv1a64(d, bodySize) != storedSum) {
        return false;
    }
    Cursor c{d + 8, d + bodySize};
    uint32_t version = 0;
    uint64_t savedAt = 0;
    uint32_t channelCount = 0, programmeCount = 0;
    if (!c.u32(version) || version != VERSION) {
        return false;
    }
    if (!c.u64(savedAt) || !c.u32(channelCount) || !c.u32(programmeCount)) {
        return false;
    }
    if (channelCount > 400000 || programmeCount > 4000000) {
        return false;   // corrupt: implausible counts
    }
    guide.savedAt = (int64_t) savedAt;
    guide.byChannel.reserve(channelCount);
    int64_t kept = 0;
    for (uint32_t ch = 0; ch < channelCount; ch++) {
        uint16_t idLen = 0;
        if (!c.u16(idLen) || idLen == 0 || idLen > 256) {
            return false;
        }
        std::string id;
        if (!c.str(idLen, id)) {
            return false;
        }
        uint32_t count = 0;
        if (!c.u32(count) || count > 200) {
            return false;
        }
        std::vector<epg::Program> vec;
        vec.reserve(count);
        for (uint32_t n = 0; n < count; n++) {
            epg::Program p;
            if (!c.i64(p.start) || !c.i64(p.stop)) {
                return false;
            }
            if (p.stop <= p.start) {
                return false;
            }
            uint16_t tlen = 0, dlen = 0;
            if (!c.u16(tlen) || !c.str(tlen, p.title) || !c.u16(dlen) || !c.str(dlen, p.desc)) {
                return false;
            }
            vec.push_back(std::move(p));
            kept++;
        }
        guide.byChannel.emplace(std::move(id), std::move(vec));
    }
    guide.programmes = kept;
    return c.p == c.end;
}

bool isGzipFile(const std::string &path) {
    FILE *f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        return false;
    }
    unsigned char magic[2] = {0, 0};
    size_t n = std::fread(magic, 1, 2, f);
    std::fclose(f);
    return n == 2 && magic[0] == 0x1F && magic[1] == 0x8B;
}

bool gunzipTo(const std::string &src, const std::string &dst) {
    gzFile in = gzopen(src.c_str(), "rb");
    if (in == nullptr) {
        return false;
    }
    FILE *out = std::fopen(dst.c_str(), "wb");
    if (out == nullptr) {
        gzclose(in);
        return false;
    }
    unsigned char buf[1 << 16];
    for (;;) {
        int n = gzread(in, buf, sizeof(buf));
        if (n == 0) {
            break;
        }
        if (n < 0) {
            std::fclose(out);
            gzclose(in);
            std::remove(dst.c_str());
            return false;
        }
        if (std::fwrite(buf, 1, (size_t) n, out) != (size_t) n) {
            std::fclose(out);
            gzclose(in);
            std::remove(dst.c_str());
            return false;
        }
    }
    std::fclose(out);
    gzclose(in);
    return true;
}

}

bool EpgService::safeName(const std::string &s) {
    if (s.empty() || s.size() > 64) {
        return false;
    }
    for (char c : s) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}

std::string EpgService::cachePath(const std::string &profileId, const std::string &dataDir) {
    if (!safeName(profileId)) {
        return std::string();
    }
    return fs::join(fs::join(fs::join(dataDir, "cache"), "epg"), profileId + ".epg");
}

bool EpgService::hasFresh(const std::string &profileId, const std::string &dataDir, int64_t maxAgeSeconds) {
    std::string path = cachePath(profileId, dataDir);
    if (path.empty()) {
        return false;
    }
    FILE *f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        return false;
    }
    unsigned char hdr[8 + 4 + 8];
    size_t n = std::fread(hdr, 1, sizeof(hdr), f);
    std::fclose(f);
    if (n != sizeof(hdr) || std::memcmp(hdr, MAGIC, 8) != 0) {
        return false;
    }
    uint32_t version = (uint32_t) hdr[8] | ((uint32_t) hdr[9] << 8) | ((uint32_t) hdr[10] << 16) | ((uint32_t) hdr[11] << 24);
    if (version != VERSION) {
        return false;
    }
    uint64_t savedAt = 0;
    for (int i = 0; i < 8; i++) {
        savedAt |= (uint64_t) hdr[12 + i] << (8 * i);
    }
    return clockx::unixNow() - (int64_t) savedAt <= maxAgeSeconds;
}

std::shared_ptr<epg::Guide> EpgService::loadCached(const std::string &profileId, const std::string &dataDir) {
    std::string path = cachePath(profileId, dataDir);
    if (path.empty()) {
        return nullptr;
    }
    std::string data;
    if (!fs::readFile(path, data, MAX_CACHE_BYTES)) {
        return nullptr;
    }
    auto g = std::make_shared<epg::Guide>();
    if (!deserialize(data, *g)) {
        LOG_W("epg", "saved guide is unreadable or damaged; ignoring (%zu bytes)", data.size());
        return nullptr;
    }
    return g;
}

CancelToken EpgService::refresh(const std::string &profileId, const std::string &url,
                                const std::unordered_set<std::string> &wanted,
                                const std::string &dataDir, Callback callback, bool force) {
    auto out = std::make_shared<Outcome>();
    std::string path = cachePath(profileId, dataDir);
    if (path.empty()) {
        out->message = i18n::tr("m3u.error_location");
        callback(*out);
        return CancelToken();
    }
    // a fresh saved guide is served without touching the network
    if (!force) {
        std::shared_ptr<epg::Guide> cached = loadCached(profileId, dataDir);
        if (cached != nullptr && clockx::unixNow() - cached->savedAt <= REFRESH_AFTER &&
            coversWantedChannels(*cached, wanted)) {
            LOG_I("epg", "guide from the saved copy (%lld programmes, saved %llds ago)",
                  (long long) cached->programmes, (long long) (clockx::unixNow() - cached->savedAt));
            out->ok = true;
            out->fromCache = true;
            out->savedAt = cached->savedAt;
            out->guide = std::move(cached);
            callback(*out);
            return CancelToken();
        }
    }
    return jobs.submit(JobPriority::High, "epg refresh",
                       [profileId, url, wanted, dataDir, path, out](const CancelToken &token) {
        std::string dir = fs::join(fs::join(dataDir, "cache"), "epg");
        std::string err;
        if (!fs::ensureDir(dir, &err)) {
            out->message = err;
            return;
        }
        std::string tmp = path + ".tmp";
        std::string dec = path + ".dec";
        auto clean = [&] {
            fs::removeFile(tmp);
            fs::removeFile(dec);
        };
        try {
            // 1. download to a temp file (streaming; decompressed by curl when the server answers gzip)
            http::Request req;
            req.url = url;
            req.cancel = token.shared();
            req.connectTimeoutMs = 10000;
            req.totalTimeoutMs = 900000;   // 15 minutes: huge guides on slow servers
            req.stallTimeoutS = 60;
            req.maxBytes = MAX_DOWNLOAD_BYTES;
            req.retries = 1;
            http::Response resp = http::getToFile(req, tmp);
            if (token.canceled()) {
                clean();
                return;
            }
            if (!resp.ok()) {
                out->networkFailure = true;
                out->message = http::describe(resp);
                LOG_W("epg", "guide download failed: %s (%s) after %.1fs", out->message.c_str(),
                      resp.detail.c_str(), resp.seconds);
                // fall back to any saved guide, however old
                if (std::shared_ptr<epg::Guide> cached = loadCached(profileId, dataDir)) {
                    LOG_I("epg", "serving the saved guide (saved %llds ago)",
                          (long long) (clockx::unixNow() - cached->savedAt));
                    out->ok = true;
                    out->fromCache = true;
                    out->staleCache = true;
                    out->savedAt = cached->savedAt;
                    out->guide = std::move(cached);
                }
                clean();
                return;
            }
            LOG_I("epg", "guide downloaded: %llu bytes in %.1fs", (unsigned long long) resp.bodyBytes, resp.seconds);

            // 2. a raw .gz file (not transport-encoded) is decompressed first
            std::string parsePath = tmp;
            if (isGzipFile(tmp)) {
                if (!gunzipTo(tmp, dec)) {
                    out->message = i18n::tr("net.failed");
                    clean();
                    return;
                }
                fs::removeFile(tmp);
                parsePath = dec;
                LOG_I("epg", "guide answer is a gzip file; decompressed");
            }

            // 3. parse (streaming): keep only the channels of the loaded catalog
            int64_t now = clockx::unixNow();
            int64_t ws = now - WINDOW_PAST;
            int64_t we = now + WINDOW_FUTURE;
            epg::ParseStats stats;
            std::shared_ptr<epg::Guide> guide;
            try {
                guide = std::make_shared<epg::Guide>(epg::parseFile(
                    parsePath, [&wanted](const std::string &id) { return wanted.find(id) != wanted.end(); },
                    ws, we, 120000, &stats));
            } catch (const std::exception &e) {
                LOG_W("epg", "guide parse failed: %s", e.what());
                out->message = i18n::tr("net.failed");
                clean();
                return;
            }
            // Preserve coverage even for wanted channels with no programmes in this window. This lets a fresh
            // cache distinguish a parsed empty result from an ID omitted by an older catalog-filtered cache.
            for (const std::string &id : wanted) {
                guide->byChannel.emplace(id, std::vector<epg::Program>());
            }
            guide->savedAt = now;
            clean();
            if (guide->programmes == 0) {
                LOG_W("epg", "guide parsed with no matching programmes (%lld programmes seen)",
                      (long long) stats.programmesSeen);
            }
            LOG_I("epg", "guide parsed: %lld kept / %lld seen, %zu channels, %s",
                  (long long) stats.programmesKept, (long long) stats.programmesSeen, guide->byChannel.size(),
                  guide->truncated ? "truncated at the global cap" : "full window");

            // 4. save the compact cache for the next start
            std::string blob = serialize(*guide);
            std::string werr;
            if (blob.size() <= MAX_CACHE_BYTES && !fs::writeFileReplace(path, blob, &werr)) {
                LOG_W("epg", "guide not cached: %s", werr.c_str());
            } else {
                LOG_I("epg", "guide cached (%zu bytes)", blob.size());
            }
            out->ok = true;
            out->savedAt = now;
            out->guide = std::move(guide);
        } catch (const std::exception &e) {
            LOG_E("epg", "refresh failed: %s", e.what());
            out->message = i18n::tr("net.failed");
            clean();
        } catch (...) {
            LOG_E("epg", "refresh failed: unknown exception");
            out->message = i18n::tr("net.failed");
            clean();
        }
    },
                       [callback, out] { callback(*out); });
}
