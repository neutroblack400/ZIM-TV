#include "classify.h"

#include <cctype>
#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "../core/utf8.h"

namespace iptv {

    std::string foldKey(const std::string &text) {
        std::u32string folded = utf8::foldForSearch(text);
        std::string out;
        out.reserve(folded.size());
        for (char32_t cp : folded) {
            if (cp < 0x80) {
                out.push_back((char) cp);
            } else if (cp >= 0x400 && cp <= 0x4FF) {
                continue;   // Cyrillic: drop (group keywords are Latin)
            } else {
                continue;   // anything else: drop
            }
        }
        return out;
    }

    namespace {

        bool contains(const std::string &key, const char *what) {
            return key.find(what) != std::string::npos;
        }

        // A group whose name starts with the provider's channel prefix ("CANAL" singular, "CANAIS"
        // plural as used by Brazilian/Spanish providers, or "CHANNEL") is a live channel group.
        bool startsWithChannelPrefix(const std::string &key) {
            static const char *const PREFIXES[] = {"canal", "canais", "channel"};
            for (const char *p : PREFIXES) {
                const size_t n = std::strlen(p);
                if (key.size() >= n && key.compare(0, n, p) == 0) {
                    return true;
                }
            }
            return false;
        }

        // tag a URL's path with a few well-known segment checks (Xtream exports put /live/ /movie/ /series/
        // in every stream URL). Query and fragment are ignored; the path is matched on folder boundaries.
        void urlPathHints(const std::string &url, bool &live, bool &movie, bool &series) {
            live = movie = series = false;
            size_t begin = url.find("://");
            size_t path = begin == std::string::npos ? std::string::npos : url.find('/', begin + 3);
            if (path == std::string::npos) {
                return;
            }
            size_t end = url.find_first_of("?#", path);
            std::string p = url.substr(path, end == std::string::npos ? std::string::npos : end - path);
            std::string key = foldKey(p);
            auto seg = [&key](const char *name) {
                std::string needle = std::string("/") + name + "/";
                return key.find(needle) != std::string::npos || key.compare(0, needle.size() - 1, needle, 0,
                       needle.size() - 1) == 0;
            };
            if (seg("live") || seg("stream")) {
                live = true;
            }
            if (seg("movie") || seg("vod") || seg("movies")) {
                movie = true;
            }
            if (seg("series") || seg("serie") || seg("tvshow") || seg("tv-show")) {
                series = true;
            }
        }

    }

    std::string urlExtension(const std::string &url) {
        size_t end = url.find_first_of("?#");
        std::string path = url.substr(0, end);
        size_t cut = path.find_last_of('/');
        if (cut != std::string::npos) {
            path = path.substr(cut + 1);
        }
        size_t dot = path.find_last_of('.');
        if (dot == std::string::npos || dot + 1 >= path.size()) {
            return "";
        }
        std::string ext = path.substr(dot + 1);
        if (ext.size() > 5) {
            return "";   // not a container extension
        }
        std::string key = foldKey(ext);
        for (char &c : key) {
            if (!isalpha((unsigned char) c) && !isdigit((unsigned char) c)) {
                return "";
            }
        }
        return key;
    }

