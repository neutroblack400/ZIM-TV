// M3U / M3U8 playlist sources on the job system (fetch, parse, catalog, saved copy). Callbacks run on the
// main thread.
//
// Opening a playlist source shows the saved copy at once (Source::Cache) and refreshes it in the background
// when it is older than REFRESH_AFTER (App::refreshPlaylist). A refresh (Source::Network) downloads into
// memory, parses and validates on a worker; only a playlist with at least one channel replaces the saved copy
// and the list on screen. A failed download / an unreadable answer never touches either: the source is never
// blanked because the network failed.
//
// Remote playlists come over the existing libcurl client (http and https, redirects, gzip / deflate, connect /
// transfer / stall timeouts, cancellation, HTTP status checks); local ones only from
// /data/ZIMTV/playlists/. Playlist URLs are never logged: only m3u::displayUrl() (host and file name).

#ifndef ZIMTV_APP_M3U_SERVICE_H
#define ZIMTV_APP_M3U_SERVICE_H

#include <functional>
#include <memory>
#include <string>

#include "../iptv/catalog.h"
#include "../iptv/m3u.h"
#include "../iptv/models.h"
#include "../network/jobs.h"

#include <unordered_map>

class M3uService {

public:

    static const int64_t REFRESH_AFTER = 12 * 3600;   // seconds: the saved copy is refreshed in the background
    static constexpr const char *CACHE_NAME = "m3u_playlist";
    // large IPTV playlists are routinely 60-200+ MB (hundreds of thousands of channels)
    static const size_t MAX_BYTES = 256u * 1024 * 1024;

    explicit M3uService(JobSystem &jobs) : jobs(jobs) {}

    enum class Source {
        Cache,      // the saved copy only (fast, offline)
        Network     // download (or read the local file); a valid playlist replaces the saved copy
    };

    struct Outcome {
        bool ok = false;
        std::shared_ptr<iptv::LiveCatalog> catalog;   // Live entries only (built on the worker)
        m3u::Info info;
        // Movies / Series derived from the same entries (Live/Movie/Series heuristics); null on failure
        std::shared_ptr<iptv::MovieCatalog> movies;
        std::shared_ptr<iptv::SeriesCatalog> series;
        std::shared_ptr<const std::unordered_map<std::string, std::shared_ptr<const iptv::SeriesInfo>>> seriesInfo;
        std::string message;            // user-facing error ("" when ok)
        bool networkFailure = false;    // could not download (vs. downloaded something unusable)
        std::shared_ptr<std::string> body;   // Network + keepBody: the downloaded playlist (editor test)
    };

    using Callback = std::function<void(Outcome &)>;

    // save: write a valid downloaded playlist as the saved copy of profile.id (no effect for Source::Cache)
    CancelToken load(const iptv::Profile &profile, const std::string &dataDir, Source source, bool save,
                     Callback callback, bool keepBody = false);

    // saved copy of a playlist downloaded by the editor's test, for a profile saved afterwards
    static bool saveBody(const std::string &dataDir, const std::string &profileId, const std::string &body,
                         int channels, int64_t now);

    // user-facing text of a parse error
    static std::string errorText(m3u::Error error);

    // the playlist location is something the source can load (http(s) URL or an allowed local file)
    static bool validLocation(const std::string &location);

private:

    JobSystem &jobs;
};

#endif // ZIMTV_APP_M3U_SERVICE_H
