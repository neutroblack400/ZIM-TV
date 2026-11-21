#include "m3u_service.h"
#include "../i18n/i18n.h"
#include "../network/http.h"
#include "../platform/clock.h"
#include "../platform/fs.h"
#include "../platform/log.h"
#include "../storage/catalog_cache.h"
#include "../storage/catalog_store.h"

#include <cstdio>
#include <cstring>

using namespace iptv;

namespace {
    // an XMLTV guide (xmltv.php / an url-tvg document) pasted as a playlist is not a channel list: it is refused
    // with a clear reason instead of a parse error or an out-of-memory download
    bool looksLikeXmltv(const std::string &body) {
        size_t i = 0;
        if (body.size() >= 3 && (unsigned char) body[0] == 0xEF && (unsigned char) body[1] == 0xBB &&
            (unsigned char) body[2] == 0xBF) {
            i = 3;   // UTF-8 BOM
        }
        i = body.find_first_not_of(" \t\r\n", i);
        if (i == std::string::npos) {
            return false;
        }
        const std::string head = body.substr(i, 4096);
        if (head.compare(0, 3, "<tv") == 0) {
            return true;
        }
        const bool xmlDecl = head.compare(0, 5, "<?xml") == 0;
        return xmlDecl && head.find("<tv") != std::string::npos
               && (head.find("<programme") != std::string::npos || head.find("<channel") != std::string::npos);
    }

    void logClassifyGroups(const char *tag, const char *what, const std::vector<std::pair<std::string, int>> &groups) {
        if (groups.empty()) {
            LOG_I(tag, "%s: (none)", what);
            return;
        }
        std::string line;
        for (const auto &g: groups) {
            std::string name;
            name.reserve(g.first.size());
            for (unsigned char c: g.first) {
                if (c >= 32 && c < 127 && c != '=' && c != ',') {
                    name.push_back((char) c);   // printable ASCII only; the rest of the name is dropped
                }
            }
            if (name.size() > 40) {
                name.resize(40);
            }
            char part[64];
            snprintf(part, sizeof(part), "%s%s=%d", line.empty() ? "" : ", ", name.c_str(), g.second);
            if (line.size() + strlen(part) > 1500) {
                line += ", ...";
                break;
            }
            line += part;
        }
        LOG_I(tag, "%s: %s", what, line.c_str());
    }

    // what the log may say about a playlist location: host + file name, never credentials / tokens
    std::string where(const Profile &p) {
        std::string shown = m3u::displayUrl(p.playlistUrl);
        return shown.empty() ? "?" : shown;
    }
}

std::string M3uService::errorText(m3u::Error error) {
    switch (error) {
        case m3u::Error::Empty:
            return i18n::tr("m3u.error_empty");
        case m3u::Error::HlsMedia:
            return i18n::tr("m3u.error_hls");
        case m3u::Error::Utf16:
            return i18n::tr("m3u.error_encoding");
        default:
            return i18n::tr("m3u.error_not_playlist");
    }
}

bool M3uService::validLocation(const std::string &location) {
    return m3u::isHttpUrl(location) || !m3u::localPlaylistPath(location).empty();
}

bool M3uService::saveBody(const std::string &dataDir, const std::string &profileId, const std::string &body,
                          int channels, int64_t now) {
    std::string err;
    if (!CatalogCache(dataDir).save(profileId, CACHE_NAME, body, now, channels, &err)) {
        LOG_W("m3u", "playlist not saved: %s", err.c_str());
        return false;
    }
    return true;
}

