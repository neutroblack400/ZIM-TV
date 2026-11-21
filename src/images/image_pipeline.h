// One image load as run on a worker thread (host-testable; the HTTP fetch is injected):
//   disk cache (fresh) -> decode
//   otherwise download -> content checks -> decode -> store in the disk cache
//   download failed but a stale cached copy exists -> use the stale copy
// A cached file that does not decode is deleted and downloaded again once. Every failure is classified
// for the scheduler: RetryLater (network) or Permanent (HTTP error status, not an image, undecodable).

#ifndef ZIMTV_IMAGES_IMAGE_PIPELINE_H
#define ZIMTV_IMAGES_IMAGE_PIPELINE_H

#include <functional>
#include <string>
#include <vector>

#include "disk_cache.h"
#include "image_decode.h"
#include "image_scheduler.h"

namespace images {

    // where the bytes came from (for the diagnostics counters; never for identity)
    enum class ArtSource { Provider = 0, Tmdb = 1, Cache = 2 };

    // optional metadata for the TMDB backfill: set only when the caller knows the item's title/year and type
    struct ArtMeta {
        std::string title;
        int year = 0;
        uint8_t subject = 0;   // 0 = none, 1 = movie, 2 = tv/series
    };

    struct FetchResponse {
        bool networkError = false;   // no HTTP response (DNS, connect, TLS, timeout, canceled)
        long status = 0;
        std::string contentType;
        std::string body;
        std::string detail;          // for the log (never the URL, never a credential)
    };

    using Fetcher = std::function<FetchResponse(const std::string &normalizedUrl)>;
    // optional fallback: called only when the provider fetch produced no usable image
    using FallbackFetcher = std::function<FetchResponse(const ArtMeta &)>;

    struct Box {
        int w;
        int h;
    };

    struct LoadOutcome {
        Scheduler::Result result = Scheduler::Result::Permanent;
        std::vector<Image> variants;   // one per requested box, when result == Ok
        int sourceW = 0;               // the decoded source image, before any downscale (for the log)
        int sourceH = 0;
        bool fromDisk = false;
        bool staleUsed = false;
        ArtSource source = ArtSource::Provider;
        bool tmdbAttempted = false;    // the TMDB backfill/preferred path ran (for the per-session miss cache)
        std::string detail;            // failure reason / notes for the log
    };

    // maxDimension: source images larger than this (either side) are refused before decoding (Permanent).
    // art/fallback: set together by the caller to allow the TMDB backfill; when art is null the behaviour
    // is byte-for-byte the pre-existing provider-only pipeline.
    LoadOutcome loadImage(const std::string &normalizedUrl, DiskCache &disk, const Fetcher &fetch, int64_t now,
                          const std::vector<Box> &boxes, int maxDimension = MAX_DIMENSION,
                          const ArtMeta *art = nullptr, const FallbackFetcher &fallback = FallbackFetcher(),
                          bool tmdbPreferred = false);
}

#endif // ZIMTV_IMAGES_IMAGE_PIPELINE_H
