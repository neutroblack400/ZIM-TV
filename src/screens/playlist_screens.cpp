// M3U / M3U8 playlist sources: the Add Source chooser, the playlist editor and Playlist Info.
//
// The editor downloads and checks a playlist before the source is saved (Save & Open = Test, then save).
// The downloaded playlist becomes the source's saved copy, so opening the new source does not download it a
// second time. The playlist URL may carry credentials or tokens: the form shows m3u::displayUrl() (host and
// file name) and only the keyboard, while editing, shows what was typed.

#include "common.h"
#include "../downloads/download_model.h"
#include "../core/format.h"
#include "../core/url.h"
#include "../iptv/m3u.h"
#include "../platform/clock.h"
#include "../platform/log.h"
#include "../player/protocols.h"
#include "../storage/catalog_cache.h"

using namespace c2d;
using namespace iptv;

namespace {

    // "4,281 channels  ·  37 groups"
    std::string channelsAndGroups(const m3u::Info &info) {
        int groups = info.stats.groups;
        return i18n::count("m3u.channels", info.stats.channels) + "  \xC2\xB7  " + i18n::count("m3u.groups", groups);
    }

    // what a test found, for the editor's status panel
    std::string testSummary(const m3u::Info &info) {
        const m3u::Stats &st = info.stats;
        std::string s = tr("playlist.ok") + "\n" + channelsAndGroups(info) + "\n"
                        + tr("playlist.logos", {fmt::number(st.withLogo), fmt::number(st.withoutLogo)});
        if (st.skipped() > 0) {
            s += "\n" + tr("playlist.skipped", {fmt::number(st.skipped())});
        }
        if (st.https > 0 && !protocols::httpsPlayable()) {
            s += "\n\n" + i18n::count("playlist.https_warning", st.https);
        }
        return s;
    }

