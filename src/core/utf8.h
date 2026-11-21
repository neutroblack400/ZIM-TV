// UTF-8 helpers. All text in the app is UTF-8 std::string; rendering decodes to code points.

#ifndef ZIMTV_CORE_UTF8_H
#define ZIMTV_CORE_UTF8_H

#include <string>

namespace utf8 {

    // Decodes UTF-8. Invalid or truncated sequences decode to U+FFFD, never throw.
    std::u32string decode(const std::string &text);

    std::string encode(char32_t codePoint);

    // Removes the last code point (for backspace).
    void popBack(std::string &text);

    size_t length(const std::string &text);

    // Search key: lower case, Latin letters with diacritics reduced to their ASCII base (ş->s, İ->i, ü->u),
    // Cyrillic/Greek lower-cased.
    std::u32string foldForSearch(const std::string &text);

    // Recovers text saved by builds whose keyboard path re-encoded bytes one at a time. Sequences that are not
    // valid UTF-8 are read as Windows-1252 (identical to Latin-1 for 0xA0-0xFF, and covers Western accents for the
    // 0x80-0x9F range). Valid UTF-8 bytes are copied unchanged, so already-correct text is untouched.
    std::string repairBrokenUtf8(const std::string &text);
}

#endif // ZIMTV_CORE_UTF8_H
