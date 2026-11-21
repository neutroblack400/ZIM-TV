// Artwork source counters for the diagnostics page: which stage produced the images the app showed.
// Never keyed by URL, title or bytes; only counts. Lives in the images module so any caller can bump it.
#ifndef ZIMTV_IMAGES_ARTWORK_STATS_H
#define ZIMTV_IMAGES_ARTWORK_STATS_H

#include <atomic>
#include <cstdint>

namespace images {

    struct ArtworkStats {
        std::atomic<unsigned long long> provider{0};      // fetched from the provider URL
        std::atomic<unsigned long long> cache{0};        // served from the disk cache (no network)
        std::atomic<unsigned long long> tmdb{0};          // TMDB fallback produced the bytes
        std::atomic<unsigned long long> unavailable{0};   // provider+fallback(+stale) all failed
        std::atomic<unsigned long long> tmdbQueries{0};   // TMDB API searches made
        std::atomic<unsigned long long> tmdbHits{0};      // those that returned a poster

        void reset() {
            provider.store(0);
            cache.store(0);
            tmdb.store(0);
            unavailable.store(0);
            tmdbQueries.store(0);
            tmdbHits.store(0);
        }
    };

    inline ArtworkStats &artworkStats() {
        static ArtworkStats s;
        return s;
    }

    inline void resetArtworkStats() {
        artworkStats().reset();
    }
}
#endif // ZIMTV_IMAGES_ARTWORK_STATS_H
