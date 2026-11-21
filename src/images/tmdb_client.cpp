// Optional TMDB backfill for artwork. Called only when the provider's image URL failed and the
// caller carries title+year; it never runs for entries whose provider artwork loaded, and every
// doubtful result is discarded. Nothing here is logged with the API key or a credential.

#include "tmdb_client.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "../core/json.h"
#include "../network/http.h"
#include "../platform/log.h"
#include "artwork_stats.h"
#include "image_pipeline.h"

namespace images::tmdb {
    namespace {
        const char *kSearchMovie = "https://api.themoviedb.org/3/search/movie?api_key=%s&query=%s%s";
        const char *kSearchTv = "https://api.themoviedb.org/3/search/tv?api_key=%s&query=%s%s";
        const char *kImageW342 = "https://image.tmdb.org/t/p/w342%s";
        const size_t kSearchMaxBytes = 256 * 1024;
        const size_t kPosterMaxBytes = 3 * 1024 * 1024;

        // UTF-8-aware-ish normalization: lowercase ASCII, strip Latin-1 accents, everything that is
        // not a letter or digit becomes one space, spaces collapse. Unusual scripts keep their bytes
        // and only compare when byte-identical, which is the safe side to err on.
        std::string normalize(const std::string &raw) {
            struct Accent {
            unsigned char lead;
            unsigned char cont;
            char out;
        };
        static const Accent latin1[] = {
                    {0xC3,0xA0,'a'},{0xC3,0xA1,'a'},{0xC3,0xA2,'a'},{0xC3,0xA3,'a'},{0xC3,0xA4,'a'},{0xC3,0xA5,'a'},
                    {0xC3,0xA7,'c'},{0xC3,0xA9,'e'},{0xC3,0xAA,'e'},{0xC3,0xAB,'e'},{0xC3,0xAA,'e'},
                    {0xC3,0xA8,'e'},{0xC3,0xAC,'i'},{0xC3,0xAD,'i'},{0xC3,0xAE,'i'},{0xC3,0xAF,'i'},
                    {0xC3,0xB1,'n'},{0xC3,0xB2,'o'},{0xC3,0xB3,'o'},{0xC3,0xB4,'o'},{0xC3,0xB5,'o'},{0xC3,0xB6,'o'},
                    {0xC3,0xB9,'u'},{0xC3,0xBA,'u'},{0xC3,0xBB,'u'},{0xC3,0xBC,'u'},{0xC3,0xBD,'y'},
                    {0xC3,0x80,'a'},{0xC3,0x81,'a'},{0xC3,0x82,'a'},{0xC3,0x83,'a'},{0xC3,0x84,'a'},{0xC3,0x85,'a'},
                    {0xC3,0x87,'c'},{0xC3,0x88,'e'},{0xC3,0x89,'e'},{0xC3,0x8A,'e'},{0xC3,0x8B,'e'},
                    {0xC3,0x8C,'i'},{0xC3,0x8D,'i'},{0xC3,0x8E,'i'},{0xC3,0x8F,'i'},
                    {0xC3,0x91,'n'},{0xC3,0x92,'o'},{0xC3,0x93,'o'},{0xC3,0x94,'o'},{0xC3,0x95,'o'},{0xC3,0x96,'o'},
                    {0xC3,0x99,'u'},{0xC3,0x9A,'u'},{0xC3,0x9B,'u'},{0xC3,0x9C,'u'},{0xC3,0x9D,'y'},
                    {0xC3,0x9F,'s'},   // Eszett
            };
            std::string out;
            out.reserve(raw.size());
            for (size_t i = 0; i < raw.size();) {
                unsigned char c = (unsigned char) raw[i];
                bool mapped = false;
                for (const auto &p: latin1) {
                    if (i + 1 < raw.size() && (unsigned char) raw[i] == p.lead && (unsigned char) raw[i + 1] == p.cont) {
                        out.push_back(p.out);
                        i += 2;
                        mapped = true;
                        break;
                    }
                }
                if (mapped) {
                    continue;
                }
                if (c < 0x80) {
                    if (std::isalnum(c)) {
                        out.push_back((char) std::tolower(c));
                    } else {
                        out.push_back(' ');
                    }
                    i += 1;
                    continue;
                }
                // a non-ASCII continuation byte that is not part of a known pair: keep it verbatim so
                // CJK titles only match themselves, never a mangled Latin lookalike
                out.push_back((char) c);
                i += 1;
            }
            // collapse spaces
            std::string collapsed;
            collapsed.reserve(out.size());
            bool lastSpace = true;
            for (char c: out) {
                if (c == ' ') {
                    if (!lastSpace) collapsed.push_back(' ');
                    lastSpace = true;
                } else {
                    collapsed.push_back(c);
                    lastSpace = false;
                }
            }
            while (!collapsed.empty() && collapsed.back() == ' ') {
                collapsed.pop_back();
            }
            return collapsed;
        }

