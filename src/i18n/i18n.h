// Application UI localization (host-testable).
//
// Every user-facing text of the app comes from a table of stable keys ("home.live_tv", "download.pause")
// per UI language: strings_en.cpp (English, the fallback), strings_tr.cpp (Türkçe), strings_pt.cpp
// (Português do Brasil, the default of every installation), strings_es.cpp (Español), strings_fr.cpp
// (Français), strings_it.cpp (Italiano) and strings_de.cpp (Deutsch).
// Provider metadata (titles, channel names, categories, plots) is never translated.
//
//   i18n::tr("player.buffering")                    -> "Carregando…" / "Arabelleğe alınıyor…" / "Loading…"
//   i18n::tr("download.size_of", {done, total})     -> "{0} de {1}" with the arguments substituted
//   i18n::count("home.channels", 573)               -> key "home.channels.one" / ".other", {0} = the number
//
// The UI language is global to the app (Settings > Language), never chosen from the account, provider or
// region. tr() may be called from any thread: the tables are immutable and the language is atomic.
// tests/host/test_i18n.cpp checks that all the tables have the same keys and placeholders, that no text is
// blank, that every key used in src/ exists, and that screens do not bypass the tables.

#ifndef ZIMTV_I18N_I18N_H
#define ZIMTV_I18N_I18N_H

#include <initializer_list>
#include <string>
#include <vector>

namespace i18n {

    enum class Language {
        English = 0,
        Turkish = 1,
        Portuguese = 2,
        Spanish = 3,
        French = 4,
        Italian = 5,
        German = 6,
    };

    struct Entry {
        const char *key;
        const char *text;
    };

    // the string tables (strings_en.cpp / strings_tr.cpp / strings_pt.cpp), in source order
    const std::vector<Entry> &table(Language language);

    void setLanguage(Language language);

    Language language();

    // settings.json value: "en" / "tr" / "pt"
    const char *code(Language language);

    // unknown or empty codes -> Portuguese (the default of every installation)
    Language fromCode(const std::string &code);

    // the language's own name for the Language setting: "English", "Türkçe", "Português (Brasil)"
    const char *nativeName(Language language);

    // text of a key in the current language; English when the language lacks it; the key itself when no
    // table has it (logged once - the host tests make sure this never happens in a release)
    const std::string &tr(const char *key);

    // with {0}, {1} ... replaced by the arguments
    std::string tr(const char *key, std::initializer_list<std::string> args);

    // key + ".one" when n == 1, else key + ".other"; {0} is the number
    std::string count(const char *key, long long n);

    // {0} {1} substitution (exposed for tests)
    std::string substitute(const std::string &text, std::initializer_list<std::string> args);

    // lookup in a specific table ("" when absent)
    std::string lookup(Language language, const std::string &key);
}

#endif // ZIMTV_I18N_I18N_H
