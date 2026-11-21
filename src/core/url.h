// URL helpers: percent-encoding and Xtream server address normalization.

#ifndef ZIMTV_CORE_URL_H
#define ZIMTV_CORE_URL_H

#include <string>

namespace url {

    // RFC 3986 percent-encoding: unreserved characters (A-Z a-z 0-9 - . _ ~) are kept.
    std::string encode(const std::string &s);

    struct Server {
        bool ok = false;
        std::string error;       // user-facing reason when !ok
        std::string base;        // scheme://host[:port][/prefix] without trailing slash
        std::string scheme;      // "http" / "https"
        std::string host;
        std::string port;        // empty = scheme default
        std::string displayHost; // host[:port] for UI
    };

    // Accepts "host", "host:port", "http(s)://host[:port][/path]" and pasted Xtream links such as
    // ".../player_api.php?..." or ".../get.php?...". Defaults to http.
    Server normalizeServer(const std::string &input);

    // Removes zero-width / formatting characters that never belong in a URL or a host name (BOM, zero-width
    // spaces, word joiners, directional marks, ...).
    std::string stripFormat(const std::string &input);

    std::string lower(std::string s);

    std::string trim(const std::string &s);

    // "http://user:pass@host:8080/path?q" -> host "host", port 8080; the scheme default port when none is given.
    // False when the authority is missing or unusable (not a name or an IPv4 literal).
    bool hostPort(const std::string &url, std::string &host, int &port);
}

#endif // ZIMTV_CORE_URL_H
