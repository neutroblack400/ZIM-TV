#include "xmltv.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace epg {

namespace {

// days-from-civil (Howard Hinnant's algorithm): Y/M/D -> days since 1970-01-01, no timezone database needed
int64_t daysFromCivil(int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned) (y - era * 400);                                    // [0, 399]
    const unsigned doy = (153 * (m + (m > 2 ? static_cast<unsigned>(-3) : 9)) + 2) / 5 + d - 1;   // [0, 365]
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;                        // [0, 146096]
    return era * 146097 + (int64_t) doe - 719468;
}

bool allDigits(const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------- tokenizer
struct Attr { std::string name, value; };

enum class Tok { StartElement, EndElement, Text, None };

class XmlReader {
public:
    explicit XmlReader(const std::string &path) : f(std::fopen(path.c_str(), "rb")) {
        if (f == nullptr) {
            throw std::runtime_error("cannot open " + path);
        }
    }

    ~XmlReader() {
        if (f != nullptr) {
            std::fclose(f);
        }
    }

    XmlReader(const XmlReader &) = delete;
    XmlReader &operator=(const XmlReader &) = delete;

    Tok next() {
        for (;;) {
            if (pos >= len && !refill()) {
                return Tok::None;
            }
            char c = buf[pos];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                pos++;
                continue;
            }
            if (c == '<') {
                if (!ensure(4)) {
                    // "<" as the very last byte of the file: nothing more to read
                    return Tok::None;
                }
                if (startsWith("<!--")) {
                    pos += 4;
                    skipComment();
                    continue;
                }
                if (startsWith("<?")) {
                    pos += 2;
                    skipUntil("?>");
                    continue;
                }
                if (startsWith("<![CDATA[")) {
                    pos += 9;
                    text.clear();
                    readCdata();
                    return Tok::Text;
                }
                if (startsWith("<!")) {
                    pos += 2;
                    skipDoctype();
                    continue;
                }
                pos++;   // '<'
                if (pos < len && buf[pos] == '/') {
                    pos++;
                    name.clear();
                    readName(name, 128);
                    skipTo('>');
                    return Tok::EndElement;
                }
                readStartElement();
                return Tok::StartElement;
            }
            readText();
            return Tok::Text;
        }
    }

    const std::string &tokenName() const { return name; }
    const std::string &tokenText() const { return text; }

    const std::string *attrValue(const char *key) const {
        for (const Attr &a : attrs) {
            if (a.name == key) {
                return &a.value;
            }
        }
        return nullptr;
    }

    bool selfClosing() const { return SelfClosing; }

private:
    static constexpr size_t BUF = 1 << 17;   // 128 KiB
        FILE *f = nullptr;
        char buf[BUF];
        size_t pos = 0;
        size_t len = 0;
        bool eof = false;

        std::string name;
        std::vector<Attr> attrs;
        std::string text;
        bool SelfClosing = false;

        // pull more bytes; the remaining tail is compacted to the front first
        bool refill() {
            if (pos > 0) {
                std::memmove(buf, buf + pos, len - pos);
                len -= pos;
                pos = 0;
            }
            size_t got = std::fread(buf + len, 1, BUF - len, f);
            if (got == 0) {
                eof = true;
                return false;
            }
            len += got;
            return true;
        }

        // make sure `n` more bytes are visible from pos, or we are at EOF
        bool ensure(size_t n) {
            while (len - pos < n) {
                if (eof) {
                    return len > pos;   // remaining bytes, if any, are returned as-is
                }
                if (!refill()) {
                    return len > pos;
                }
            }
            return true;
        }

        bool startsWith(const char *word) const {
            size_t n = std::strlen(word);
            if (len - pos < n) {
                return false;
            }
            return std::memcmp(buf + pos, word, n) == 0;
        }

        bool atEnd() {
            if (pos < len) {
                return false;
            }
            return !refill();
        }

        // raw text up to '<' or EOF; overlong input is cut at a UTF-8 boundary and the rest is consumed
        void readText() {
            static constexpr size_t kMaxText = 4096;
            text.clear();
            for (;;) {
                if (pos >= len && !refill()) {
                    return;
                }
                char c = buf[pos];
                if (c == '<') {
                    return;
                }
                if (text.size() < kMaxText) {
                    text += c;
                }
                pos++;
            }
        }

        void readCdata() {
            text.clear();
            const char *end = "]]>";
            size_t matched = 0;
            for (;;) {
                if (pos >= len && !refill()) {
                    return;   // unterminated CDATA: keep what we have
                }
                char c = buf[pos++];
                if (end[matched] == c) {
                    matched++;
                    if (matched == 3) {
                        text.resize(text.size() >= 2 ? text.size() - 2 : 0);   // drop the "]]" we appended
                        return;
                    }
                    if (text.size() < 8192) {
                        text += c;
                    }
                    continue;
                }
                matched = 0;
                if (text.size() < 8192) {
                    text += c;
                }
            }
        }

        void readName(std::string &out, size_t maxLen) {
            for (;;) {
                if (pos >= len && !refill()) {
                    return;
                }
                char c = buf[pos];
                if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '>' || c == '/' || c == '=') {
                    return;
                }
                if (out.size() < maxLen) {
                    out += c;
                }
                pos++;
            }
        }

        void skipTo(char end) {
            for (;;) {
                if (pos >= len && !refill()) {
                    return;
                }
                if (buf[pos++] == end) {
                    return;
                }
            }
        }

        void skipUntil(const char *word) {
            size_t n = std::strlen(word);
            size_t matched = 0;
            for (;;) {
                if (pos >= len && !refill()) {
                    return;
                }
                char c = buf[pos++];
                matched = (c == word[matched]) ? matched + 1 : (c == word[0] ? 1 : 0);
                if (matched == n) {
                    return;
                }
            }
        }

        void skipComment() {
            skipUntil("-->");
        }

        void skipDoctype() {
            int depth = 0;
            for (;;) {
                if (pos >= len && !refill()) {
                    return;
                }
                char c = buf[pos++];
                if (c == '[') {
                    depth++;
                } else if (c == ']') {
                    if (depth > 0) {
                        depth--;
                    }
                } else if (c == '>' && depth == 0) {
                    return;
                }
            }
        }

        void readStartElement() {
            name.clear();
            attrs.clear();
            SelfClosing = false;
            readName(name, 128);
            for (;;) {
                skipWs();
                if (pos >= len) {
                    if (!refill()) {
                        return;
                    }
                }
                char c = buf[pos];
                if (c == '>') {
                    pos++;
                    return;
                }
                if (c == '/') {
                    pos++;
                    skipWs();
                    if (pos < len || refill()) {
                        if (buf[pos] == '>') {
                            pos++;
                        }
                    }
                    SelfClosing = true;
                    return;
                }
                std::string attrName;
                readName(attrName, 64);
                skipWs();
                if (pos < len || refill()) {
                    if (buf[pos] == '=') {
                        pos++;
                        skipWs();
                    } else {
                        continue;   // attribute without a value: tolerated
                    }
                } else {
                    return;
                }
                skipWs();
                if (pos >= len && !refill()) {
                    return;
                }
                char quote = buf[pos];
                if (quote != '"' && quote != '\'') {
                    continue;   // tolerated: unquoted values are not XMLTV, skip
                }
                pos++;
                std::string value;
                const size_t maxValue = 2048;
                for (;;) {
                    if (pos >= len && !refill()) {
                        break;   // unterminated: keep what we have
                    }
                    char d = buf[pos++];
                    if (d == quote) {
                        break;
                    }
                    if (value.size() < maxValue) {
                        value += d;
                    } else {
                        // keep the tail out of memory but still consume it
                        value += '\0';
                        value.pop_back();
                    }
                }
                if (attrs.size() < 32 && !attrName.empty()) {
                    attrs.push_back({attrName, value});
                }
            }
        }

        void skipWs() {
            for (;;) {
                if (pos >= len && !refill()) {
                    return;
                }
                char c = buf[pos];
                if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                    pos++;
                    continue;
                }
                return;
            }
        }
};