CancelToken M3uService::load(const Profile &profile, const std::string &dataDir, Source source, bool save,
                             Callback callback, bool keepBody) {
    auto out = std::make_shared<Outcome>();
    Profile p = profile;
    bool fromCache = source == Source::Cache;
    return jobs.submit(fromCache ? JobPriority::High : JobPriority::Normal, fromCache ? "m3u (cache)" : "m3u",
                       [=](const CancelToken &token) {
        auto body = std::make_shared<std::string>();
        CatalogCache cache(dataDir);
        CatalogCache::Meta meta;
        double fetchSeconds = 0;
        if (fromCache) {
            if (!cache.load(p.id, CACHE_NAME, *body, meta)) {
                out->message = i18n::tr("m3u.no_saved_copy");
                return;
            }
        } else {
            std::string local = m3u::localPlaylistPath(p.playlistUrl);
            double t0 = clockx::monotonic();
            if (!local.empty()) {
                std::string err;
                if (!fs::readFile(local, *body, MAX_BYTES, &err)) {
                    out->networkFailure = true;
                    out->message = i18n::tr("m3u.local_unreadable");
                    LOG_W("m3u", "local playlist %s: %s", where(p).c_str(), err.c_str());
                    return;
                }
            } else if (m3u::isHttpUrl(p.playlistUrl)) {
                http::Request req;
                req.url = p.playlistUrl;
                req.cancel = token.shared();
                req.connectTimeoutMs = 10000;
                req.totalTimeoutMs = 300000;   // hundreds of thousands of entries on a slow server
                req.stallTimeoutS = 30;
                req.maxBytes = MAX_BYTES;
                req.userAgent = p.userAgent;
                http::Response resp = http::get(req);
                if (token.canceled()) {
                    return;
                }
                if (!resp.ok()) {
                    out->networkFailure = true;
                    out->message = http::describe(resp);
                    LOG_W("m3u", "download %s: %s (%s) after %.1fs", where(p).c_str(), out->message.c_str(),
                          resp.detail.c_str(), resp.seconds);
                    return;
                }
                *body = std::move(resp.body);
            } else {
                out->message = i18n::tr("m3u.error_location");
                return;
            }
            fetchSeconds = clockx::monotonic() - t0;
            meta.savedAt = clockx::unixNow();
        }
        if (token.canceled()) {
            return;
        }
        if (looksLikeXmltv(*body)) {
            out->message = i18n::tr("m3u.epg_xmltv");
            LOG_W("m3u", "%s%s: XMLTV guide, not a channel playlist", where(p).c_str(),
                  fromCache ? " (saved copy)" : "");
            return;
        }

        // one pass over the body we already hold, doubling as the fingerprint of this saved copy
        const uint64_t bodyFingerprint = CatalogCache::checksum(*body);

        // fast path: the processed catalog of this exact saved copy, rebuilt on the spot
        if (fromCache) {
            CatalogStore store(dataDir);
            CatalogStore::Result dr;
            std::string storeErr;
            double t0 = clockx::monotonic();
            if (store.load(p.id, bodyFingerprint, dr, &storeErr)) {
                auto catalog = std::make_shared<iptv::LiveCatalog>();
                catalog->assign(std::move(dr.liveCategories), std::move(dr.liveChannels));
                auto moviesCatalog = std::make_shared<iptv::MovieCatalog>();
                moviesCatalog->prepare(std::move(dr.movieCategories), std::move(dr.movieItems));
                auto seriesCatalog = std::make_shared<iptv::SeriesCatalog>();
                seriesCatalog->prepare(std::move(dr.seriesCategories), std::move(dr.seriesItems));
                const double buildMs = (clockx::monotonic() - t0) * 1000.0;
                out->info.stats = dr.diag.stats;
                out->info.classify = dr.diag.classify;
                out->info.urlTvg = dr.diag.urlTvg;
                out->info.categories = dr.diag.categories;
                moviesCatalog->diagnostics().parse = dr.diag.movieParse;
                moviesCatalog->diagnostics().parseMs = dr.diag.movieParseMs;
                seriesCatalog->diagnostics().parse = dr.diag.seriesParse;
                seriesCatalog->diagnostics().parseMs = dr.diag.seriesParseMs;
                out->info.parseMs = 0;   // the list was not re-parsed
                out->info.indexMs = buildMs;
                out->info.indexBytes = catalog->searchIndex().memoryBytes()
                                       + moviesCatalog->diagnostics().indexBytes
                                       + seriesCatalog->diagnostics().indexBytes;
                out->info.fromCache = true;
                out->info.savedAt = meta.savedAt;
                catalog->fromCache = true;
                catalog->loadedAt = meta.savedAt;
                moviesCatalog->fromCache = true;
                moviesCatalog->savedAt = meta.savedAt;
                seriesCatalog->fromCache = true;
                seriesCatalog->savedAt = meta.savedAt;
                LOG_I("m3u", "classify: live %d, movies %d, series %d (%d series, %d episodes without a marker)",
                      out->info.classify.live, out->info.classify.movie, out->info.classify.series,
                      out->info.classify.seriesFound, out->info.classify.seriesUnmarked);
                logClassifyGroups("m3u", "movie groups", out->info.classify.movieGroups);
                logClassifyGroups("m3u", "series groups", out->info.classify.seriesGroups);
                logClassifyGroups("m3u", "live groups", out->info.classify.liveGroups);
                LOG_I("m3u", "%s (saved copy, catalog store): %d channels in %d groups (%zu KiB), loaded in %.0f ms",
                      where(p).c_str(), (int) catalog->channels().size(), dr.diag.categories,
                      (body->size() + 1023) / 1024, buildMs);
                out->movies = moviesCatalog;
                out->series = seriesCatalog;
                out->seriesInfo = std::make_shared<const std::unordered_map<std::string, std::shared_ptr<const iptv::SeriesInfo>>>(
                    std::move(dr.seriesInfo));
                out->catalog = catalog;
                out->ok = true;
                return;
            }
            LOG_I("m3u", "catalog store unusable (%s); rebuilding from the saved copy", storeErr.c_str());
        }

        auto split = std::make_shared<m3u::Classified>();
        m3u::Error e = m3u::buildClassified(p.id, *body, *split, out->info);
        auto catalog = std::make_shared<iptv::LiveCatalog>(std::move(split->live));
        auto moviesCatalog = std::make_shared<iptv::MovieCatalog>(std::move(split->movies));
        auto seriesCatalog = std::make_shared<iptv::SeriesCatalog>(std::move(split->series));
        const m3u::Stats &st = out->info.stats;
        if (e != m3u::Error::None) {
            out->message = errorText(e);
            LOG_W("m3u", "%s%s: not a usable channel playlist (error %d, %zu bytes, %d lines, %d entries skipped)",
                  where(p).c_str(), fromCache ? " (saved copy)" : "", (int) e, st.bytes, st.lines, st.skipped());
            if (fromCache) {
                cache.remove(p.id, CACHE_NAME);   // never read a broken saved copy again
            }
            return;
        }
        if (fromCache && meta.items >= 0 && meta.items != st.channels) {
            LOG_W("m3u", "saved playlist: %d channels parsed, %d saved: discarded", st.channels, meta.items);
            cache.remove(p.id, CACHE_NAME);
            out->message = i18n::tr("m3u.no_saved_copy");
            return;
        }
        if (!fromCache && save) {
            std::string err;
            double t0 = clockx::monotonic();
            if (!cache.save(p.id, CACHE_NAME, *body, meta.savedAt, st.channels, &err)) {
                LOG_W("m3u", "playlist not saved: %s", err.c_str());
            } else {
                LOG_I("m3u", "playlist saved in %.0f ms", (clockx::monotonic() - t0) * 1000);
            }
        }
        // keep the processed-catalog store in step with the saved copy: after a refresh, and whenever
        // the store was missing or rejected (heal), so the next open is instant
        if ((!fromCache && save) || fromCache) {
            CatalogStore store(dataDir);
            CatalogStore::Diagnostics diag;
            diag.classify = out->info.classify;
            diag.stats = out->info.stats;
            diag.urlTvg = out->info.urlTvg;
            diag.categories = out->info.categories;
            diag.movieParse = moviesCatalog->diagnostics().parse;
            diag.seriesParse = seriesCatalog->diagnostics().parse;
            diag.movieParseMs = moviesCatalog->diagnostics().parseMs;
            diag.seriesParseMs = seriesCatalog->diagnostics().parseMs;
            std::string err;
            double t0 = clockx::monotonic();
            if (!store.save(p.id, bodyFingerprint, meta.savedAt, *catalog, *moviesCatalog, *seriesCatalog,
                            split->seriesInfo, diag, &err)) {
                LOG_W("m3u", "catalog store not saved: %s", err.c_str());
            } else {
                LOG_I("m3u", "catalog store saved in %.0f ms", (clockx::monotonic() - t0) * 1000);
            }
        }
        out->info.fromCache = fromCache;
        out->info.savedAt = meta.savedAt;
        catalog->fromCache = fromCache;
        catalog->loadedAt = meta.savedAt;
        moviesCatalog->fromCache = fromCache;
        moviesCatalog->savedAt = meta.savedAt;
        seriesCatalog->fromCache = fromCache;
        seriesCatalog->savedAt = meta.savedAt;
        LOG_I("m3u", "classify: live %d, movies %d, series %d (%d series, %d episodes without a marker)",
              out->info.classify.live, out->info.classify.movie, out->info.classify.series,
              out->info.classify.seriesFound, out->info.classify.seriesUnmarked);
        logClassifyGroups("m3u", "movie groups", out->info.classify.movieGroups);
        logClassifyGroups("m3u", "series groups", out->info.classify.seriesGroups);
        logClassifyGroups("m3u", "live groups", out->info.classify.liveGroups);
        LOG_I("m3u", "%s%s: %d channels in %d groups (%zu KiB%s), %d with logo, %d skipped (missing URL %d, "
                     "invalid URL %d, over limit %d), %d malformed EXTINF, %d unknown directives, %d HLS, %d HTTPS, "
                     "%d other protocols, %d own User-Agent, %d duplicate ids; parse %.0f ms, index %.0f ms",
              where(p).c_str(), fromCache ? " (saved copy)" : "", st.channels, st.groups, (body->size() + 1023) / 1024,
              fromCache ? "" : diag::format(", %.1fs download", fetchSeconds).c_str(), st.withLogo, st.skipped(),
              st.missingUrl, st.invalidUrl, st.overLimit, st.malformedExtinf, st.unknownDirectives, st.hls, st.https,
              st.otherProtocols, st.userAgents, st.duplicateIds, out->info.parseMs, out->info.indexMs);
        out->movies = moviesCatalog;
        out->series = seriesCatalog;
        out->seriesInfo = std::make_shared<const std::unordered_map<std::string, std::shared_ptr<const iptv::SeriesInfo>>>(
            std::move(split->seriesInfo));
        out->catalog = catalog;
        out->ok = true;
        if (keepBody) {
            out->body = body;
        }
    }, [out, callback]() {
        callback(*out);
    });
}