    // printable ASCII only: a User-Agent goes into an HTTP header line
    bool validUserAgent(const std::string &ua) {
        if (ua.size() > 256) {
            return false;
        }
        for (unsigned char c: ua) {
            if (c < 0x20 || c > 0x7E) {
                return false;
            }
        }
        return true;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////
    class PlaylistEditScreen : public Screen {
    public:
        PlaylistEditScreen(App &a, const Profile &p) : Screen(a), profile(p), original(p) {
            isNew = p.id.empty();
            profile.type = SourceType::M3u;
            // the onboarding opens the EPG shortcut with this marker
            isEpg = (p.name == "EPG / XMLTV");
            bgTex = new c2d::C2DTexture(app.romfs() + "assets/onboarding_bg.png");
            if (bgTex && bgTex->available) {
                auto *bg = new RectangleShape(Vector2f(theme::SCREEN_W, theme::SCREEN_H));
                bg->setTexture(bgTex, true);
                add(bg);
            } else {
                auto *bg = new RectangleShape(Vector2f(theme::SCREEN_W, theme::SCREEN_H));
                bg->setFillColor(theme::background());
                add(bg);
                if (bgTex) { delete bgTex; bgTex = nullptr; }
            }

            // h1 + subtitle (mock form layout)
            auto *t = ui::label(this, isEpg ? tr("playlist.form_title_epg") : tr("playlist.form_title_m3u"), 56, 110, 88, ui::Weight::SemiBold,
                                theme::textStrong());
            (void) t;
            auto *s = ui::label(this, isEpg ? tr("playlist.form_subtitle_epg") : tr("playlist.form_subtitle_m3u"),
                                24, 110, 172, ui::Weight::Regular, theme::withAlpha(theme::textPrimary(), 158));
            (void) s;

            const char *captions[FIELDS] = {"playlist.name", "playlist.url", "playlist.user_agent", "playlist.epg"};
            const float labelYs[FIELDS] = {240, 355, 470, 585};
            for (int i = 0; i < FIELDS; i++) {
                auto *cap = ui::label(this, tr(captions[i]), 21, 110, labelYs[i], ui::Weight::SemiBold,
                                      theme::withAlpha(theme::textPrimary(), 158));
                (void) cap;
                fields[i] = new screens::FormField(FloatRect(110, labelYs[i] + 35, 1000, 70), tr(captions[i]));
                add(fields[i]);
            }

            // side panel (mock .side)
            statusSide = ui::box(this, FloatRect(1260, 300, 560, 360), theme::withAlpha(theme::textPrimary(), 16), 26);
            auto *st = ui::label(statusSide, isEpg ? tr("playlist.form_title_epg") : tr("playlist.form_title_m3u"), 24, 36, 34,
                                 ui::Weight::SemiBold, theme::textStrong());
            (void) st;
            std::string info = isEpg ? tr("playlist.side_epg_text") : tr("playlist.side_m3u_text");
            auto *sb = ui::label(statusSide, info, 22, 36, 86, ui::Weight::Regular, theme::withAlpha(theme::textPrimary(), 192));
            sb->setMaxWidth(560 - 72);
            sb->setMaxLines(8);

            buttons[0] = new screens::PillButton(tr("playlist.load"), FloatRect(110, 800, 280, 74), true);
            buttons[1] = new screens::PillButton(tr("common.back"), FloatRect(410, 800, 180, 74), false);
            for (auto *b: buttons) add(b);

            statusLine = ui::label(this, "", 22, 110, 905, ui::Weight::SemiBold, theme::textSecondary());
            statusLine->setMaxWidth(1550);
            statusLine->setMaxLines(2);

            setStatus(tr(isNew ? "playlist.status_new" : "playlist.status_edit"), theme::textSecondary());
            refresh();
        }

        ~PlaylistEditScreen() override {
            testToken.cancel();
            if (bgTex) { delete bgTex; bgTex = nullptr; }
        }

        const char *name() const override { return "playlist-edit"; }

        void tick(double) override {}

        // physical keyboard: typing on a focused field opens its editor with what was typed
        bool handleKey(const kbd::Event &k) override {
            if (k.key != kbd::Key::Char || focus >= FIELDS || testing) {
                return false;
            }
            seed = k.text;
            activate();
            return true;
        }

        void handleInput(const InputEvent &e) override {
            switch (e.button) {
                case PadButton::Up:
                    focus = focus >= FIELDS ? FIELDS - 1 : std::max(0, focus - 1);
                    break;
                case PadButton::Down:
                    focus = std::min(focus + 1, FIELDS + BUTTONS - 1);
                    break;
                case PadButton::Left:
                    if (focus > FIELDS) {
                        focus--;
                    }
                    break;
                case PadButton::Right:
                    if (focus >= FIELDS && focus < FIELDS + BUTTONS - 1) {
                        focus++;
                    }
                    break;
                case PadButton::Cross:
                    if (!e.repeat) {
                        activate();
                    }
                    return;
                case PadButton::Circle:
                    if (!e.repeat) {
                        app.pop();
                    }
                    return;
                default:
                    return;
            }
            refresh();
        }

    private:
        static constexpr int FIELDS = 4;
        static constexpr int BUTTONS = 2;
        bool isEpg = false;

        std::string seed;   // character typed on a physical keyboard that opened a field's editor

        void activate() {
            switch (focus) {
                case 0:
                    edit(tr("playlist.name"), profile.name, [this](const std::string &v) { profile.name = v; });
                    break;
                case 1:
                    edit(tr("playlist.url"), profile.playlistUrl, screens::KeyboardKind::Url,
                         [this](const std::string &v) { profile.playlistUrl = v; });
                    break;
                case 2:
                    edit(tr("playlist.user_agent"), profile.userAgent, [this](const std::string &v) { profile.userAgent = v; });
                    break;
                case 3:
                    edit(tr("playlist.epg"), profile.epgUrl, screens::KeyboardKind::Url,
                         [this](const std::string &v) { profile.epgUrl = v; });
                    break;
                case FIELDS:
                    test(true);
                    break;
                default:
                    app.pop();
                    break;
            }
        }

        void edit(const std::string &title, const std::string &value, std::function<void(const std::string &)> set) {
            edit(title, value, screens::KeyboardKind::Default, set);
        }
        void edit(const std::string &title, const std::string &value, screens::KeyboardKind kind, std::function<void(const std::string &)> set) {
            std::string initial = value + seed;   // a character typed on a physical keyboard opened the editor
            seed.clear();
            app.push(screens::makeKeyboard(app, title, initial, kind, [this, set](const std::string &v) {
                set(v);
                tested.reset();
                refresh();
            }));
        }

        bool locationChanged() const {
            return isNew || url::trim(profile.playlistUrl) != original.playlistUrl
                   || url::trim(profile.userAgent) != original.userAgent;
        }

        // validates the form into `profile`; returns an error message or ""
        std::string validate() {
            std::string location = url::stripFormat(url::trim(profile.playlistUrl));
            std::string local = m3u::localPlaylistPath(location);
            if (!local.empty()) {
                location = local;
            } else {
                if (location.find("://") == std::string::npos) {
                    // the web reference accepts schemeless addresses too: default to http://
                    location = "http://" + location;
                }
                if (!m3u::isHttpUrl(location)) {
                    return tr("playlist.error.url");
                }
            }
            std::string ua = url::trim(profile.userAgent);
            if (!validUserAgent(ua)) {
                return tr("playlist.error.user_agent");
            }
            std::string epg = url::trim(profile.epgUrl);
            if (!epg.empty()) {
                std::string epgLocal = m3u::localPlaylistPath(epg);
                if (epgLocal.empty()) {
                    if (epg.find("://") == std::string::npos) {
                        epg = "http://" + epg;   // schemeless accepted, like the list address
                    }
                    if (!m3u::isHttpUrl(epg)) {
                        return tr("playlist.error.epg");
                    }
                }
            }
            profile.playlistUrl = location;
            profile.userAgent = ua;
            profile.epgUrl = epg;
            profile.name = url::trim(profile.name);
            if (profile.name.empty()) {
                std::string shown = m3u::displayUrl(location);
                profile.name = shown.substr(0, shown.find('/'));
            }
            return "";
        }

        void test(bool thenSave) {
            if (testing) {
                return;
            }
            std::string err = validate();
            if (!err.empty()) {
                setStatus(err, theme::danger());
                refresh();
                return;
            }
            // only the name changed: nothing to download again
            if (thenSave && (tested || !locationChanged())) {
                save();
                return;
            }
            testing = true;
            setStatus(tr("playlist.testing"), theme::textSecondary());
            LOG_I("profiles", "testing playlist %s", m3u::displayUrl(profile.playlistUrl).c_str());
            testToken = app.m3u().load(profile, APP_DATA_DIR, M3uService::Source::Network, false,
                                       [this, thenSave](M3uService::Outcome &o) {
                testing = false;
                if (o.ok) {
                    LOG_I("profiles", "playlist OK: %d entries", o.info.stats.channels);
                } else {
                    LOG_W("profiles", "playlist test failed: %s", o.message.c_str());
                }
                if (o.ok) {
                    tested = o.body;
                    testedChannels = o.info.stats.channels;
                    setStatus(testSummary(o.info), o.info.stats.https > 0 && !protocols::httpsPlayable() ? theme::warning()
                                                                                                    : theme::success());
                    if (thenSave) {
                        save();
                    }
                } else {
                    tested.reset();
                    setStatus(o.message + (thenSave ? "\n\n" + tr("playlist.not_saved") : ""), theme::danger());
                }
                refresh();
            }, true);
            refresh();
        }

        void save() {
            Profile p = profile;
            bool changed = locationChanged();
            int64_t now = clockx::unixNow();
            std::string id = app.profiles().upsert(p, now);
            if (id.empty()) {
                setStatus(tr("users.limit_reached"), theme::danger());
                return;
            }
            app.profiles().setActive(id);
            std::string err;
            if (!app.profiles().save(&err)) {
                LOG_E("profiles", "save failed: %s", err.c_str());
                setStatus(tr("profile.save_failed"), theme::danger());
                return;
            }
            if (tested) {
                // the playlist just checked becomes the saved copy: opening does not download it again
                if (M3uService::saveBody(APP_DATA_DIR, id, *tested, testedChannels, now)) {
                    app.notePlaylistLoaded(id, testedChannels);
                }
            } else if (changed) {
                CatalogCache(APP_DATA_DIR).remove(id, M3uService::CACHE_NAME);   // never show another URL's list
            }
            LOG_I("profiles", "playlist source %s saved (%s)", id.c_str(), m3u::displayUrl(p.playlistUrl).c_str());
            app.replaceAll(screens::makeConnect(app, *app.profiles().find(id)));
        }

        void setStatus(const std::string &text, const Color &color) {
            if (statusLine) {
                statusLine->setText(text);
                statusLine->setColor(color);
            }
        }

        void refresh() {
            LOG_I("profiles", "refresh entered (url %zu chars)", profile.playlistUrl.size());
            std::string shown = m3u::displayUrl(url::trim(profile.playlistUrl));
            fields[0]->setValue(profile.name.empty() ? tr("profile.name_placeholder") : profile.name, profile.name.empty());
            if (profile.playlistUrl.empty()) {
                fields[1]->setValue(tr("playlist.url_placeholder"), true);
            } else {
                fields[1]->setValue(shown.empty() ? tr("playlist.url_invalid") : shown, false);
            }
            fields[2]->setValue(profile.userAgent.empty() ? tr("playlist.user_agent_default") : profile.userAgent,
                                profile.userAgent.empty());
            fields[3]->setValue(profile.epgUrl.empty() ? tr("playlist.epg_placeholder") : profile.epgUrl,
                                profile.epgUrl.empty());
            for (int i = 0; i < FIELDS; i++) {
                fields[i]->setFocused(focus == i);
            }
            for (int i = 0; i < BUTTONS; i++) {
                buttons[i]->setFocused(focus == FIELDS + i);
                buttons[i]->setEnabled(!testing);
            }
            LOG_I("profiles", "refresh exited");
        }

        Profile profile;
        Profile original;
        bool isNew;
        bool testing = false;
        std::shared_ptr<std::string> tested;   // the playlist downloaded by the last successful test
        int testedChannels = 0;
        int focus = 0;
        screens::FormField *fields[FIELDS];
        screens::PillButton *buttons[BUTTONS];
        RectangleShape *statusSide = nullptr;
        ui::Label *statusLine = nullptr;
        c2d::C2DTexture *bgTex = nullptr;
        CancelToken testToken;
    };

    ////////////////////////////////////////////////////////////////////////////////////////////////
    class PlaylistInfoScreen : public Screen {
    public:
        explicit PlaylistInfoScreen(App &a) : Screen(a) {
            ui::background(this);
            screens::header(this, tr("m3u.info"), app.session().profile.name);
            panel = ui::box(this, FloatRect(theme::SAFE_X, 200, theme::SCREEN_W - 2 * theme::SAFE_X, 760),
                            theme::surface(), theme::RADIUS);
            for (int i = 0; i < ROWS; i++) {
                float y = 34 + (float) i * 50;
                captions[i] = ui::label(panel, "", theme::BODY, 48, y, ui::Weight::Regular, theme::textSecondary());
                captions[i]->setMaxWidth(760);
                values[i] = ui::label(panel, "", theme::BODY, 0, y, ui::Weight::SemiBold);
                values[i]->setAlign(ui::Align::Right, panel->getSize().x - 48);
                values[i]->setMaxWidth(900);
            }
            screens::hintBar(this, {{A::Confirm, tr("m3u.refresh")}, {A::Back, tr("common.back")}});
            fill();
        }

        const char *name() const override { return "playlist-info"; }

        void tick(double) override {
            if (app.session().liveGeneration != generation) {
                fill();
                redraw();
            }
        }

        void handleInput(const InputEvent &e) override {
            if (e.repeat) {
                return;
            }
            if (e.button == PadButton::Cross || e.button == PadButton::Triangle) {
                app.refreshPlaylist(true);
                fill();
            } else if (e.button == PadButton::Circle) {
                app.pop();
            }
        }

    private:
        static constexpr int ROWS = 14;

        void fill() {
            const Session &s = app.session();
            generation = s.liveGeneration;
            const m3u::Info &info = s.playlist;
            const m3u::Stats &st = info.stats;
            int uncategorized = s.live.countInCategory(UNCATEGORIZED_ID);
            std::string refreshed = info.savedAt > 0 ? clockx::localDate(info.savedAt) : tr("m3u.never");
            if (s.playlistRefreshing) {
                refreshed = tr("m3u.refreshing");
            }
            std::vector<std::pair<std::string, std::string>> rows = {
                    {tr("m3u.info_source"), screens::sourceLocation(s.profile)},
                    {tr("m3u.info_channels"), fmt::number((long long) s.live.channels().size())},
                    {tr("m3u.info_groups"), fmt::number(st.groups)},
                    {tr("catalog.uncategorized"), fmt::number(uncategorized)},
                    {tr("m3u.info_with_logos"), fmt::number(st.withLogo)},
                    {tr("m3u.info_without_logos"), fmt::number(st.withoutLogo)},
                    {tr("m3u.info_skipped"), st.skipped() == 0 ? fmt::number(0)
                                                               : tr("m3u.info_skipped_detail",
                                                                    {fmt::number(st.skipped()), fmt::number(st.missingUrl),
                                                                     fmt::number(st.invalidUrl + st.overLimit)})},
                    {tr("m3u.info_repaired"), fmt::number(st.malformedExtinf + st.invalidUtf8)},
                    {tr("m3u.info_hls"), fmt::number(st.hls)},
                    {tr("m3u.info_https"), fmt::number(st.https)},
                    {tr("m3u.info_other"), fmt::number(st.otherProtocols)},
                    {tr("m3u.info_user_agents"), fmt::number(st.userAgents)},
                    {tr("m3u.info_last_refresh"), refreshed},
                    {tr("m3u.info_size"), st.bytes > 0 ? dl::formatBytes((int64_t) st.bytes) : "-"}};
            for (int i = 0; i < ROWS; i++) {
                captions[i]->setText(i < (int) rows.size() ? rows[(size_t) i].first : "");
                values[i]->setText(i < (int) rows.size() ? rows[(size_t) i].second : "");
            }
            // HTTPS streams play when the backend has TLS; without it they are flagged, not hidden
            values[9]->setColor(st.https > 0 && !protocols::httpsPlayable() ? theme::warning() : theme::textPrimary());
            values[6]->setColor(st.skipped() > 0 ? theme::warning() : theme::textPrimary());
        }

        RectangleShape *panel;
        ui::Label *captions[ROWS];
        ui::Label *values[ROWS];
        unsigned generation = 0;
    };
}

namespace {