// decode the XML predefined entities + numeric character references
std::string decodeEntities(const std::string &in) {
    if (in.find('&') == std::string::npos) {
        return in;
    }
    std::string out;
    out.reserve(in.size() + 8);
    for (size_t i = 0; i < in.size();) {
        if (in[i] != '&') {
            out += in[i++];
            continue;
        }
        size_t semi = in.find(';', i + 1);
        if (semi == std::string::npos || semi - i > 10) {
            out += in[i++];
            continue;
        }
        std::string ent = in.substr(i + 1, semi - i - 1);
        if (ent == "amp") { out += '&'; }
        else if (ent == "lt") { out += '<'; }
        else if (ent == "gt") { out += '>'; }
        else if (ent == "quot") { out += '"'; }
        else if (ent == "apos") { out += '\''; }
        else if (!ent.empty() && ent[0] == '#') {
            long cp = -1;
            if (ent.size() > 2 && (ent[1] == 'x' || ent[1] == 'X')) {
                cp = std::strtol(ent.c_str() + 2, nullptr, 16);
            } else {
                cp = std::strtol(ent.c_str() + 1, nullptr, 10);
            }
            if (cp >= 0 && cp <= 0x10FFFF) {
                if (cp < 0x80) { out += (char) cp; }
                else if (cp < 0x800) { out += (char) (0xC0 | (cp >> 6)); out += (char) (0x80 | (cp & 0x3F)); }
                else if (cp < 0x10000) { out += (char) (0xE0 | (cp >> 12)); out += (char) (0x80 | ((cp >> 6) & 0x3F)); out += (char) (0x80 | (cp & 0x3F)); }
                else { out += (char) (0xF0 | (cp >> 18)); out += (char) (0x80 | ((cp >> 12) & 0x3F)); out += (char) (0x80 | ((cp >> 6) & 0x3F)); out += (char) (0x80 | (cp & 0x3F)); }
            }
        } else {
            out += '&';
            out += ent;
        }
        i = semi + 1;
    }
    return out;
}

