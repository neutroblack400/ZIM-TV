#include <curl/curl.h>

#include <chrono>
#include <condition_variable>
#include <mutex>

#include "http.h"
#include "http_policy.h"
#include "doh.h"
#include "../core/url.h"
#include "../i18n/i18n.h"
#include "../platform/clock.h"
#include "../platform/log.h"

#include <vector>
#include <cstdio>
#include <cstdarg>
#include <unistd.h>

// TLS diagnostics hook: the patched libcurl hands TLS verification diagnostics to this logger.
// The hook is weak in the library (a no-op default); this strong definition routes the lines
// to the app's own channel, which is proven to reach the console log.
extern "C" void ps4_diag_line(const char *fmt, ...) {
    char buf[1600];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    LOG_I("tlsdiag", "%s", buf);
}

namespace http {

    namespace {
        std::string g_caBundle;
        std::string g_userAgent = "PS4IPTV";

        struct Transfer {
            std::string *body;      // memory sink (nullptr when sinking to a file)
            FILE *sink = nullptr;   // file sink (getToFile)
            size_t written = 0;     // bytes through sink/body
            size_t maxBytes;
            bool tooLarge = false;
            bool ioError = false;   // the sink failed (disk full / I/O error)
            CancelFlag cancel;
        };

        size_t onWrite(char *data, size_t size, size_t count, void *user) {
            auto *t = (Transfer *) user;
            size_t n = size * count;
            if (t->written + n > t->maxBytes) {
                t->tooLarge = true;
                return 0;  // aborts the transfer (CURLE_WRITE_ERROR)
            }
            if (t->sink != nullptr) {
                size_t w = std::fwrite(data, 1, n, t->sink);
                if (w != n) {
                    t->ioError = true;
                    return 0;  // aborts the transfer
                }
                t->written += n;
                return n;
            }
            t->body->append(data, n);
            t->written = t->body->size();
            return n;
        }

        int onProgress(void *user, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
            auto *t = (Transfer *) user;
            return t->cancel && t->cancel->load() ? 1 : 0;  // non-zero aborts (CURLE_ABORTED_BY_CALLBACK)
        }

        // one easy handle per worker thread: keeps connections alive between requests. A plain pointer (a
        // thread_local object with a destructor needs __cxa_thread_atexit_impl, which the PS4 runtime lacks):
        // releaseThreadResources() frees it when the worker ends (JobSystem::setWorkerExitHook).
        thread_local CURL *t_handle = nullptr;

        CURL *threadHandle() {
            if (t_handle == nullptr) {
                t_handle = curl_easy_init();
            } else {
                curl_easy_reset(t_handle);
            }
            return t_handle;
        }

        bool canceled(const Request &req) {
            return req.cancel && req.cancel->load();
        }

        // scheme://host:port only: request addresses can carry credentials
        std::string originOf(const std::string &url) {
            httppolicy::Origin o;
            if (!httppolicy::parseOrigin(url, o)) {
                return "?";
            }
            return o.scheme + "://" + o.host + ":" + std::to_string(o.port);
        }

        Error mapCurl(CURLcode rc, bool tooLarge) {
            switch (rc) {
                case CURLE_ABORTED_BY_CALLBACK:
                    return Error::Canceled;
                case CURLE_WRITE_ERROR:
                    return tooLarge ? Error::TooLarge : Error::Other;
                case CURLE_OPERATION_TIMEDOUT:
                    return Error::Timeout;
                case CURLE_COULDNT_RESOLVE_HOST:
                    return Error::Dns;
                case CURLE_COULDNT_CONNECT:
                    return Error::Connect;
                case CURLE_UNSUPPORTED_PROTOCOL:
                    return Error::HttpsUnsupported;
                case CURLE_PEER_FAILED_VERIFICATION:   // 7.80: also the former CURLE_SSL_CACERT (unknown issuer)
                case CURLE_SSL_CACERT_BADFILE:
                case CURLE_SSL_ISSUER_ERROR:
                case CURLE_SSL_CRL_BADFILE:
                    return Error::TlsCertificate;
                case CURLE_SSL_CONNECT_ERROR:
                case CURLE_SSL_CERTPROBLEM:
                case CURLE_SSL_CIPHER:
                case CURLE_USE_SSL_FAILED:
                case CURLE_SSL_SHUTDOWN_FAILED:
                    return Error::Tls;
                case CURLE_RECV_ERROR:
                case CURLE_SEND_ERROR:
                case CURLE_GOT_NOTHING:
                case CURLE_PARTIAL_FILE:
                case CURLE_HTTP2:
                case CURLE_HTTP2_STREAM:
                    return Error::Interrupted;
                default:
                    return Error::Other;
            }
        }