    // Add Source (new profile) or reconfigure an existing profile's source: same four cards, but in
    // reconfigure mode the editor opens on the SAME user, converting its type in place (other-type secrets are
    // cleared so no Xtream password hides behind an M3U URL, and vice-versa).
    class SourceChooserScreen : public Screen {
    public:
        static constexpr int CARDS = 3;

        explicit SourceChooserScreen(App &a, const Profile *target = nullptr) : Screen(a, true), hasTarget(target != nullptr) {
            if (target != nullptr) {
                targetProfile = *target;
            }
            ui::background(this);
            const float cardW = 420;
            const float cardH = 380;
            const float gap = 28;
            const float x0 = (theme::SCREEN_W - CARDS * cardW - (CARDS - 1) * gap) / 2;
            const float y0 = 320;
            auto *title = ui::label(this, "ZIMTV - " + tr("source.add_title"), theme::TITLE, 0, 170, ui::Weight::SemiBold,
                                    theme::accentText());
            title->setAlign(ui::Align::Center, theme::SCREEN_W);
            auto *sub = ui::label(this, tr("source.choose_subtitle"), theme::BODY, 0, 235,
                                  ui::Weight::Regular, theme::textSecondary());
            sub->setAlign(ui::Align::Center, theme::SCREEN_W);
            // exactly three sources: Xtream Codes, a plain M3U / M3U8 list, or the XMLTV guide associated with it
            const std::string names[CARDS] = {"Xtream Codes", tr("source.card.m3u_title"), tr("playlist.form_title_epg")};
            const std::string details[CARDS] = {
                tr("source.card.xtream_detail"), tr("source.card.m3u_detail"), tr("source.card.epg_detail")};
            const std::string provides[CARDS] = {tr("source.card.xtream_provides"), tr("source.card.m3u_provides"),
                                                tr("source.card.epg_provides")};
            for (int i = 0; i < CARDS; i++) {
                Card &c = cards[i];
                c.bg = ui::box(this, FloatRect(x0 + (float) i * (cardW + gap), y0, cardW, cardH), theme::card(),
                               theme::RADIUS_LARGE);
                c.bg->setOutlineColor(theme::accent());
                c.mark = ui::box(c.bg, FloatRect(40, 44, 60, 8), theme::accent(), 4);
                // small Irken-like accent bars
                ui::box(c.bg, FloatRect(cardW - 70, 36, 30, 30), theme::accentMuted(), 6);
                c.name = ui::label(c.bg, names[i], theme::HEADING, 40, 86, ui::Weight::SemiBold);
                c.name->setMaxWidth(cardW - 80);
                auto *d = ui::label(c.bg, details[i], theme::LABEL, 40, 150, ui::Weight::Regular, theme::textSecondary());
                d->setMaxWidth(cardW - 80);
                d->setMaxLines(6);
                auto *pv = ui::label(c.bg, provides[i], theme::BODY, 40, cardH - 64, ui::Weight::SemiBold,
                                     theme::accentText());
                pv->setMaxWidth(cardW - 80);
            }
            screens::hintBar(this, {{A::Confirm, tr("common.select")}, {A::Back, tr("common.back")}});
            refresh();
        }

