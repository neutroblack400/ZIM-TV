#include "core/utf8.h"
#include <cstring>
#include <cstdio>

using namespace std;

std::wstring utf8ToWide(const std::string &s) {
    std::wstring out;
    const std::u32string codepoints = utf8::decode(s);
    out.reserve(codepoints.size());
    for (char32_t value : codepoints) {
        uint32_t cp = static_cast<uint32_t>(value);
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
        if (sizeof(wchar_t) == 2 && cp > 0xFFFF) {
            cp -= 0x10000;
            out.push_back(static_cast<wchar_t>(0xD800u + (cp >> 10)));
            out.push_back(static_cast<wchar_t>(0xDC00u + (cp & 0x3FFu)));
        } else {
            out.push_back(static_cast<wchar_t>(cp));
        }
    }
    return out;
}

std::string wideToUtf8(const wchar_t *ws, size_t maxUnits) {
    std::string out;
    if (!ws) return out;
    for (size_t i = 0; i < maxUnits && ws[i] != 0; ++i) {
        uint32_t cp = static_cast<uint32_t>(ws[i]);
        if (sizeof(wchar_t) == 2 && cp >= 0xD800 && cp <= 0xDBFF) {
            if (i + 1 < maxUnits) {
                uint32_t low = static_cast<uint32_t>(ws[i + 1]);
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    cp = 0x10000u + ((cp - 0xD800u) << 10) + (low - 0xDC00u);
                    ++i;
                } else {
                    cp = 0xFFFD;
                }
            } else {
                cp = 0xFFFD;
            }
        } else if (cp >= 0xD800 && cp <= 0xDFFF) {
            cp = 0xFFFD;
        } else if (cp > 0x10FFFF) {
            cp = 0xFFFD;
        }
        out += utf8::encode(static_cast<char32_t>(cp));
    }
    return out;
}

static const char *kInputs[] = {
    "https://servidor.com:8080",
    "http://192.168.1.100:8080/get.php?username=teste&password=senha123",
    "usuário",
    "São Paulo",
    "",
    "ç",
    "\xC0\xA9",
    "\xEF\xBF\xBD",
    "\xC2\xA0",
    "https://servidor.com/érie",
};

static void hex(const string &s, const char *label) {
    printf("  %s (%zu bytes): ", label, s.size());
    for (unsigned char c : s) printf("%02X ", c);
    printf(" [");
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        putchar((c >= 32 && c < 127) ? c : '.');
    }
    printf("]\n");
}

int main() {
    printf("sizeof(wchar_t) = %zu\n", sizeof(wchar_t));
    for (const char *input : kInputs) {
        printf("\nInput: '%s'\n", input);
        hex(input, "in  ");
        wstring w = utf8ToWide(input);
        printf("  wide units = %zu\n", w.size());
        string back = wideToUtf8(w.c_str(), w.size() + 1);
        hex(back, "back");
        printf("  roundtrip %s\n", back == input ? "OK" : "DIFFERS");
    }
    return 0;
}
