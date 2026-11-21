#include "image_pipeline.h"

#include "image_key.h"
#include "../platform/log.h"

namespace images {

    namespace {
        bool decodeVariants(const std::string &bytes, const std::vector<Box> &boxes, int maxDimension,
                            LoadOutcome &out, const std::string &tag) {
            Image img;
            std::string err;
            if (!decode(bytes, img, &err, maxDimension)) {
                LOG_I("images", "IMAGE_DECODE_RESULT hash=%s ok=0 bytes=%zu err=%s", tag.c_str(), bytes.size(), err.c_str());
                out.detail = err;
                return false;
            }
            out.variants.clear();
            out.sourceW = img.w;
            out.sourceH = img.h;
            for (const Box &b: boxes) {
                out.variants.push_back(prepare(img, b.w, b.h));
            }
            out.detail = std::to_string(img.w) + "x" + std::to_string(img.h) + " " + formatName(sniff(bytes));
            LOG_I("images", "IMAGE_DECODE_RESULT hash=%s ok=1 w=%d h=%d bytes=%zu format=%s", tag.c_str(), img.w,
                  img.h, bytes.size(), formatName(sniff(bytes)));
            return true;
        }
    }

    LoadOutcome loadImage(const std::string &url, DiskCache &disk, const Fetcher &fetch, int64_t now,
                          const std::vector<Box> &boxes, int maxDimension,
                          const ArtMeta *art, const FallbackFetcher &fallback, bool tmdbPreferred) {
        LoadOutcome out;
        // The disk key is salted with the art identity when the caller knows the item's title:
        // two DIFFERENT productions that share one provider URL (Xtream panels serve the same default
        // poster for many titles) must never share one cache file, or both would show the same poster.
        std::string diskUrl = url;
        if (art && !art->title.empty()) {
            std::string identity = art->title + '|' + std::to_string(art->year) + '|' + std::to_string(art->subject);
            diskUrl = url.empty() ? "art#" + identity : url + "|art#" + identity;
        }
        const std::string tag = cacheKey(diskUrl);
        std::string cached;
        DiskCache::Lookup lookup = disk.get(diskUrl, now, cached);
        LOG_I("images", "IMAGE_CACHE_READ hash=%s lookup=%d", tag.c_str(), (int) lookup);
        if (lookup == DiskCache::Lookup::Fresh) {
            if (decodeVariants(cached, boxes, maxDimension, out, tag)) {
                out.result = Scheduler::Result::Ok;
                out.fromDisk = true;
                out.source = ArtSource::Cache;
                return out;
            }
            disk.remove(diskUrl);   // stored bytes no longer decode: fetch them again
            cached.clear();
        }

        FetchResponse r;
        std::string failure;
        Scheduler::Result failResult = Scheduler::Result::Permanent;
        if (tmdbPreferred && art && !art->title.empty() && fallback) {
            // The provider's artwork is only trusted when TMDB has no confident match for a known
            // title (exact normalized title + the known year): try TMDB first, then the provider.
            FetchResponse fb = fallback(*art);
            out.tmdbAttempted = true;
            if (!fb.networkError && fb.status == 200 && !fb.body.empty() && acceptableContentType(fb.contentType)) {
                if (decodeVariants(fb.body, boxes, maxDimension, out, tag)) {
                    std::string err;
                    if (!disk.put(diskUrl, fb.body, now, &err)) {
                        LOG_I("images", "IMAGE_CACHE_WRITE hash=%s bytes=%zu ok=0 err=%s", tag.c_str(), fb.body.size(), err.c_str());
                        out.detail += " (not cached: " + err + ")";
                    } else {
                        LOG_I("images", "IMAGE_CACHE_WRITE hash=%s bytes=%zu ok=1 tmdb", tag.c_str(), fb.body.size());
                    }
                    out.result = Scheduler::Result::Ok;
                    out.source = ArtSource::Tmdb;
                    out.detail += " (tmdb preferred)";
                    return out;
                }
                failure = "tmdb preferred: " + out.detail;
            } else {
                failure = "tmdb preferred: " + (!fb.detail.empty() ? fb.detail : (fb.networkError ? "network error" : std::to_string(fb.status)));
            }
            // fall through to the provider fetch below (failResult is re-derived there)
        }
        r = fetch(url);
        if (r.networkError) {
            failure = "network: " + r.detail;
            failResult = Scheduler::Result::RetryLater;
        } else if (r.status != 200) {
            failure = "HTTP " + std::to_string(r.status);
            if (!r.detail.empty()) {
                failure += ": " + r.detail;   // TLS diagnostic: keep the real reason (e.g. certificate verify)
            }
            // 5xx / 429 are worth another try later; other statuses are not
            failResult = r.status >= 500 || r.status == 429 || r.status == 408 ? Scheduler::Result::RetryLater
                                                                                : Scheduler::Result::Permanent;
        } else if (r.body.empty()) {
            failure = "empty response";
        } else if (!acceptableContentType(r.contentType)) {
            failure = "not an image (Content-Type " + r.contentType + ")";
        } else if (decodeVariants(r.body, boxes, maxDimension, out, tag)) {
            std::string err;
            if (!disk.put(diskUrl, r.body, now, &err)) {
                LOG_I("images", "IMAGE_CACHE_WRITE hash=%s bytes=%zu ok=0 err=%s", tag.c_str(), r.body.size(), err.c_str());
                out.detail += " (not cached: " + err + ")";
            } else {
                LOG_I("images", "IMAGE_CACHE_WRITE hash=%s bytes=%zu ok=1", tag.c_str(), r.body.size());
            }
            out.result = Scheduler::Result::Ok;
            out.source = ArtSource::Provider;
            return out;
        } else {
            failure = out.detail;   // decode error
        }

        // provider failed for a reason the bytes could not fix: try the TMDB backfill when the caller gave metadata
        if (art && !art->title.empty() && fallback) {
            out.tmdbAttempted = true;
            FetchResponse fb = fallback(*art);
            if (!fb.networkError && fb.status == 200 && !fb.body.empty() && acceptableContentType(fb.contentType)) {
                if (decodeVariants(fb.body, boxes, maxDimension, out, tag)) {
                    std::string err;
                    if (!disk.put(diskUrl, fb.body, now, &err)) {
                        LOG_I("images", "IMAGE_CACHE_WRITE hash=%s bytes=%zu ok=0 err=%s", tag.c_str(), fb.body.size(), err.c_str());
                        out.detail += " (not cached: " + err + ")";
                    } else {
                        LOG_I("images", "IMAGE_CACHE_WRITE hash=%s bytes=%zu ok=1 tmdb", tag.c_str(), fb.body.size());
                    }
                    out.result = Scheduler::Result::Ok;
                    out.source = ArtSource::Tmdb;
                    out.detail += " (tmdb)";
                    return out;
                }
                failure = "tmdb fallback: " + out.detail;
            } else {
                failure += " | tmdb: " + (!fb.detail.empty() ? fb.detail : (fb.networkError ? "network error" : std::to_string(fb.status)));
            }
        }

        if (lookup == DiskCache::Lookup::Stale && !cached.empty() && decodeVariants(cached, boxes, maxDimension, out, tag)) {
            out.result = Scheduler::Result::Ok;
            out.fromDisk = true;
            out.staleUsed = true;
            out.source = ArtSource::Cache;
            out.detail += " (stale copy; refresh failed: " + failure + ")";
            return out;
        }
        out.variants.clear();
        out.result = failResult;
        out.detail = failure;
        return out;
    }
}
