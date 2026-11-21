#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_set>

#include "../core/url.h"

#include <mbedtls/x509_crt.h>

#include "image_key.h"

#include "image_loader.h"
#include "image_key.h"
#include "artwork_stats.h"
#include "tmdb_client.h"
#include "../network/http.h"
#include "../platform/clock.h"
#include "../platform/log.h"

namespace {
    const int MAX_UPLOADS_PER_FRAME = 2;         // texture creation is cheap, but never a burst per frame
    const size_t MEMORY_ENTRIES = 320;
    const int64_t MEMORY_BYTES = 48ll * 1024 * 1024;   // GPU bytes of all cached textures
    const size_t MAX_IMAGE_BYTES = 3u * 1024 * 1024;

    // A power-of-two RGBA page with the image in its top-left corner, uploaded once. Same texture type
    // and CPU->GPU path (lock, write the CPU copy, unlock = glTexSubImage2D) as the glyph atlas pages.
    // Afterwards the CPU copy is released: these textures are never locked again (GLTexture's destructor
    // only frees `pixels` when it is set).
    // exact (the large kinds): the texture is exactly the image, clamped to its edges - complete for a
    // non-power-of-two size in core GLES2 (images/art_sizes.h).
    std::shared_ptr<c2d::Texture> makeTexture(const images::Image &img, int64_t &bytes, bool exact,
                                              c2d::Vector2i &pageSize) {
        if (!img.valid()) {
            return nullptr;
        }
        images::art::Page page = images::art::page(img.w, img.h, exact);
        int tw = page.w;
        int th = page.h;
        auto *tex = new c2d::C2DTexture(c2d::Vector2f((float) tw, (float) th), c2d::Texture::Format::RGBA8);
        void *pixels = nullptr;
        int pitch = 0;
        if (!tex->available || tex->lock(nullptr, &pixels, &pitch) != 0 || pixels == nullptr || pitch < tw * 4) {
            delete tex;
            return nullptr;
        }
        auto *dst = (uint8_t *) pixels;
        memset(dst, 0, (size_t) pitch * (size_t) th);
        for (int y = 0; y < img.h; y++) {
            memcpy(dst + (size_t) y * (size_t) pitch, &img.rgba[(size_t) y * (size_t) img.w * 4], (size_t) img.w * 4);
        }
        tex->unlock();
        free(tex->pixels);
        tex->pixels = nullptr;
        if (exact) {
            glBindTexture(GL_TEXTURE_2D, tex->texID);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        bytes += page.bytes;
        pageSize = {tw, th};
        return std::shared_ptr<c2d::Texture>(tex);
    }

    std::string artKeyString(const images::ArtMeta &a) {
        return a.title + '|' + std::to_string(a.year) + '|' + std::to_string(a.subject);
    }
    std::string artKey(const images::ArtMeta &a) {
        return "art#" + images::cacheKey(artKeyString(a));
    }

    char kindChar(ImageKind k) {
        return (char) ('0' + (int) k);
    }

    // names for MBEDTLS_X509_BADCERT_* bits, so the console log shows e.g. [NOT_TRUSTED] without a lookup table
    std::string verifyFlagNames(uint32_t flags) {
        static const struct { uint32_t bit; const char *name; } k[] = {
            { MBEDTLS_X509_BADCERT_EXPIRED, "EXPIRED" }, { MBEDTLS_X509_BADCERT_REVOKED, "REVOKED" },
            { MBEDTLS_X509_BADCERT_CN_MISMATCH, "CN_MISMATCH" }, { MBEDTLS_X509_BADCERT_NOT_TRUSTED, "NOT_TRUSTED" },
            { MBEDTLS_X509_BADCERT_MISSING, "MISSING" }, { MBEDTLS_X509_BADCERT_OTHER, "OTHER" },
            { MBEDTLS_X509_BADCERT_FUTURE, "FUTURE" }, { MBEDTLS_X509_BADCERT_KEY_USAGE, "KEY_USAGE" },
            { MBEDTLS_X509_BADCERT_EXT_KEY_USAGE, "EXT_KEY_USAGE" }, { MBEDTLS_X509_BADCERT_BAD_MD, "BAD_MD" },
            { MBEDTLS_X509_BADCERT_BAD_PK, "BAD_PK" }, { MBEDTLS_X509_BADCERT_BAD_KEY, "BAD_KEY" },
        };
        std::string out;
        for (const auto &e: k) {
            if (flags & e.bit) {
                if (!out.empty()) out += "|";
                out += e.name;
            }
        }
        return out;
    }
}

// Texture pages are powers of two (makeTexture), so a box is chosen by the page it lands in:
//   Poster       170 x 255 -> 256 x 256 page, 256 KiB (as 140 x 210 was): the grid's 166 x 249 tile and the Home
//                card's 170 x 255 poster are drawn at ~1:1 instead of the 1.19x upscale of v2
//   PosterLarge  400 x 600 -> 512 x 1024, 2 MiB: the 320 x 480 movie poster, and the detail pages' poster-derived
//                backdrop
//   Backdrop / HeroPoster: exact-size textures, see images/art_sizes.h (1280 x 720, 3.52 MiB / 512 x 768, 1.5 MiB)
// Every texture counts against the one byte-bounded LRU (MEMORY_BYTES): bigger images mean fewer cached ones,
// never a higher peak.
std::vector<images::Box> ImageLoader::boxes(ImageKind kind) {
    switch (kind) {
        case ImageKind::Poster:
            return {{170, 255}};
        case ImageKind::PosterLarge:
            return {{400, 600}};
        case ImageKind::Backdrop:
            return {images::art::BACKDROP};
        case ImageKind::HeroPoster:
            return {images::art::HERO_POSTER};
        default:
            return {{96, 52}, {340, 190}};
    }
}

int ImageLoader::maxSourceDimension(ImageKind kind) {
    return kind == ImageKind::Backdrop ? images::art::BACKDROP_MAX_SOURCE : images::MAX_DIMENSION;
}

bool ImageLoader::exactPage(ImageKind kind) {
    return kind == ImageKind::Backdrop || kind == ImageKind::HeroPoster;
}

ImageLoader::ImageLoader(JobSystem &j, const std::string &cacheDir)
        : jobs(j), memory(MEMORY_ENTRIES, MEMORY_BYTES) {
    images::DiskCache::Config cfg;
    cfg.dir = cacheDir;
    cfg.maxBytes = 96ll * 1024 * 1024;     // posters are larger than logos
    cfg.trimTo = 80ll * 1024 * 1024;
    cfg.maxFileBytes = MAX_IMAGE_BYTES;
    disk = std::make_shared<images::DiskCache>(cfg);
    hostHealthShared = std::make_shared<HostHealth>();
}

ImageLoader::~ImageLoader() {
    *alive = false;
}

void ImageLoader::setEnabled(bool enabled) {
    if (enabled != on) {
        on = enabled;
        if (!on) {
            scheduler.want({}, now);
            prunePendingFetchUrls();
        }
        gen++;
    }
}

const std::string &ImageLoader::normalized(const std::string &raw) {
    auto it = normalizedUrls.find(raw);
    if (it != normalizedUrls.end()) {
        return it->second;
    }
    if (normalizedUrls.size() > 8192) {
        normalizedUrls.clear();
    }
    return normalizedUrls.emplace(raw, images::normalizeUrl(raw)).first->second;
}

std::string ImageLoader::key(ImageKind kind, const std::string &raw) {
    const std::string &u = normalized(raw);
    return u.empty() ? std::string() : kindChar(kind) + u;
}

void ImageLoader::prunePendingFetchUrls() {
    for (auto it = pendingFetchUrls.begin(); it != pendingFetchUrls.end();) {
        if (scheduler.state(it->first) != images::Scheduler::State::Queued) {
            it = pendingFetchUrls.erase(it);
        } else {
            ++it;
        }
    }
}

void ImageLoader::want(const std::vector<ImageRequest> &requests) {
    std::vector<std::string> list;
    if (on) {
        list.reserve(requests.size());
        for (const auto &r: requests) {
            // entries with title metadata resolve by content identity ("art#..."): they keep working
            // across URL rotations and across catalogs without claiming each other's images
            if (!r.art.title.empty()) {
                const std::string k = kindChar(r.kind) + artKey(r.art);
                if (artByUrl.size() > 8192) {
                    artByUrl.clear();
                }
                artByUrl[k] = r.art;
                if (!r.url.empty()) {
                    pendingFetchUrls[k] = r.url;
                } else {
                    pendingFetchUrls.erase(k);
                }
                list.push_back(k);
                continue;
            }
            if (!r.url.empty()) {
                const std::string &u = normalized(r.url);
                if (key(r.kind, r.url).empty()) {
                    continue;
                }
                list.push_back(key(r.kind, r.url));
            }
        }
    }
    scheduler.want(list, now);
    // Provider URLs are only needed until their queued item starts; the queue itself is bounded.
    prunePendingFetchUrls();
}

void ImageLoader::want(const std::vector<std::string> &logoUrls) {
    std::vector<ImageRequest> requests;
    requests.reserve(logoUrls.size());
    for (const auto &u: logoUrls) {
        requests.push_back({ImageKind::Logo, u});
    }
    want(requests);
}

bool ImageLoader::failed(ImageKind kind, const std::string &url) {
    if (!on || url.empty()) {
        return false;
    }
    std::string k = key(kind, url);
    return !k.empty() && scheduler.state(k) == images::Scheduler::State::Failed;
}

std::shared_ptr<ImageSet> ImageLoader::getKey(const std::string &k) {
    if (renderReported.size() > 4096) {
        renderReported.clear();
    }
    std::shared_ptr<ImageSet> *v = memory.get(k);
    if (v) {
        // once per key: the console shows the poster, not the placeholder
        if (renderReported.insert(k).second) {
            const std::string urlTag = k.size() > 1 ? k.substr(1) : k;
            LOG_I("images", "IMAGE_RENDER_RESULT hash=%s bound=1", images::cacheKey(urlTag).c_str());
        }
        return *v;
    }
    return nullptr;
}

std::shared_ptr<ImageSet> ImageLoader::get(ImageKind kind, const std::string &url) {
    if (!on || url.empty()) {
        return nullptr;
    }
    std::string k = key(kind, url);
    if (k.empty()) {
        return nullptr;
    }
    return getKey(k);
}

std::shared_ptr<ImageSet> ImageLoader::get(ImageKind kind, const std::string &url, const images::ArtMeta &art) {
    if (!on) {
        return nullptr;
    }
    if (!art.title.empty()) {
        return getKey(kindChar(kind) + artKey(art));
    }
    return get(kind, url);
}

bool ImageLoader::update(double t) {
    now = t;
    if (on) {
        for (std::string k = scheduler.next(); !k.empty(); k = scheduler.next()) {
            start(k);
        }
    }
    bool changed = false;
    for (int i = 0; i < MAX_UPLOADS_PER_FRAME && !decoded.empty(); i++) {
        std::shared_ptr<Pending> p = decoded.front();
        decoded.pop_front();
        finish(p);
        changed = true;
    }
    return changed;
}

void ImageLoader::start(const std::string &k) {
    auto pending = std::make_shared<Pending>();
    pending->key = k;
    pending->kind = (ImageKind) (k[0] - '0');
    pending->url = k.substr(1);
    {
        // art-space entries: the provider URL (when there was one) lives in pendingFetchUrls;
        // without it, the TMDB fallback is the only source (loadImage runs the art fallback)
        auto fetchIt = pendingFetchUrls.find(pending->key);
        if (fetchIt != pendingFetchUrls.end()) {
            pending->url = fetchIt->second;
            pendingFetchUrls.erase(fetchIt);
        } else if (pending->url.compare(0, 4, "art#") == 0) {
            pending->url = "";
        }
        auto it = artByUrl.find(pending->key);
        if (it == artByUrl.end()) {
            it = artByUrl.find(pending->url);
        }
        if (it != artByUrl.end()) {
            pending->art = it->second;
        }
    }
    std::shared_ptr<images::DiskCache> cache = disk;
    std::weak_ptr<bool> weak = alive;
    std::vector<images::Box> variantBoxes = boxes(pending->kind);
    int maxSource = maxSourceDimension(pending->kind);
    std::shared_ptr<HostHealth> hostHealth = hostHealthShared;
    {
        std::string reqHost;
        int reqPort = 0;
        bool reqKnown = url::hostPort(pending->url, reqHost, reqPort);
        LOG_I("images", "IMAGE_REQUEST hash=%s kind=%d host=%s", images::cacheKey(pending->key.substr(1)).c_str(),
              (int) pending->kind, reqKnown ? reqHost.c_str() : "artmeta");
    }
    // a local copy of the provider: the worker lambda must not capture `this` (the PS4 runtime has no thread
    // exit handling for captured members); the key itself is only read at request time
    std::function<std::string()> keyProvider = tmdbKeyProvider;
    std::shared_ptr<ImageLoader::TmdbMissCache> misses = tmdbMisses;
    jobs.submit(JobPriority::Low, "image", [hostHealth, pending, cache, variantBoxes, maxSource, keyProvider = std::move(keyProvider), misses = std::move(misses)](const CancelToken &token) {
        // the fetcher runs on worker threads: hosts that fail with network errors repeatedly this session are
        // skipped (the PS4 cannot reach them: the poster tiles show their title, the app stops asking)
        images::Fetcher fetch = [hostHealth, &token](const std::string &u) {
            std::string host;
            int port = 0;
            bool known = url::hostPort(u, host, port);
            const std::string reqTag = images::cacheKey(u);
            if (known && hostHealth->isBlocked(host, clockx::unixNow())) {
                images::FetchResponse f;
                f.networkError = true;
                f.detail = "host unavailable from this console (skipped this session)";
                LOG_I("images", "IMAGE_REQUEST hash=%s host=%s skipped=1 (host marked unavailable)", reqTag.c_str(), host.c_str());
                return f;
            }
            http::Request req;
            req.url = u;
            req.connectTimeoutMs = 6000;
            req.totalTimeoutMs = 15000;
            req.stallTimeoutS = 8;
            req.maxBytes = MAX_IMAGE_BYTES;
            req.cancel = token.shared();
            req.credentials = false;   // artwork: an https -> http redirect carries nothing secret
            req.retries = 0;           // images have their own back-off (images::loadImage)
            http::Response r = http::get(req);
            LOG_I("images", "IMAGE_HTTP_STATUS hash=%s host=%s status=%ld bytes=%llu curl=%ld content_type=%s",
                  reqTag.c_str(), known ? host.c_str() : "?", r.status, (unsigned long long) r.bodyBytes,
                  r.curlCode, r.contentType.c_str());
            LOG_I("images", "IMAGE_DOWNLOAD_BYTES hash=%s bytes=%llu", reqTag.c_str(),
                  (unsigned long long) r.bodyBytes);
            if (u.compare(0, 5, "https") == 0 || r.sslVerifyResult >= 0) {
                uint32_t vf = r.sslVerifyResult >= 0 ? (uint32_t) r.sslVerifyResult : 0xffffffffu;
                LOG_I("images", "IMAGE_TLS_RESULT hash=%s host=%s curl=%ld verify_result=0x%08lx [%s]",
                      reqTag.c_str(), known ? host.c_str() : "?", r.curlCode, (unsigned long) vf,
                      r.sslVerifyResult >= 0 ? verifyFlagNames(vf).c_str() : "?");
            }
            if (known) {
                if (r.error != http::Error::None) {
                    hostHealth->noteFailure(host, clockx::unixNow());
                } else if (r.status >= 200 && r.status < 300) {
                    hostHealth->noteSuccess(host);
                }
            }
            images::FetchResponse f;
            f.status = r.status;
            f.contentType = r.contentType;
            f.body = std::move(r.body);
            f.detail = http::describe(r);
            if (!r.detail.empty()) {
                // the generic "network: ..." text is what the user sees; this is for the log only
                f.detail += " [" + r.detail + "]";
            }
            // TLS diagnostics: exact curl code + the host that failed (the URL path stays out of the log)
            f.detail += " [curl=" + std::to_string(r.curlCode) + ", host=" + (known ? host : std::string("?")) + "]";
            if (r.error == http::Error::TooLarge) {
                f.status = 413;                // permanent: never retried
            } else if (r.error == http::Error::HttpsUnsupported || r.error == http::Error::Redirect
                       || r.error == http::Error::TlsCertificate) {
                f.status = 0;                  // permanent: not http(s), refused redirect, untrusted certificate
            } else if (r.error != http::Error::None) {
                f.networkError = true;         // retried after a back-off
            }
            return f;
        };
        // the TMDB fallback is built only when this image carries title/year metadata and the user set a key
        std::function<images::FetchResponse(const images::ArtMeta &)> fallbackFn;
        if (!pending->art.title.empty() && keyProvider) {
            std::string key = keyProvider();
            if (!key.empty()) {
                fallbackFn = [key = std::move(key)](const images::ArtMeta &art) {
                    return images::tmdb::findPoster(key, art);   // by-value key: never logged, never stored elsewhere
                };
            }
        }
        // TMDB-preferred: when a key is set, trust TMDB's exact-title/year match before the provider's
        // artwork (IPTV exports often serve one default poster for many titles). Titles TMDB cannot
        // match confidently are remembered for this session and skip the extra probe next time.
        bool tmdbPreferred = static_cast<bool>(fallbackFn);
        std::string missKey;
        if (!pending->art.title.empty()) {
            missKey = artKey(pending->art);
            std::lock_guard<std::mutex> lock(misses->mutex);
            if (misses->set.count(missKey) != 0) {
                tmdbPreferred = false;   // probed this session and missed: straight to the provider copy
            }
        }
        pending->outcome = images::loadImage(pending->url, *cache, fetch, clockx::unixNow(), variantBoxes, maxSource,
                                             pending->art.title.empty() ? nullptr : &pending->art, fallbackFn, tmdbPreferred);
        if (tmdbPreferred && !missKey.empty() && pending->outcome.tmdbAttempted
            && pending->outcome.source == images::ArtSource::Provider && !pending->outcome.variants.empty()) {
            std::lock_guard<std::mutex> lock(misses->mutex);
            if (misses->set.size() < 4096) {
                misses->set.insert(missKey);
            }
            LOG_I("images", "IMAGE_TMDB_MISS remembered=1");
        }
        if (pending->kind == ImageKind::HeroPoster && !pending->outcome.variants.empty()) {
            // the poster's mean colour (64 x 64 samples at most): the Home showcase's tint
            pending->average = images::averageColor(pending->outcome.variants[0]);
        }
    }, [this, weak, pending]() {
        if (weak.lock()) {
            decoded.push_back(pending);
        }
    });
}

void ImageLoader::finish(const std::shared_ptr<Pending> &p) {
    const images::LoadOutcome &o = p->outcome;
    std::string hash = images::cacheKey(p->url);   // logged instead of the URL
    if (o.result != images::Scheduler::Result::Ok || o.variants.empty()) {
        images::artworkStats().unavailable++;
        scheduler.finished(p->key, o.result == images::Scheduler::Result::Ok ? images::Scheduler::Result::Permanent
                                                                             : o.result, now);
        LOG_I("images", "IMAGE_RENDER_RESULT hash=%s bound=0 (placeholder)", hash.c_str());
        LOG_I("images", "image %s unavailable (%s): %s", hash.c_str(),
              o.result == images::Scheduler::Result::RetryLater ? "retry later" : "placeholder", o.detail.c_str());
        if (p->kind == ImageKind::Backdrop) {
            gen++;   // screens waiting for this backdrop rebind and show their poster fallback
        }
        return;
    }
    auto set = std::make_shared<ImageSet>();
    set->source = {o.sourceW, o.sourceH};
    set->average = p->average;
    int64_t bytes = 0;
    const bool exact = exactPage(p->kind);
    for (const auto &v: o.variants) {
        ImageVariant iv;
        iv.texture = makeTexture(v, bytes, exact, iv.page);
        iv.size = {v.w, v.h};
        if (!iv.texture) {
            scheduler.finished(p->key, images::Scheduler::Result::RetryLater, now);
            LOG_I("images", "IMAGE_TEXTURE_RESULT hash=%s kind=%d w=%d h=%d ok=0", hash.c_str(), (int) p->kind, v.w, v.h);
            LOG_W("images", "image %s: texture creation failed", hash.c_str());
            return;
        }
        set->variants.push_back(iv);
    }
    LOG_I("images", "IMAGE_TEXTURE_RESULT hash=%s kind=%d w=%d h=%d ok=1 variants=%zu bytes=%lld", hash.c_str(),
          (int) p->kind, o.sourceW, o.sourceH, set->variants.size(), (long long) bytes);
    for (const auto &evicted: memory.put(p->key, set, bytes)) {
        scheduler.forget(evicted);
    }
    const bool cached = memory.contains(p->key);
    if (p->kind == ImageKind::Backdrop && cached) {
        // never a pile of 3.5 MiB backdrops: the oldest leaves memory (a screen still showing it keeps it alive)
        backdropKeys.erase(std::remove(backdropKeys.begin(), backdropKeys.end(), p->key), backdropKeys.end());
        backdropKeys.push_back(p->key);
        while ((int) backdropKeys.size() > MAX_BACKDROPS) {
            memory.erase(backdropKeys.front());
            scheduler.forget(backdropKeys.front());
            backdropKeys.pop_front();
        }
    }
    if (o.source == images::ArtSource::Cache) {
        images::artworkStats().cache++;
    } else if (o.source == images::ArtSource::Tmdb) {
        images::artworkStats().tmdb++;
    } else {
        images::artworkStats().provider++;
    }
    scheduler.finished(p->key, images::Scheduler::Result::Ok, now);
    if (!cached) {
        // A rejected (for example oversized) entry must not remain Ready without being retrievable.
        scheduler.forget(p->key);
    }
    gen++;
    if (exact) {
        // the large art: what the provider sent and what it became, so a soft picture can be traced to its source
        const ImageVariant &v = set->variants[0];
        LOG_I("images", "large image %s kind %d: source %dx%d -> texture %dx%d (page %dx%d exact, %lld KiB)%s",
              hash.c_str(), (int) p->kind, o.sourceW, o.sourceH, v.size.x, v.size.y, v.page.x, v.page.y,
              (long long) (bytes / 1024), o.fromDisk ? " from disk" : "");
    }
    LOG_V("images", "image %s kind %d ready: %s%s (memory %d images, %lld KiB)", hash.c_str(), (int) p->kind,
          o.detail.c_str(), o.fromDisk ? " from disk" : "", (int) memory.size(), (long long) (memory.bytes() / 1024));
}

void ImageLoader::clearCache(std::function<void(int64_t)> done) {
    memory.clear();     // widgets keep what they show until they rebind
    backdropKeys.clear();
    scheduler.reset();
    prunePendingFetchUrls();
    gen++;
    std::shared_ptr<images::DiskCache> cache = disk;
    auto freed = std::make_shared<int64_t>(0);
    std::weak_ptr<bool> weak = alive;
    jobs.submit(JobPriority::High, "clear-images", [cache, freed](const CancelToken &) {
        *freed = cache->clear();
    }, [this, weak, freed, done]() {
        if (weak.lock()) {
            diskUsage = 0;
            LOG_I("images", "image cache cleared: %lld KiB freed", (long long) (*freed / 1024));
            if (done) {
                done(*freed);
            }
        }
    });
}

void ImageLoader::refreshDiskUsage(std::function<void()> done) {
    std::shared_ptr<images::DiskCache> cache = disk;
    auto bytes = std::make_shared<int64_t>(0);
    std::weak_ptr<bool> weak = alive;
    jobs.submit(JobPriority::Low, "image-cache-size", [cache, bytes](const CancelToken &) {
        *bytes = cache->totalBytes();
    }, [this, weak, bytes, done]() {
        if (weak.lock()) {
            diskUsage = *bytes;
            if (done) {
                done();
            }
        }
    });
}