std::string trim(const std::string &s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) {
        return std::string();
    }
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

}

bool parseXmltvTime(const std::string &input, int64_t &out) {
    std::string s = trim(input);
    if (s.size() < 14) {
        return false;
    }
    if (!allDigits(s.c_str(), 14)) {
        return false;
    }
    int year = std::atoi(s.substr(0, 4).c_str());
    int month = std::atoi(s.substr(4, 2).c_str());
    int day = std::atoi(s.substr(6, 2).c_str());
    int hour = std::atoi(s.substr(8, 2).c_str());
    int minute = std::atoi(s.substr(10, 2).c_str());
    int second = std::atoi(s.substr(12, 2).c_str());
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 60) {
        return false;
    }
    size_t i = 14;
    if (i < s.size() && s[i] == '.') {   // fractional seconds: ".fff"
        i++;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            i++;
        }
    }
    int64_t tzMinutes = 0;
    if (i < s.size()) {
        while (i < s.size() && s[i] == ' ') {
            i++;
        }
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
            if (i + 5 > s.size()) {
                return false;
            }
            std::string off = s.substr(i + 1, 4);
            if (!allDigits(off.c_str(), 4)) {
                return false;
            }
            int oh = (off[0] - '0') * 10 + (off[1] - '0');
            int om = (off[2] - '0') * 10 + (off[3] - '0');
            if (oh > 14 || om > 59) {
                return false;
            }
            tzMinutes = oh * 60 + om;
            if (s[i] == '-') {
                tzMinutes = -tzMinutes;
            }
        } else if (i < s.size() && (s[i] == 'Z' || s[i] == 'z')) {
            tzMinutes = 0;
        } else {
            return false;
        }
    }
    out = daysFromCivil(year, (unsigned) month, (unsigned) day) * 86400 + hour * 3600 + minute * 60 + second
          - tzMinutes * 60;
    return true;
}

