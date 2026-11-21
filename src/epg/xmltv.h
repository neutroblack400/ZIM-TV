// XMLTV (EPG) streaming parser: reads a file of any size with a bounded memory footprint and keeps only
// what the guide needs -- the programmes of the channels from a wanted set, inside a time window, capped
// globally and per channel. No DOM, no third-party XML library: a tokenizer for the XMLTV dialect.
//
//   <tv>
//     <channel id="bbc1">...</channel>
//     <programme start="20261114190000 +0000" stop="20261114200000 +0000" channel="bbc1">
//       <title lang="en">News</title>
//       <desc>Daily news</desc>
//     </programme>
//   </tv>
//
// Time: XMLTV timestamps are "YYYYMMDDHHMMSS[.fff] [[+-]HHMM | Z]". A missing offset is treated as UTC.
// Malformed programmes are skipped, never fatal. Text is treated as UTF-8 (entity and numeric references
// decoded); over-long values are cut on a UTF-8 boundary.

#ifndef ZIMTV_EPG_XMLTV_H
#define ZIMTV_EPG_XMLTV_H

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace epg {

struct Program {
    int64_t start = 0;
    int64_t stop = 0;
    std::string title;
    std::string desc;
};

struct Guide {
    // epgId (tvg-id / epg_channel_id) -> programmes inside the requested window, ordered by start
    std::unordered_map<std::string, std::vector<Program>> byChannel;
    int64_t savedAt = 0;         // unix time the guide was fetched / read from the cache
    int64_t programmes = 0;      // kept programmes
    bool truncated = false;      // the global programme cap stopped the parse early
};

struct ParseStats {
    int64_t programmesSeen = 0;
    int64_t programmesKept = 0;
    int64_t skippedBadTime = 0;
    int64_t skippedEmptyTitle = 0;
    int64_t skippedChannelCap = 0;
    int64_t skippedGlobalCap = 0;
};

// Parses `path` (a plain XML file; gzip files must be decompressed by the caller) keeping the programmes
// whose channel id returns true from keepChannel, starting at or after `windowStart` and ending before
// `windowEnd`. Stop times are clamped to the window. `maxProgrammes` caps the total kept (0 = default).
// Per channel at most 40 programmes are kept. Throws std::runtime_error only if the file cannot be opened.
Guide parseFile(const std::string &path,
                const std::function<bool(const std::string &)> &keepChannel,
                int64_t windowStart, int64_t windowEnd,
                size_t maxProgrammes = 0, ParseStats *stats = nullptr);

// "20261114193000 -0300" / "20261114193000Z" / "20261114193000" -> unix time. Returns false when malformed.
bool parseXmltvTime(const std::string &text, int64_t &out);

}

#endif // ZIMTV_EPG_XMLTV_H
