#include "doh.h"

#include <algorithm>
#include <cctype>
#include <curl/curl.h>
#include <mutex>
#include <unordered_map>

#include "../core/json.h"
#include "../platform/clock.h"
#include "../platform/log.h"

namespace doh {

    namespace {

        // well-known DoH servers, reached by IP so resolving them needs no DNS of their own
        const char *const kEndpoints[] = {
                "https://1.1.1.1/dns-query", "https://1.0.0.1/dns-query",               // Cloudflare
                "https://8.8.8.8/dns-query", "https://8.8.4.4/dns-query",               // Google
                "https://9.9.9.9/dns-query", "https://149.112.112.112/dns-query",       // Quad9
        };
        constexpr double kCacheSeconds = 300.0;
        constexpr size_t kMaxReply = 256 * 1024;   // a DoH reply is a few KB; anything bigger is not a reply

        std::string g_caBundle;

        size_t onWrite(char *data, size_t size, size_t count, void *user) {
            auto *out = (std::string *) user;
            size_t n = size * count;
            if (out->size() + n > kMaxReply) {
                return 0;
            }
            out->append(data, n);
            return n;
        }

        std::string lowerStr(std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char) std::tolower(c); });
            return s;
        }

        bool saneHost(const std::string &host) {
            if (host.empty() || host.size() > 253) {
                return false;
            }
            for (char c: host) {
                const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                                || c == '-' || c == '.';
                if (!ok) {
                    return false;
                }
            }
            return true;
        }

        // "1.2.3.4" only; the answer lists are walked defensively anyway
        bool isIpv4(const std::string &s) {
            int dots = 0, at = 0, octet = 0, digits = 0;
            for (char c: s) {
                if (c == '.') {
                    if (digits == 0 || digits > 3 || octet > 255) {
                        return false;
                    }
                    dots++;
                    at++;
                    octet = 0;
                    digits = 0;
                    continue;
                }
                if (c < '0' || c > '9') {
                    return false;
                }
                octet = octet * 10 + (c - '0');
                digits++;
            }
            return dots == 3 && digits > 0 && digits <= 3 && octet <= 255;
        }

        struct CacheEntry {
            double at = 0;
            std::vector<std::string> ips;
        };
        std::unordered_map<std::string, CacheEntry> g_cache;
        std::mutex g_mutex;

        // one DoH query via one endpoint; returns the A records (may be empty)
        std::vector<std::string> query(const char *endpoint, const std::string &host) {
            std::vector<std::string> out;
            CURL *c = curl_easy_init();
            if (c == nullptr) {
                return out;
            }
            std::string url = std::string(endpoint) + "?name=" + host + "&type=A";
            std::string body;
            char errbuf[CURL_ERROR_SIZE] = {0};
            struct curl_slist *headers = curl_slist_append(nullptr, "Accept: application/dns-json");
            curl_easy_setopt(c, CURLOPT_URL, url.c_str());
            curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
            curl_easy_setopt(c, CURLOPT_PROTOCOLS, (long) CURLPROTO_HTTPS);
            curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, 2500L);
            curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, 4500L);
            curl_easy_setopt(c, CURLOPT_NOPROGRESS, 1L);
            curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, onWrite);
            curl_easy_setopt(c, CURLOPT_WRITEDATA, &body);
            curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(c, CURLOPT_USERAGENT, "ZIMTV-IPTV/1.0");
            curl_easy_setopt(c, CURLOPT_ERRORBUFFER, errbuf);
            curl_easy_setopt(c, CURLOPT_BUFFERSIZE, 16L * 1024);
            if (!g_caBundle.empty()) {
                curl_easy_setopt(c, CURLOPT_CAINFO, g_caBundle.c_str());
            }
            CURLcode rc = curl_easy_perform(c);
            long status = 0;
            curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
            curl_slist_free_all(headers);
            curl_easy_cleanup(c);
            if (rc != CURLE_OK || status != 200) {
                LOG_W("doh", "%s: request failed (%s%sHTTP %ld)", endpoint, curl_easy_strerror(rc),
                      errbuf[0] ? ": " : "", status);
                return out;
            }
            json::Value root;
            std::string parseError;
            if (!json::parse(body, root, &parseError) || !root.isObject()) {
                LOG_W("doh", "%s: malformed JSON (%s)", endpoint, parseError.c_str());
                return out;
            }
            const json::Value &status_ = root["Status"];
            if (status_.isNumber() && status_.asInt() != 0) {
                // 1 = FORMERR, 2 = SERVFAIL, 3 = NXDOMAIN: the name does not resolve publicly either
                LOG_I("doh", "%s: RCODE %lld for %s", endpoint, (long long) status_.asInt(), host.c_str());
                return out;
            }
            const json::Value &answer = root["Answer"];
            for (size_t i = 0; i < answer.size(); i++) {
                const json::Value &rec = answer.at(i);
                if (!rec.isObject()) {
                    continue;
                }
                const json::Value &type = rec["type"];
                const json::Value &data = rec["data"];
                if (!type.isNumber() || type.asInt() != 1) {   // 1 = A; CNAMEs (5) are followed by the servers
                    continue;
                }
                std::string ip = data.asString();
                if (isIpv4(ip)) {
                    out.push_back(ip);
                }
            }
            return out;
        }

    }

    void setCaBundle(const std::string &caBundle) {
        g_caBundle = caBundle;
    }

    std::vector<std::string> lookup(const std::string &host) {
        std::string h = lowerStr(host);
        if (!saneHost(h)) {
            return {};
        }
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            auto it = g_cache.find(h);
            if (it != g_cache.end() && clockx::monotonic() - it->second.at < kCacheSeconds) {
                return it->second.ips;
            }
        }
        std::vector<std::string> out;
        for (const char *endpoint: kEndpoints) {
            out = query(endpoint, h);
            if (!out.empty()) {
                break;
            }
        }
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_cache[h] = {clockx::monotonic(), out};
        }
        if (out.empty()) {
            LOG_W("doh", "%s: no address from any DoH server", h.c_str());
        }
        return out;
    }

    void clearCache() {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_cache.clear();
    }

}
