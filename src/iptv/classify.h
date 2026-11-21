// Heuristic classification of M3U entries into Live TV / Movie / Series (host-testable, no networking).
//
// Why: an Xtream panel exported as M3U (get.php?...&type=m3u_plus) routinely mixes live channels, movies and
// series episodes in one list. The entries themselves carry the hints; nothing about content is invented:
//
//   1. URL path segments (Xtream exports):  /live/... -> Live,  /movie/... -> Movie,  /series/... -> Series
//   2. group-title keywords (pt/es/en/tr):  filme / movie / film -> Movie; serie / series / novela / dorama / anime -> Series;
//                                           canais / channels / live tv -> Live
//   3. episode pattern in the name ("S01E02", "1x02", "Season 2 Episode 3") -> Series
//   4. container extension of the media URL (.mkv .mp4 .avi ...) -> VOD; the series signals above win, the
//      remaining entries are movies
//   5. no signal at all -> Live (the playlist's native meaning)
//
// Release groups ("Lançamentos", "Estreias", "Novidades", "Recent", "New"...) are detected separately
// (releaseHint) so the Home screen can feature premieres; they are not a content-type signal by themselves.
//
// The weights are deliberately conservative: an explicit URL segment or an episode pattern outranks a
// group keyword; a group keyword outranks a bare file extension; a tie between Movie and Series goes to
// Series (episodes are the more specific case). Ambiguous results are counted and logged by the caller
// (classifyStats) so the keyword tables can be tuned against real playlists.

#ifndef ZIMTV_IPTV_CLASSIFY_H
#define ZIMTV_IPTV_CLASSIFY_H

#include <string>

namespace iptv {

    enum class Bucket {
        Live = 0,
        Movie = 1,
        Series = 2
    };

    struct BucketScores {
        int live = 0;
        int movie = 0;
        int series = 0;
    };

    // the bucket for one entry, and (optionally) the raw scores behind the decision
    Bucket classifyEntry(const std::string &group, const std::string &name, const std::string &url,
                         BucketScores *scores = nullptr);

    // true when the group looks like a "new releases / premieres" group (Home features it)
    bool releaseHint(const std::string &group);

    // a folded ASCII key for keyword matching (lower case, diacritics reduced, non-ASCII dropped)
    std::string foldKey(const std::string &text);

    // a stable identity key for a live channel: same emitter, different resolutions/quality tags
    // ("Discovery Channel 4K", "Discovery Channel FHD (H265)", "Discovery Channel HD" -> "discovery channel").
    // Used to share a channel's logo between its variants when one of them has no tvg-logo.
    std::string channelIdentityKey(const std::string &name);

    // the media URL's path extension in lower case ("mkv"), "" when there is none or it is implausible
    std::string urlExtension(const std::string &url);

    // parse the episode marker out of a name: "S01E02", "S01 E02", "s1e2", "1x02", "Season 2 Episode 3",
    // "Temporada 2 Episodio 3". Returns true and fills season/episode (1-based) on a match.
    bool parseEpisodeMarker(const std::string &name, int &season, int &episode);

    // trailing "(2024)" or " 2024" at the end of a name -> the year (0 when absent)
    int trailingYear(const std::string &name);

    // the name before the episode marker, trailing separators stripped, the year KEPT ("Show 2024")
    std::string cutAtMarker(const std::string &name);

    // the name with the episode marker and a trailing year removed: the series / movie title
    std::string titleBeforeMarker(const std::string &name);

    // the name with a trailing year removed ("(2024)" or " 2024"); unchanged when there is none
    std::string stripTrailingYear(const std::string &name);

    // the text after the episode marker ("A hora do caos"), trimmed of separators; the whole name when there is none
    std::string titleAfterMarker(const std::string &name);

}   // namespace iptv

#endif   // ZIMTV_IPTV_CLASSIFY_H