        const char *name() const override { return "source-chooser"; }

        void handleInput(const InputEvent &e) override {
            if (e.button == PadButton::Left || e.button == PadButton::Right) {
                focus = e.button == PadButton::Right ? std::min(CARDS - 1, focus + 1) : std::max(0, focus - 1);
                refresh();
            } else if (e.button == PadButton::Cross && !e.repeat) {
                int choice = focus;
                App &a = app;
                a.pop();
                if (hasTarget) {
                    Profile p = targetProfile;
                    switch (choice) {
                        case 0:   // Xtream Codes: keep Xtream creds only if already Xtream
                            if (p.type != SourceType::Xtream) {
                                p.server.clear(); p.username.clear(); p.password.clear();
                                p.mac.clear(); p.playlistUrl.clear(); p.userAgent.clear(); p.epgUrl.clear();
                            }
                            p.type = SourceType::Xtream;
                            a.push(screens::makeProfileEdit(a, p));
                            break;
                        case 1:   // Lista M3U: keep playlist fields only if already M3U
                            if (!p.isPlaylist()) {
                                p.server.clear(); p.username.clear(); p.password.clear();
                                p.mac.clear(); p.playlistUrl.clear(); p.userAgent.clear(); p.epgUrl.clear();
                            }
                            p.type = SourceType::M3u;
                            a.push(screens::makePlaylistEdit(a, p));
                            break;
                        default:  // EPG XMLTV: the list editor with the EPG field (the guide is associated with the list)
                            if (!p.isPlaylist()) {
                                p.server.clear(); p.username.clear(); p.password.clear();
                                p.mac.clear(); p.playlistUrl.clear(); p.userAgent.clear(); p.epgUrl.clear();
                            }
                            p.type = SourceType::M3u;
                            a.push(screens::makePlaylistEdit(a, p));
                            break;
                    }
                    return;
                }
                if (choice == 0) {
                    a.push(screens::makeProfileEdit(a, Profile()));
                } else if (choice == 1) {
                    Profile p;
                    p.type = SourceType::M3u;
                    a.push(screens::makePlaylistEdit(a, p));
                } else {
                    // EPG (XMLTV): the list editor in guide mode; the name doubles as the marker for the EPG form
                    Profile p;
                    p.type = SourceType::M3u;
                    p.name = "EPG / XMLTV";
                    a.push(screens::makePlaylistEdit(a, p));
                }
            } else if (e.button == PadButton::Circle && !e.repeat) {
                app.pop();
            }
        }

