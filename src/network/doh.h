// DNS-over-HTTPS fallback: when the console's resolver cannot resolve a host (broken, filtered or hijacked DNS),
// the app resolves the name itself via a DoH server reached by IP (resolving the DoH servers needs no DNS).
// The addresses are pinned per request with CURLOPT_RESOLVE, so the TLS handshake (SNI) and the Host header
// keep the original name. Used only when the system resolution fails; the console's resolver stays in charge
// whenever it works.

#ifndef ZIMTV_NETWORK_DOH_H
#define ZIMTV_NETWORK_DOH_H

#include <string>
#include <vector>

namespace doh {

    // The path of the CA bundle used to verify the DoH servers' certificates (called once at startup
    // from http::globalInit, before any request).
    void setCaBundle(const std::string &caBundle);

    // The IPv4 addresses for `host` from a public DoH server (Cloudflare, Google and Quad9, tried in order).
    // An empty result means the name does not resolve publicly either, or every server was unreachable.
    // Short-lived cache; hostnames only (IPv4 literals resolve without DNS and never reach this).
    std::vector<std::string> lookup(const std::string &host);

    // drops the cache (host tests)
    void clearCache();

}

#endif // ZIMTV_NETWORK_DOH_H