Guide parseFile(const std::string &path,
                const std::function<bool(const std::string &)> &keepChannel,
                int64_t windowStart, int64_t windowEnd,
                size_t maxProgrammes, ParseStats *stats) {
    Guide guide;
    ParseStats localStats;
    if (stats == nullptr) {
        stats = &localStats;
    }
    if (maxProgrammes == 0) {
        maxProgrammes = 120000;
    }
    const size_t perChannelCap = 40;
    const size_t maxText = 512;

    XmlReader reader(path);

    bool inProgramme = false;
    bool skipCurrent = false;
    std::string curChannel;
    int64_t curStart = 0, curStop = 0;
    std::string curChannelLast;
    int64_t lastStart = -1;
    std::string curTitle, curDesc;
    enum class Slot { None, Title, Desc } slot = Slot::None;
    // track the nesting of children we do not care about (<category><icon>...), so EndElement depth is balanced
    int ignoredDepth = 0;

    // one place where a programme (self-closed or properly closed) is flushed
    auto flushProgramme = [&]() {
        inProgramme = false;
        const bool inWindow = curStop > windowStart && curStart < windowEnd;
        if (skipCurrent || !inWindow) {
            skipCurrent = false;
            return;
        }
        if ((size_t) guide.programmes >= maxProgrammes) {
            stats->skippedGlobalCap++;
            guide.truncated = true;
            return;
        }
        auto &vec = guide.byChannel[curChannel];
        if (vec.size() >= perChannelCap) {
            stats->skippedChannelCap++;
            return;
        }
        std::string titleDecoded = trim(decodeEntities(curTitle));
        if (titleDecoded.empty()) {
            stats->skippedEmptyTitle++;
            return;
        }
        bool dup = curChannel == curChannelLast && curStart == lastStart;
        if (dup) {
            return;
        }
        Program p;
        p.start = curStart;
        p.stop = curStop;
        p.title = std::move(titleDecoded);
        p.desc = trim(decodeEntities(curDesc));
        vec.push_back(std::move(p));
        guide.programmes++;
        curChannelLast = curChannel;
        lastStart = curStart;
        stats->programmesKept++;
    };

    for (;;) {
        Tok t = reader.next();
        if (t == Tok::None) {
            if (inProgramme) {
                flushProgramme();   // unterminated <programme>: flush what we have
            }
            break;
        }
        if (t == Tok::StartElement) {
            const std::string &elem = reader.tokenName();
            if (inProgramme) {
                if (ignoredDepth > 0) {
                    ignoredDepth++;
                    continue;
                }
                if (elem == "title") {
                    slot = Slot::Title;
                    curTitle.clear();
                    continue;
                }
                if (elem == "desc") {
                    slot = Slot::Desc;
                    curDesc.clear();
                    continue;
                }
                ignoredDepth = 1;   // sub-title, category, icon, ...: consumed, content ignored
                continue;
            }
            if (elem == "programme") {
                const std::string *ch = reader.attrValue("channel");
                const std::string *start = reader.attrValue("start");
                const std::string *stop = reader.attrValue("stop");
                if (ch == nullptr || start == nullptr || stop == nullptr) {
                    skipCurrent = true;
                    inProgramme = true;
                    ignoredDepth = 0;
                    slot = Slot::None;
                    stats->skippedBadTime++;
                    continue;
                }
                int64_t ps = 0, pe = 0;
                if (!parseXmltvTime(*start, ps) || !parseXmltvTime(*stop, pe) || pe <= ps) {
                    stats->skippedBadTime++;
                    skipCurrent = true;
                    inProgramme = true;
                    ignoredDepth = 0;
                    slot = Slot::None;
                    continue;
                }
                stats->programmesSeen++;
                inProgramme = true;
                skipCurrent = !keepChannel(*ch);
                curChannel = *ch;
                curStart = ps;
                curStop = pe;
                curTitle.clear();
                curDesc.clear();
                slot = Slot::None;
                ignoredDepth = 0;
                if (reader.selfClosing()) {
                    flushProgramme();
                }
                continue;
            }
            // anything else (tv, channel, display-name, ...): consumed and ignored
            continue;
        }
        if (t == Tok::Text) {
            if (!inProgramme || ignoredDepth > 0) {
                continue;
            }
            const std::string &raw = reader.tokenText();
            if (slot == Slot::Title && curTitle.size() < maxText) {
                curTitle += raw.substr(0, maxText - curTitle.size());
            } else if (slot == Slot::Desc && curDesc.size() < maxText) {
                curDesc += raw.substr(0, maxText - curDesc.size());
            }
            continue;
        }
        // EndElement
        const std::string &elem = reader.tokenName();
        if (ignoredDepth > 0) {
            ignoredDepth--;
            continue;
        }
        if (elem == "title" || elem == "desc") {
            slot = Slot::None;
            continue;
        }
        if (elem == "programme" && inProgramme) {
            flushProgramme();
        }
        if (guide.truncated) {
            break;
        }
    }

    // stable order within channels: XMLTV is usually time-ordered, but sort to be sure
    for (auto &[id, vec] : guide.byChannel) {
        std::stable_sort(vec.begin(), vec.end(),
                         [](const Program &a, const Program &b) { return a.start < b.start; });
    }
    return guide;
}

}