    bool parseEpisodeMarker(const std::string &name, int &season, int &episode) {
        std::string key = foldKey(name);
        // S01E02 / S01 E02 / S1.E2 / s01e02
        for (size_t i = 0; i + 3 < key.size(); i++) {
            if (key[i] != 's' || !isdigit((unsigned char) key[i + 1])) {
                continue;
            }
            size_t p = i + 1;
            int s = 0;
            while (p < key.size() && isdigit((unsigned char) key[p]) && s < 100) {
                s = s * 10 + (key[p] - '0');
                p++;
            }
            if (p >= key.size() || (unsigned char) (key[p] | 0x20) != 'e') {
                continue;
            }
            p++;
            // tolerate a single separator between the season and the episode ("S01 E02", "S01-E02", "S01.E02")
            while (p < key.size() && (key[p] == ' ' || key[p] == '-' || key[p] == '.' || key[p] == '_')) {
                p++;
            }
            int e = 0;
            bool any = false;
            while (p < key.size() && isdigit((unsigned char) key[p]) && e < 10000) {
                e = e * 10 + (key[p] - '0');
                p++;
                any = true;
            }
            if (any && s >= 0 && e >= 1) {
                season = s < 1 ? 1 : s;
                episode = e;
                return true;
            }
        }
        // 1x02 / 01x002
        for (size_t i = 0; i + 3 < key.size(); i++) {
            if (!isdigit((unsigned char) key[i])) {
                continue;
            }
            size_t p = i;
            int s = 0;
            while (p < key.size() && isdigit((unsigned char) key[p]) && s < 100) {
                s = s * 10 + (key[p] - '0');
                p++;
            }
            if (p >= key.size() || key[p] != 'x') {
                continue;
            }
            p++;
            int e = 0;
            bool any = false;
            while (p < key.size() && isdigit((unsigned char) key[p]) && e < 10000) {
                e = e * 10 + (key[p] - '0');
                p++;
                any = true;
            }
            if (any && s >= 1 && e >= 1) {
                season = s;
                episode = e;
                return true;
            }
        }
        // "Season 2 Episode 3" / "Temporada 2 Episodio 3" / "T02 E03" (loose)
        auto wordNum = [&key](const char *word) -> int {
            size_t at = key.find(word);
            if (at == std::string::npos) {
                return -1;
            }
            size_t p = at + std::string(word).size();
            while (p < key.size() && (key[p] == ' ' || key[p] == '.' || key[p] == '_' || key[p] == '-')) {
                p++;
            }
            int v = 0;
            bool any = false;
            while (p < key.size() && isdigit((unsigned char) key[p]) && v < 10000) {
                v = v * 10 + (key[p] - '0');
                p++;
                any = true;
            }
            return any ? v : -1;
        };
        int s = wordNum("season");
        if (s < 0) s = wordNum("temporada");
        if (s < 0) s = wordNum("saison");
        int e = wordNum("episode");
        if (e < 0) e = wordNum("episodio");
        if (e < 0) e = wordNum("capitulo");
        if (e < 0) e = wordNum("ch");
        if (s >= 0 && e >= 0) {
            season = s < 1 ? 1 : s;
            episode = e < 1 ? 1 : e;
            return true;
        }
        return false;
    }

    int trailingYear(const std::string &name) {
        // "(2024)" at the very end, or a bare 4-digit year at the end
        size_t end = name.find_last_not_of(" \t\r\n");
        if (end == std::string::npos || end < 4) {
            return 0;
        }
        size_t stop = end + 1;
        bool parens = name[end] == ')';
        size_t digitsEnd = parens ? end : stop;
        size_t digitsBegin = digitsEnd;
        while (digitsBegin > 0 && isdigit((unsigned char) name[digitsBegin - 1])) {
            digitsBegin--;
        }
        if (digitsEnd - digitsBegin != 4) {
            return 0;
        }
        int y = std::atoi(name.substr(digitsBegin, 4).c_str());
        if (y < 1900 || y > 2099) {
            return 0;
        }
        if (parens && (digitsBegin == 0 || name[digitsBegin - 1] != '(')) {
            return 0;
        }
        if (!parens && digitsBegin > 0 && !isspace((unsigned char) name[digitsBegin - 1])) {
            return 0;   // part of a longer token
        }
        return y;
    }

    std::string stripTrailingYear(const std::string &name) {
        std::string s = name;
        auto rstrip = [&s](const char *chars) {
            size_t e = s.find_last_not_of(chars);
            s = e == std::string::npos ? "" : s.substr(0, e + 1);
        };
        rstrip(" \t-_:.|–—");
        if (trailingYear(s) == 0) {
            return s;
        }
        size_t stop = s.find_last_not_of(" \t");
        bool parens = s[stop] == ')';
        size_t dEnd = parens ? stop : stop + 1;   // digits are [dBegin, dEnd)
        size_t dBegin = dEnd;
        while (dBegin > 0 && isdigit((unsigned char) s[dBegin - 1])) {
            dBegin--;
        }
        s = s.substr(0, parens ? dBegin - 1 : dBegin);
        rstrip(" \t-_:.|–—");
        return s;
    }