        httppolicy::Failure failureOf(Error e) {
            switch (e) {
                case Error::None:
                    return httppolicy::Failure::None;
                case Error::Canceled:
                    return httppolicy::Failure::Canceled;
                case Error::Timeout:
                    return httppolicy::Failure::Timeout;
                case Error::Dns:
                    return httppolicy::Failure::Dns;
                case Error::Connect:
                    return httppolicy::Failure::Connect;
                case Error::Interrupted:
                    return httppolicy::Failure::Interrupted;
                case Error::Tls:
                    return httppolicy::Failure::Tls;
                case Error::TlsCertificate:
                    return httppolicy::Failure::TlsCertificate;
                case Error::TooLarge:
                    return httppolicy::Failure::TooLarge;
                case Error::Redirect:
                    return httppolicy::Failure::Redirect;
                default:
                    return httppolicy::Failure::Other;
            }
        }

        const char *refusal(httppolicy::Redirect v) {
            switch (v) {
                case httppolicy::Redirect::Downgrade:
                    return "https to http";
                case httppolicy::Redirect::OtherHost:
                    return "another host";
                case httppolicy::Redirect::BadScheme:
                    return "not http / https";
                default:
                    return "invalid address";
            }
        }

        // rewind a sink for a fresh attempt (redirect retry / request retry): no stale bytes from a failed try
        void sinkReset(FILE *sink) {
            if (sink == nullptr) {
                return;
            }
            std::fflush(sink);
            if (ftruncate(fileno(sink), 0) != 0) {
                std::perror("ftruncate");
            }
            std::rewind(sink);
        }

