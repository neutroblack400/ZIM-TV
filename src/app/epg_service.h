// XMLTV EPG: background download of the guide for the current profile, parsed in streaming and cached to
// disk, so reopening the app costs no network. Sources: the playlist's url-tvg (M3U) or the Xtream panel's
// /xmltv.php. Stalker portals expose no such endpoint here and are skipped.
//
// The guide for providers with hundreds of thousands of channels can be several hundred MB; it is never held
// in memory whole: the HTTP body streams to a temp file, the XML is parsed with a small buffer keeping only
// the programmes of channels from the loaded catalog inside a 72 h window, and the in-memory result is
// bounded (per channel and global caps in xmltv.h). The cache file is compact binary with an FNV checksum.
//
// Callbacks run on the main thread (JobSystem).

#ifndef ZIMTV_APP_EPG_SERVICE_H
#define ZIMTV_APP_EPG_SERVICE_H

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>

#include "../epg/xmltv.h"
#include "../network/jobs.h"

// Main-thread latch: remember one refresh request arriving while an async refresh is in flight.
class EpgRefreshGate {
public:
    bool request() {
        if (refreshing) {
            pending = true;
            return false;
        }
        refreshing = true;
        return true;
    }

    // Returns true when a queued refresh should start after the current one completes.
    bool finish() {
        refreshing = false;
        bool again = pending;
        pending = false;
        return again;
    }

private:
    bool refreshing = false;
    bool pending = false;
};

class EpgService {

public:

    static const int64_t REFRESH_AFTER = 12 * 3600;   // seconds: a saved guide is reused within this window
    static const int64_t WINDOW_PAST = 2 * 3600;      // kept programmes start no earlier than now-2h
    static const int64_t WINDOW_FUTURE = 72 * 3600;   // and no later than now+72h
    static const size_t MAX_DOWNLOAD_BYTES = 1536u * 1024 * 1024;   // 1.5 GB, decompressed
    static const size_t MAX_CACHE_BYTES = 192u * 1024 * 1024;

    struct Outcome {
        bool ok = false;
        std::shared_ptr<epg::Guide> guide;
        bool fromCache = false;         // the network was not used (a fresh cache exists) or not needed
        bool staleCache = false;        // an older cache was used because the network failed
        std::string message;            // when !ok (user-facing)
        bool networkFailure = false;
        int64_t savedAt = 0;            // when the guide was fetched (unix time)
    };

    using Callback = std::function<void(Outcome &)>;

    explicit EpgService(JobSystem &jobs) : jobs(jobs) {}

    // downloads (unless a fresh cache exists), parses and caches the guide; only programmes whose channel id
    // is in `wanted` are kept
    CancelToken refresh(const std::string &profileId, const std::string &url,
                        const std::unordered_set<std::string> &wanted,
                        const std::string &dataDir, Callback callback, bool force = false);

    // cache only (offline start): nullptr when there is no readable cache file
    static std::shared_ptr<epg::Guide> loadCached(const std::string &profileId, const std::string &dataDir);

    static bool hasFresh(const std::string &profileId, const std::string &dataDir, int64_t maxAgeSeconds);

    // Empty programme vectors are valid coverage markers for channels with no listings in the parsed window.
    static bool coversWantedChannels(const epg::Guide &guide, const std::unordered_set<std::string> &wanted) {
        for (const std::string &id : wanted) {
            if (guide.byChannel.find(id) == guide.byChannel.end()) {
                return false;
            }
        }
        return true;
    }

    static std::string cachePath(const std::string &profileId, const std::string &dataDir);

private:

    static bool safeName(const std::string &s);

    JobSystem &jobs;
};

#endif // ZIMTV_APP_EPG_SERVICE_H