    std::string cutAtMarker(const std::string &name) {
        size_t cut = std::string::npos;
        // S01E02 / 1x02 style marker position
        for (size_t i = 0; i < name.size(); i++) {
            unsigned char c = (unsigned char) name[i];
            if ((c | 0x20) == 's' && i + 1 < name.size() && isdigit((unsigned char) name[i + 1])) {
                size_t p = i + 1;
                while (p < name.size() && isdigit((unsigned char) name[p])) p++;
                if (p < name.size() && (char) (name[p] | 0x20) == 'e') { cut = i; break; }
            }
            if (isdigit(c)) {
                size_t p = i;
                while (p < name.size() && isdigit((unsigned char) name[p])) p++;
                if (p < name.size() && (char) (name[p] | 0x20) == 'x') { cut = i; break; }
            }
        }
        if (cut == std::string::npos) {
            // "season 2" / "temporada 2" / "saison 2" (ASCII words: a case-insensitive byte scan is exact)
            static const char *WORDS[] = {"season", "temporada", "saison"};
            for (const char *w : WORDS) {
                size_t n = strlen(w);
                if (name.size() < n) {
                    continue;
                }
                for (size_t i = 0; i + n <= name.size(); i++) {
                    bool ok = true;
                    for (size_t k = 0; k < n && ok; k++) {
                        unsigned char a = (unsigned char) name[i + k];
                        if (a >= 0x80 || (a | 0x20) != (unsigned char) (w[k] | 0x20)) {
                            ok = false;
                        }
                    }
                    if (ok) { cut = i; break; }
                }
                if (cut != std::string::npos) {
                    break;
                }
            }
        }
        if (cut == std::string::npos) {
            cut = name.size();
        }
        std::string head = name.substr(0, cut);
        size_t e = head.find_last_not_of(" \t-_:.|–—");
        return e == std::string::npos ? "" : head.substr(0, e + 1);
    }

    std::string titleBeforeMarker(const std::string &name) {
        std::string head = cutAtMarker(name);
        // drop a trailing parenthesised year "(2024)" or a bare " 2024"
        size_t ye = head.size();
        if (ye >= 7 && head[ye - 1] == ')') {
            size_t open = head.rfind('(', ye - 1);
            if (open != std::string::npos && ye - 1 - open == 5 && isdigit((unsigned char) head[open + 1])) {
                head = stripTrailingYear(head);
                return head;
            }
        }
        return stripTrailingYear(head);
    }

    std::string titleAfterMarker(const std::string &name) {
        // find the end of the episode marker (episode digits), then take what follows
        for (size_t i = 0; i < name.size(); i++) {
            unsigned char c = (unsigned char) name[i];
            bool sxxeyy = (c | 0x20) == 's' && i + 1 < name.size() && isdigit((unsigned char) name[i + 1]);
            bool nxnn = isdigit(c);
            if (!sxxeyy && !nxnn) {
                continue;
            }
            size_t p = sxxeyy ? i + 1 : i;
            while (p < name.size() && isdigit((unsigned char) name[p])) p++;
            if (sxxeyy) {
                if (p >= name.size() || (char) (name[p] | 0x20) != 'e') {
                    continue;
                }
                p++;
                while (p < name.size() && (name[p] == ' ' || name[p] == '-' || name[p] == '.' || name[p] == '_')) p++;
                while (p < name.size() && isdigit((unsigned char) name[p])) p++;
            } else {
                if (p >= name.size() || (char) (name[p] | 0x20) != 'x') {
                    continue;
                }
                p++;
                while (p < name.size() && isdigit((unsigned char) name[p])) p++;
            }
            size_t e = name.find_first_not_of(" \t-_:.|–—", p);
            return e == std::string::npos ? "" : name.substr(e);
        }
        // word markers ("Season 2 Episode 3"): the slice after "episode N" is not worth the byte-mapping risk;
        // episode rows fall back to "Episode N" instead of showing a wrong slice of the name
        return "";
    }

    bool releaseHint(const std::string &group) {
        std::string key = foldKey(group);
        if (key.empty()) {
            return false;
        }
        static const char *const KEYS[] = {
            "lancamento", "estreia", "novidade", "recem", "new release", "recent", "new", "premiere", "premieres",
            "cines", "cinema 202", "2026", "2025"
        };
        for (const char *k : KEYS) {
            if (contains(key, k)) {
                return true;
            }
        }
        return false;
    }

