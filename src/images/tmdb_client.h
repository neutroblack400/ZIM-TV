// Optional TMDB backfill for artwork. Used only when the provider's image URL failed and the
// caller carries title+year metadata; never consulted otherwise, never replaces a working
// provider image, and never falls back on a dubious match (strict normalized equality, plus the
// exact year when one is known).
#ifndef ZIMTV_IMAGES_TMDB_CLIENT_H
#define ZIMTV_IMAGES_TMDB_CLIENT_H

#include <cstddef>
#include <string>

#include "image_pipeline.h"   // FetchResponse

namespace images::tmdb {

    // Pure candidate guard: when a year is requested, only a complete YYYY-MM-DD date
    // with that exact year qualifies. This deliberately rejects missing/partial dates.
    inline bool candidateYearMatches(int requestedYear, const std::string &date) {
        if (requestedYear <= 0) {
            return true;
        }
        if (date.size() != 10 || date[4] != '-' || date[7] != '-') {
            return false;
        }
        for (std::size_t i = 0; i < date.size(); ++i) {
            if (i != 4 && i != 7 && (date[i] < '0' || date[i] > '9')) {
                return false;
            }
        }
        int year = (date[0] - '0') * 1000 + (date[1] - '0') * 100 + (date[2] - '0') * 10 + (date[3] - '0');
        return year == requestedYear;
    }

    // searches TMDB for the artwork and returns the poster bytes; networkError=true means the
    // search or the poster download failed (caller treats it as "no fallback available"). Never
    // logs the API key or the full URL.
    FetchResponse findPoster(const std::string &apiKey, const ArtMeta &art);

} // namespace images::tmdb
#endif // ZIMTV_IMAGES_TMDB_CLIENT_H