        std::string percentEncode(const std::string &raw) {
            static const char hex[] = "0123456789ABCDEF";
            std::string out;
            out.reserve(raw.size() * 2);
            for (unsigned char c: raw) {
                if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'
                    || c == '.' || c == '~') {
                    out.push_back((char) c);
                } else if (c == ' ') {
                    out.push_back('+');
                } else {
                    out.push_back('%');
                    out.push_back(hex[c >> 4]);
                    out.push_back(hex[c & 0xF]);
                }
            }
            return out;
        }

        bool sameTitle(const std::string &want, const std::string &have) {
            return normalize(want) == normalize(have);
        }

        // pick the first result whose normalized title equals the query's, with the exact year when known;
        // any doubt -> no match. Never picks "most popular" on a bare prefix or a Levenshtein-close title.
        std::string pickPosterPath(const json::Value &results, const std::string &title, int year, bool isTv) {
            if (!results.isArray()) {
                return "";
            }
            for (const json::Value &r: results.items()) {
                std::string original = isTv ? r["original_name"].asString() : r["original_title"].asString();
                std::string common = isTv ? r["name"].asString() : r["title"].asString();
                std::string date = isTv ? r["first_air_date"].asString() : r["release_date"].asString();
                if (r["adult"].asBool(false)) {
                    continue;
                }
                bool titleOk = (!original.empty() && sameTitle(title, original)) || (!common.empty() && sameTitle(title, common));
                if (!titleOk) {
                    continue;
                }
                if (!candidateYearMatches(year, date)) {
                    continue;
                }
                std::string poster = r["poster_path"].asString();
                if (poster.empty()) {
                    continue;
                }
                return poster;
            }
            return "";
        }
    } // namespace

    FetchResponse findPoster(const std::string &apiKey, const ArtMeta &art) {
        FetchResponse out;
        std::string title = art.title;   // used as-is for the query, normalized only for matching
        if (apiKey.empty() || title.empty()) {
            out.networkError = true;
            out.detail = "no metadata";
            return out;
        }
        artworkStats().tmdbQueries++;
        char url[2048];
        std::string enc = percentEncode(title);
        int yr = art.year;
        std::string yearParam = yr > 0 ? ("&year=" + std::to_string(yr)) : "";
        if (art.subject == 2) {
            std::string air = yr > 0 ? ("&first_air_date_year=" + std::to_string(yr)) : "";
            std::snprintf(url, sizeof(url), kSearchTv, apiKey.c_str(), enc.c_str(), air.c_str());
        } else {
            std::snprintf(url, sizeof(url), kSearchMovie, apiKey.c_str(), enc.c_str(), yearParam.c_str());
        }
        http::Request req;
        req.url = url;
        req.connectTimeoutMs = 5000;
        req.totalTimeoutMs = 8000;
        req.stallTimeoutS = 6;
        req.maxBytes = kSearchMaxBytes;
        // The API key is in the query string: never follow an HTTPS search redirect down to plain HTTP.
        req.credentials = true;
        req.retries = 0;
        http::Response r = http::get(req);
        if (r.error != http::Error::None || r.status != 200) {
            out.networkError = true;
            out.detail = "search failed (HTTP " + std::to_string(r.status) + ")";
            return out;
        }
        json::Value root;
        std::string jerr;
        if (!json::parse(r.body, root, &jerr) || !root.isObject()) {
            out.networkError = true;
            out.detail = "search: bad response";
            return out;
        }
        std::string posterPath = pickPosterPath(root["results"], title, yr, art.subject == 2);
        if (posterPath.empty()) {
            // one retry without the year constraint, but only accept a match whose year also matches
            if (yr > 0) {
                if (art.subject == 2) {
                    std::snprintf(url, sizeof(url), kSearchTv, apiKey.c_str(), enc.c_str(), "");
                } else {
                    std::snprintf(url, sizeof(url), kSearchMovie, apiKey.c_str(), enc.c_str(), "");
                }
                req.url = url;
                r = http::get(req);
                if (r.error == http::Error::None && r.status == 200) {
                    root = json::Value();
                    if (json::parse(r.body, root, &jerr) && root.isObject()) {
                        posterPath = pickPosterPath(root["results"], title, yr, art.subject == 2);
                    }
                }
            }
        }
        if (posterPath.empty()) {
            out.networkError = true;
            out.detail = "no confident match";
            return out;
        }
        artworkStats().tmdbHits++;
        std::snprintf(url, sizeof(url), kImageW342, posterPath.c_str());
        req.url = url;
        req.maxBytes = kPosterMaxBytes;
        req.totalTimeoutMs = 12000;
        req.credentials = false;  // this image request carries no API key
        r = http::get(req);
        if (r.error != http::Error::None || r.status != 200 || r.body.empty()) {
            out.networkError = true;
            out.detail = "poster download failed";
            return out;
        }
        out.status = 200;
        out.contentType = r.contentType;
        out.body = std::move(r.body);
        return out;
    }
} // namespace images::tmdb