    private:
        struct Card {
            RectangleShape *bg;
            RectangleShape *mark;
            ui::Label *name;
        };

        Profile targetProfile;         // reconfigure mode: the user whose source is being edited (kept as-is, id preserved)
        bool hasTarget = false;

        void refresh() {
            for (int i = 0; i < CARDS; i++) {
                bool f = i == focus;
                cards[i].bg->setFillColor(f ? theme::cardHover() : theme::card());
                cards[i].bg->setOutlineThickness(f ? theme::FOCUS_BORDER : 0);
                cards[i].mark->setFillColor(f ? theme::accent() : theme::accentMuted());
                cards[i].name->setColor(f ? theme::textStrong() : theme::accentText());
            }
        }

        Card cards[CARDS];
        int focus = 0;
    };
}

namespace screens {
    Screen *makeSourceChooser(App &app) {
        return new SourceChooserScreen(app);
    }

    Screen *makeSourceChooser(App &app, const iptv::Profile &target) {
        Profile p = target;
        return new SourceChooserScreen(app, &p);
    }

    Screen *makePlaylistEdit(App &app, const Profile &profile) {
        return new PlaylistEditScreen(app, profile);
    }

    Screen *makePlaylistInfo(App &app) {
        return new PlaylistInfoScreen(app);
    }
}