        // one attempt: the request and the redirects the policy allows, each hop checked before it is made
        Response attempt(const Request &req, double started, FILE *sink = nullptr) {
            Response r;
            std::string url = req.url;
            bool sameHostOnly = !req.headers.empty();
            struct curl_slist *headers = nullptr;
            for (const auto &h: req.headers) {
                headers = curl_slist_append(headers, h.c_str());
            }
            for (int hop = 0;; hop++) {
                CURL *c = threadHandle();   // reset for every hop (the connection cache stays)
                if (c == nullptr) {
                    r.error = Error::Other;
                    r.detail = "curl_easy_init failed";
                    break;
                }
                sinkReset(sink);
                Transfer t;
                r.body.clear();
                t.body = &r.body;
                t.sink = sink;
                t.written = 0;
                t.maxBytes = req.maxBytes;
                t.cancel = req.cancel;
                char errbuf[CURL_ERROR_SIZE] = {0};
                // redirect hops share the request's time budget
                long left = req.totalTimeoutMs - (long) ((clockx::monotonic() - started) * 1000.0);
                if (req.totalTimeoutMs > 0 && left <= 0) {
                    r.error = Error::Timeout;
                    r.detail = "time budget used up";
                    break;
                }
                curl_easy_setopt(c, CURLOPT_URL, url.c_str());
                curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
                // only http / https, also for anything a server redirects to (http_policy.h)
                curl_easy_setopt(c, CURLOPT_PROTOCOLS, (long) (CURLPROTO_HTTP | CURLPROTO_HTTPS));
                curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS, (long) (CURLPROTO_HTTP | CURLPROTO_HTTPS));
                curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 0L);   // each redirect is checked below
                // The console's resolver answers some hosts with IPv6 addresses its route cannot reach
                // (e.g. BunnyCDN-backed artwork fails with HTTP 0 while IPv4 hosts work): prefer IPv4.
                curl_easy_setopt(c, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
                curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, req.connectTimeoutMs);
                curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, req.totalTimeoutMs > 0 ? left : 0L);
                curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1L);
                curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, req.stallTimeoutS);
                curl_easy_setopt(c, CURLOPT_USERAGENT, req.userAgent.empty() ? g_userAgent.c_str() : req.userAgent.c_str());
                curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");  // gzip/deflate: Xtream JSON lists shrink a lot
                curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, onWrite);
                curl_easy_setopt(c, CURLOPT_WRITEDATA, &t);
                curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, onProgress);
                curl_easy_setopt(c, CURLOPT_XFERINFODATA, &t);
                curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
                curl_easy_setopt(c, CURLOPT_ERRORBUFFER, errbuf);
                curl_easy_setopt(c, CURLOPT_BUFFERSIZE, 64L * 1024);
                curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);   // libcurl's defaults, stated: never switched off
                curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
                if (!g_caBundle.empty()) {
                    curl_easy_setopt(c, CURLOPT_CAINFO, g_caBundle.c_str());
                }
                if (headers != nullptr) {
                    curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
                }
                CURLcode rc = curl_easy_perform(c);
                if (rc == CURLE_COULDNT_RESOLVE_HOST) {
                    // the console's resolver cannot resolve the host (or the name does not resolve publicly):
                    // ask a DoH server reached by IP and pin its addresses for this request — the TLS
                    // handshake and the Host header keep the original name
                    std::string host;
                    int port = 0;
                    if (url::hostPort(url, host, port)) {
                        std::vector<std::string> ips = doh::lookup(host);
                        if (!ips.empty()) {
                            struct curl_slist *rl = nullptr;
                            for (const std::string &ip: ips) {
                                std::string entry = host + ":" + std::to_string(port) + ":" + ip;
                                rl = curl_slist_append(rl, entry.c_str());
                            }
                            LOG_I("http", "%s: console DNS cannot resolve the host; DoH resolved %zu address(es), retrying pinned",
                                  originOf(url).c_str(), ips.size());
                            sinkReset(sink);
                            r.body.clear();
                            t.body = &r.body;
                            t.written = 0;
                            t.tooLarge = false;
                            t.ioError = false;
                            curl_easy_setopt(c, CURLOPT_RESOLVE, rl);
                            rc = curl_easy_perform(c);
                            curl_easy_setopt(c, CURLOPT_RESOLVE, nullptr);
                            curl_slist_free_all(rl);
                        } else {
                            LOG_W("http", "%s: console DNS failed and no DoH server resolved the host", host.c_str());
                        }
                    }
                }
                r.bodyBytes = sink != nullptr ? (uint64_t) t.written : (uint64_t) r.body.size();
                if (headers != nullptr) {
                    curl_easy_setopt(c, CURLOPT_HTTPHEADER, nullptr);   // the handle is reused by the next request
                }
                r.status = 0;
                curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &r.status);
                {
                    long vr = -1;
                    if (curl_easy_getinfo(c, CURLINFO_SSL_VERIFYRESULT, &vr) == CURLE_OK) {
                        r.sslVerifyResult = vr;   // TLS diagnostics instrumentation
                    }
                }
                char *ct = nullptr;
                r.contentType.clear();
                if (curl_easy_getinfo(c, CURLINFO_CONTENT_TYPE, &ct) == CURLE_OK && ct) {
                    r.contentType = ct;
                }
                if (t.ioError && rc != CURLE_OK) {
                    r.error = Error::Other;
                    r.detail = "cannot write the response to disk (storage full?)";
                    break;
                }
                if (rc != CURLE_OK) {
                    r.error = mapCurl(rc, t.tooLarge);
                    r.curlCode = (long) rc;                       // curl code for the log (TLS diagnostics)
                    r.detail = std::string(curl_easy_strerror(rc)) + (errbuf[0] ? std::string(": ") + errbuf : "");
                    break;
                }
                char *location = nullptr;
                bool redirect = r.status >= 300 && r.status < 400 && r.status != 304
                                && curl_easy_getinfo(c, CURLINFO_REDIRECT_URL, &location) == CURLE_OK && location;
                if (!redirect) {
                    break;
                }
                std::string next = location;
                if (hop >= httppolicy::MAX_REDIRECTS) {
                    r.error = Error::Redirect;
                    r.detail = "more than " + std::to_string(httppolicy::MAX_REDIRECTS) + " redirects";
                    break;
                }
                httppolicy::Redirect v = httppolicy::checkRedirect(url, next, req.credentials, sameHostOnly);
                if (v != httppolicy::Redirect::Follow) {
                    r.error = Error::Redirect;
                    r.downgradeRefused = v == httppolicy::Redirect::Downgrade;
                    size_t sep = next.find("://");
                    r.detail = std::string("redirect refused (") + refusal(v) + "): " + originOf(url) + " -> "
                               + (v == httppolicy::Redirect::BadScheme && sep != std::string::npos
                                  ? next.substr(0, sep) + "://" : originOf(next));
                    break;
                }
                url = next;
            }
            curl_slist_free_all(headers);
            r.redirectedToHttps = req.url.compare(0, 8, "https://") != 0 && url.compare(0, 8, "https://") == 0;
            return r;
        }
    }

    bool globalInit(const std::string &caBundle, const std::string &userAgent) {
        g_caBundle = caBundle;
        g_userAgent = userAgent;
        doh::setCaBundle(caBundle);   // the DoH fallback authenticates the resolver servers with the same bundle
        CURLcode rc = curl_global_init(CURL_GLOBAL_ALL);
        curl_version_info_data *v = curl_version_info(CURLVERSION_NOW);
        LOG_I("http", "curl_global_init: %d (%s); libcurl %s, %s, features 0x%x", (int) rc, curl_easy_strerror(rc),
              v->version, v->ssl_version ? v->ssl_version : "no TLS", v->features);
        return rc == CURLE_OK;
    }

    void releaseThreadResources() {
        if (t_handle != nullptr) {
            curl_easy_cleanup(t_handle);
            t_handle = nullptr;
        }
    }

    void globalShutdown() {
        curl_global_cleanup();
    }

    const std::string &userAgent() {
        return g_userAgent;
    }

    const std::string &caBundle() {
        return g_caBundle;
    }

    static Response getInternal(const Request &req, FILE *sink) {
        double started = clockx::monotonic();
        Response r;
        if (!httppolicy::schemeAllowed(req.url)) {
            r.error = Error::HttpsUnsupported;
            r.detail = "only http:// and https:// addresses are requested";
            r.attempts = 0;
            return r;
        }
        int maxRetries = req.retries < 0 ? 0 : req.retries > httppolicy::MAX_RETRIES ? httppolicy::MAX_RETRIES : req.retries;
        for (int retry = 0;; retry++) {
            sinkReset(sink);
            r = attempt(req, started, sink);
            r.attempts = retry + 1;
            double elapsed = clockx::monotonic() - started;
            r.seconds = elapsed;
            if (!httppolicy::shouldRetry(failureOf(r.error), r.status, elapsed, req.totalTimeoutMs, retry, maxRetries,
                                         canceled(req))) {
                break;
            }
            double pause = httppolicy::backoffSeconds(retry + 1);
            std::string why = r.error == Error::None ? "HTTP " + std::to_string(r.status) : r.detail;
            LOG_I("http", "%s: %s - retry %d of %d in %.2fs", originOf(req.url).c_str(), why.c_str(), retry + 1,
                  maxRetries, pause);
            // the pause in small steps: a cancel (screen closed, source switched) ends it at once. A timed condition
            // wait like the download worker's (hardware-proven), not this_thread::sleep_for (musl's nanosleep path),
            // and the deadline from clockx::monotonic (steady_clock is process CPU time on the PS4)
            double until = clockx::monotonic() + pause;
            std::mutex m;
            std::condition_variable cv;
            std::unique_lock<std::mutex> lock(m);
            while (clockx::monotonic() < until && !canceled(req)) {
                cv.wait_for(lock, std::chrono::milliseconds(25));
            }
            if (canceled(req)) {
                int done = retry + 1;
                r = Response();
                r.error = Error::Canceled;
                r.detail = "canceled";
                r.attempts = done;
                r.seconds = clockx::monotonic() - started;
                break;
            }
        }
        LOG_I("http", "%s: HTTP %ld, %llu bytes in %.2fs", originOf(req.url).c_str(), r.status,
              (unsigned long long) (sink != nullptr ? r.bodyBytes : (uint64_t) r.body.size()), r.seconds);
        return r;
    }

    Response get(const Request &req) {
        return getInternal(req, nullptr);
    }

    Response getToFile(const Request &req, const std::string &path) {
        FILE *f = std::fopen(path.c_str(), "wb");
        if (f == nullptr) {
            Response r;
            r.error = Error::Other;
            r.detail = "cannot open the download file for writing";
            return r;
        }
        Response r = getInternal(req, f);
        if (std::fflush(f) != 0) {
            r.error = Error::Other;
            r.detail = "cannot flush the download file (storage full?)";
        }
        std::fclose(f);
        if (!r.ok()) {
            remove(path.c_str());   // never leave a half answer behind
        }
        return r;
    }

    std::string describe(const Response &r) {
        switch (r.error) {
            case Error::None:
                return r.status >= 200 && r.status < 300 ? i18n::tr("net.ok")
                                                         : i18n::tr("net.http_error", {std::to_string(r.status)});
            case Error::Canceled:
                return i18n::tr("net.canceled");
            case Error::Timeout:
                return i18n::tr("net.timeout");
            case Error::Dns:
                return i18n::tr("net.dns");
            case Error::Connect:
                return i18n::tr("net.connect");
            case Error::Tls:
                return i18n::tr("net.tls");
            case Error::TlsCertificate:
                return i18n::tr("net.tls_certificate");
            case Error::Interrupted:
                return i18n::tr("net.interrupted");
            case Error::Redirect:
                return i18n::tr(r.downgradeRefused ? "net.https_downgrade" : "net.unsafe_redirect");
            case Error::TooLarge:
                return i18n::tr("net.too_large");
            case Error::HttpsUnsupported:
                return i18n::tr("net.unsupported_protocol");
            default:
                return i18n::tr("net.failed");
        }
    }
}