    Bucket classifyEntry(const std::string &group, const std::string &name, const std::string &url,
                         BucketScores *out) {
        BucketScores s;
        bool urlLive = false, urlMovie = false, urlSeries = false;
        urlPathHints(url, urlLive, urlMovie, urlSeries);
        if (urlLive) s.live += 3;
        if (urlMovie) s.movie += 3;
        if (urlSeries) s.series += 3;

        std::string ext = urlExtension(url);
        bool vodExt = ext == "mp4" || ext == "mkv" || ext == "avi" || ext == "mov" || ext == "m4v" || ext == "wmv"
                      || ext == "flv" || ext == "webm" || ext == "mpg" || ext == "mpeg" || ext == "divx";
        if (vodExt) {
            s.movie += 1;   // a file extension says VOD, not which kind of VOD
        }

        std::string g = foldKey(group);
        if (!g.empty()) {
            bool series = contains(g, "series") || contains(g, "serie") || contains(g, "novela")
                          || contains(g, "dorama") || contains(g, "anime") || contains(g, "tvshow")
                          || contains(g, "dizi") || contains(g, "temporada");
            bool movie = contains(g, "filme") || contains(g, "movie") || contains(g, "film")
                         || contains(g, "cinema") || contains(g, "vod") || contains(g, "pelicul")
                         || contains(g, "sinema");
            bool live = contains(g, "canais") || contains(g, "channel") || contains(g, "channels")
                        || contains(g, "canali") || contains(g, "tv ao vivo") || contains(g, "live tv")
                        || contains(g, "abierta") || contains(g, "aberto");
            // the provider's own naming: a group whose name STARTS with the channel prefix
            // ("CANAL | …", "CANAIS | …", "CANAIS 24H | …", "CHANNEL …") is a live channel group —
            // VOD groups are named FILMES/SERIES instead
            if (startsWithChannelPrefix(g)) {
                live = true;
            }
            if (series) s.series += 2;
            if (movie) s.movie += 2;
            if (live) s.live += 2;
        }

        int season = 0, episode = 0;
        if (parseEpisodeMarker(name, season, episode)) {
            s.series += 3;
        }
        if (trailingYear(name) > 0) {
            s.movie += 1;
        }
        {
            std::string gk = foldKey(group);
            bool urlVod = urlMovie || urlSeries;
            if (!urlVod && startsWithChannelPrefix(gk)) {
                s.live += 4;   // provider prefix wins over group keywords
            }
        }

        if (out != nullptr) {
            *out = s;
        }
        int best = std::max({s.live, s.movie, s.series});
        if (best == 0) {
            return Bucket::Live;   // no signal: a playlist is a live list by default
        }
        if (s.series == best && s.movie != best) {
            return Bucket::Series;
        }
        if (s.movie == best && s.series != best) {
            return Bucket::Movie;
        }
        if (s.series == best && s.movie == best) {
            return Bucket::Series;   // tie between VOD kinds: episodes are the more specific case
        }
        return Bucket::Live;
    }

}   // namespace iptv

namespace iptv {

    std::string channelIdentityKey(const std::string &name) {
        std::string key = foldKey(name);
        // drop parenthesised / bracketed qualifiers: "(H265)", "[4K]", "(FHD)"
        std::string stripped;
        stripped.reserve(key.size());
        int depth = 0;
        for (unsigned char c: key) {
            if (c == '(' || c == '[') { depth++; continue; }
            if (c == ')' || c == ']') { if (depth > 0) depth--; continue; }
            if (depth == 0) stripped += (char) c;
        }
        // tokenize on non-alphanumerics and drop quality words
        static const char *const NOISE[] = {
            "4k", "uhd", "fhd", "hd", "sd", "h265", "h264", "hevc", "x265", "x264", "vp9", "av1",
            "2160p", "1440p", "1080p", "1080i", "720p", "576i", "576p", "540p", "480p", "360p", "240p"
        };
        std::string words, cur;
        auto flush = [&words, &cur]() {
            if (cur.empty()) return;
            bool noise = false;
            for (const char *n: NOISE) {
                if (cur == n) { noise = true; break; }
            }
            if (!noise) {
                words += cur;
                words += ' ';
            }
            cur.clear();
        };
        for (unsigned char c: stripped) {
            if (std::isalnum(c)) { cur += (char) c; } else { flush(); }
        }
        flush();
        // collapse to a single-spaced token string, then fold it again
        std::string out;
        out.reserve(words.size());
        bool lastSpace = false;
        for (unsigned char c: words) {
            if (c == ' ') {
                if (!lastSpace && !out.empty()) out += ' ';
                lastSpace = true;
            } else {
                out += (char) c;
                lastSpace = false;
            }
        }
        while (!out.empty() && out.back() == ' ') out.pop_back();
        return foldKey(out);
    }

}   // namespace iptv
