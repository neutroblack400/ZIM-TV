// Onboarding, source (profile) manager and the Xtream profile editor. Sources are Xtream Codes accounts or
// M3U / M3U8 playlists (playlist editor and Playlist Info: playlist_screens.cpp).

#include "common.h"
#include "../core/format.h"
#include "../core/url.h"
#include "../iptv/stalker.h"
#include "../platform/clock.h"
#include "../platform/log.h"
#include "../storage/catalog_cache.h"
#include "../storage/catalog_store.h"
#include "../iptv/m3u.h"

using namespace c2d;
using namespace iptv;

namespace {

    ////////////////////////////////////////////////////////////////////////////////////////////////
    class OnboardingScreen : public Screen {
    public:
        explicit OnboardingScreen(App &a) : Screen(a) {
            // v2 reference ("Escolha sua fonte IPTV"): deep purple radial gradient with a soft glow,
            // centred ZIM TV artwork, and four lettered cards (X / M / E / L) to pick the source.
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

            const float cx = theme::SCREEN_W / 2.0f;

            logoTex = new c2d::C2DTexture(app.romfs() + "assets/zimtv_hero.png");
            if (logoTex && logoTex->available) {
                float lw = 880.0f;
                float lh = lw * logoTex->getSize().y / logoTex->getSize().x;
                auto *logo = new RectangleShape(Vector2f(lw, lh));
                logo->setTexture(logoTex, true);
                logo->setPosition(cx - lw / 2.0f, 26.0f);
                add(logo);
            } else {
                ui::label(this, "ZIMTV", 96, 0, 120, ui::Weight::SemiBold, theme::warning())
                    ->setAlign(ui::Align::Center, theme::SCREEN_W);
                ui::label(this, "IPTV", 44, 0, 236, ui::Weight::SemiBold, theme::padCross())
                    ->setAlign(ui::Align::Center, theme::SCREEN_W);
                if (logoTex) { delete logoTex; logoTex = nullptr; }
            }

            auto *t = ui::label(this, tr("source.add_title"), 60, 0, 312, ui::Weight::SemiBold, theme::textStrong());
            t->setAlign(ui::Align::Center, theme::SCREEN_W);
            auto *s = ui::label(this, tr("onboarding.text"), 24, 0, 418, ui::Weight::Regular,
                                theme::withAlpha(theme::textPrimary(), 158));
            s->setAlign(ui::Align::Center, theme::SCREEN_W);

            // 4 lettered cards, centered (mock v3: flexbox, 300px cards, 25px gaps)
            buttonCount = 3;
            const char *glyph[3] = {"X", "M", "E"};
            const std::string name[3] = {tr("source.type_xtream"), tr("source.type_m3u_short"), "EPG (XMLTV)"};
            const std::string desc[3] = {tr("source.type_xtream_detail"), tr("source.type_m3u_detail"),
                                         tr("epg.unavailable_title")};
            const float cw = 300, ch = 230, y = 600, gap = 25;
            float total = buttonCount * cw + (buttonCount - 1) * gap;
            float bx = (theme::SCREEN_W - total) / 2.0f;
            for (int i = 0; i < buttonCount; i++) {
                float x = bx + (float) i * (cw + gap);
                Card &c = cards[i];
                c.bg = ui::box(this, FloatRect(x, y, cw, ch), cardOff, 30);
                c.bg->setOutlineColor(theme::withAlpha(theme::textPrimary(), 30));
                c.bg->setOutlineThickness(2);
                c.icon = ui::box(this, FloatRect(x + 30, y + 30, 66, 66), iconOff, 20);
                c.glyph = ui::label(this, glyph[i], 30, 0, 0, ui::Weight::SemiBold, theme::textStrong());
                c.glyph->setAlign(ui::Align::Center, 66);
                c.glyph->setPosition(x + 30, y + 47);
                c.name = ui::label(this, name[i], 30, x + 30, y + 130, ui::Weight::SemiBold, theme::textStrong());
                c.name->setMaxWidth(cw - 60);
                c.desc = ui::label(this, desc[i], 17, x + 30, y + 172, ui::Weight::Regular, theme::withAlpha(theme::textPrimary(), 140));
                c.desc->setMaxWidth(cw - 60);
                c.desc->setMaxLines(2);
            }

            auto *h = ui::label(this, std::string("\xE2\x86\x90 \xE2\x86\x92 ") + tr("common.select")
                                         + "  ·  X " + tr("common.select"),
                                20, 0, theme::SCREEN_H - 46, ui::Weight::Regular,
                                theme::withAlpha(theme::textPrimary(), 115));
            h->setAlign(ui::Align::Center, theme::SCREEN_W);
            refresh();
        }

        ~OnboardingScreen() override {
            if (bgTex) { delete bgTex; bgTex = nullptr; }
            if (logoTex) { delete logoTex; logoTex = nullptr; }
        }

        const char *name() const override { return "onboarding"; }

        void handleInput(const InputEvent &e) override {
            if (e.button == PadButton::Left || e.button == PadButton::Right) {
                focus = e.button == PadButton::Right ? std::min(buttonCount - 1, focus + 1) : std::max(0, focus - 1);
                refresh();
            } else if (e.button == PadButton::Cross && !e.repeat) {
                if (focus == 0) {
                    app.push(screens::makeProfileEdit(app, Profile()));
                } else if (focus == 1) {
                    Profile p; p.type = SourceType::M3u;
                    app.push(screens::makePlaylistEdit(app, p));
                } else if (focus == 2) {
                    // the XMLTV guide importer is not implemented: say so instead of opening a dead-end editor
                    app.push(screens::makeDialog(app, tr("epg.unavailable_title"), tr("epg.unavailable_text"),
                                                 {tr("common.understood")}, [](int) {}));
                }
            } else if (e.button == PadButton::Circle && !e.repeat) {
                app.push(screens::makeDialog(app, tr("app.exit_title"), "", {tr("common.cancel"), tr("common.exit")},
                                             [this](int c) {
                                                 if (c == 1) {
                                                     app.quit();
                                                 }
                                             }));
            }
        }

    private:
        struct Card {
            RectangleShape *bg = nullptr;
            RectangleShape *icon = nullptr;
            ui::Label *glyph = nullptr;
            ui::Label *name = nullptr;
            ui::Label *desc = nullptr;
        };

        void refresh() {
            for (int i = 0; i < buttonCount; i++) {
                auto &c = cards[i];
                bool on = (i == focus);
                c.bg->setFillColor(on ? cardOn : cardOff);
                c.bg->setOutlineThickness(on ? 0 : 2);
                c.icon->setFillColor(on ? iconOn : iconOff);
                c.name->setColor(on ? darkOn : theme::textStrong());
                c.desc->setColor(on ? theme::withAlpha(theme::background(), 153) : theme::withAlpha(theme::textPrimary(), 140));
            }
        }

        Card cards[4];
        c2d::C2DTexture *bgTex = nullptr;
        c2d::C2DTexture *logoTex = nullptr;
        int buttonCount = 4;
        int focus = 0;
        const c2d::Color cardOff = theme::withAlpha(theme::textPrimary(), 18);
        const c2d::Color cardOn = theme::textStrong();
        const c2d::Color iconOff = theme::withAlpha(theme::textPrimary(), 30);
        const c2d::Color iconOn = theme::background();
        const c2d::Color darkOn = theme::background();
    };

    ////////////////////////////////////////////////////////////////////////////////////////////////
    class ProfilesScreen : public Screen, public ui::ListView::Adapter {
    public:
        explicit ProfilesScreen(App &a) : Screen(a) {
            ui::background(this);
            screens::header(this, tr("profiles.title"), tr("profiles.subtitle"));
            list = new ui::ListView(FloatRect(theme::SAFE_X, 220, 900, 720), 112, 16, this);
            add(list);
            detail = ui::box(this, FloatRect(1060, 220, 764, 720), theme::surface(), theme::RADIUS_LARGE);
            detailTitle = ui::label(detail, "", theme::HEADING, 48, 44, ui::Weight::SemiBold);
            detailTitle->setMaxWidth(668);
            detailBody = ui::label(detail, "", theme::BODY, 48, 112, ui::Weight::Regular, theme::textSecondary());
            detailBody->setMaxWidth(668);
            detailBody->setMaxLines(12);
            hints = screens::hintBar(this, {});
            refresh();
        }

        ~ProfilesScreen() override {
            refreshToken.cancel();
        }

        const char *name() const override { return "profiles"; }

        void onResume() override {
            list->reload();
            refresh();
        }

        // ListView::Adapter
        int count() override { return (int) app.profiles().profiles().size() + 1; }

        C2DObject *createRow(float w, float h) override {
            auto *row = ui::box(nullptr, FloatRect(0, 0, w, h), theme::card(), theme::RADIUS);
            auto *title = ui::label(row, "", theme::HEADING, 36, 18, ui::Weight::SemiBold);
            title->setMaxWidth(w - 220);
            auto *sub = ui::label(row, "", theme::LABEL, 36, 64, ui::Weight::Regular, theme::textSecondary());
            sub->setMaxWidth(w - 72);
            auto *badge = ui::label(row, "", theme::LABEL, 0, 22, ui::Weight::SemiBold, theme::success());
            badge->setAlign(ui::Align::Right, w - 36);
            rows.push_back({row, title, sub, badge});
            return row;
        }

        void bindRow(C2DObject *obj, int index, bool selected, bool focused) override {
            for (auto &r: rows) {
                if (r.bg != obj) {
                    continue;
                }
                const auto &profiles = app.profiles().profiles();
                bool add = index >= (int) profiles.size();
                r.bg->setFillColor(focused ? theme::cardHover() : theme::card());
                r.bg->setOutlineColor(theme::accent());
                r.bg->setOutlineThickness(focused ? theme::FOCUS_BORDER : 0);
                (void) selected;
                if (add) {
                    r.title->setText("+  " + tr("profiles.add"));
                    r.title->setColor(theme::accentText());
                    r.sub->setText(tr("profiles.add_sub"));
                    r.badge->setText("");
                } else {
                    const Profile &p = profiles[(size_t) index];
                    r.title->setText(p.name);
                    r.title->setColor(theme::textPrimary());
                    r.sub->setText(subtitleOf(p));
                    bool active = p.id == app.profiles().activeId();
                    r.badge->setText(active ? tr("profiles.active_badge") : "");
                }
            }
        }

        void handleInput(const InputEvent &e) override {
            const auto &profiles = app.profiles().profiles();
            int index = list->selected();
            bool onAdd = index >= (int) profiles.size();
            switch (e.button) {
                case PadButton::Up:
                    list->moveSelection(-1);
                    break;
                case PadButton::Down:
                    list->moveSelection(1);
                    break;
                case PadButton::Cross:
                    if (e.repeat) {
                        return;
                    }
                    if (onAdd) {
                        app.push(screens::makeSourceChooser(app));
                    } else {
                        open(profiles[(size_t) index]);
                    }
                    return;
                case PadButton::Square:
                    if (!onAdd && !e.repeat) {
                        app.push(screens::makeProfileEdit(app, profiles[(size_t) index]));
                    }
                    return;
                case PadButton::Options:
                    if (!e.repeat) {
                        if (onAdd) {
                            app.push(screens::makeSourceChooser(app));
                        } else {
                            actions(profiles[(size_t) index]);
                        }
                    }
                    return;
                case PadButton::Triangle:
                    if (!onAdd && !e.repeat) {
                        confirmDelete(profiles[(size_t) index]);
                    }
                    return;
                case PadButton::Circle:
                    if (app.session().connected || app.session().offline) {
                        app.replaceAll(screens::makeHome(app));
                    } else {
                        app.push(screens::makeDialog(app, tr("app.exit_title"), "",
                                                     {tr("common.cancel"), tr("common.exit")}, [this](int c) {
                            if (c == 1) {
                                app.quit();
                            }
                        }));
                    }
                    return;
                default:
                    return;
            }
            refresh();
        }

    private:
        struct Row {
            RectangleShape *bg;
            ui::Label *title;
            ui::Label *sub;
            ui::Label *badge;
        };

        // "Xtream Codes  •  host  •  Connected" / "M3U / M3U8  •  1,284 channels"
        std::string subtitleOf(const Profile &p) const {
            const std::string sep = "   \xE2\x80\xA2   ";
            std::string s = screens::sourceTypeName(p.type);
            if (p.isPlaylist()) {
                if (refreshingId == p.id) {
                    return s + sep + tr("m3u.refreshing");
                }
                return s + sep + (p.playlistChannels >= 0 ? i18n::count("m3u.channels", p.playlistChannels)
                                                          : tr("profiles.not_loaded"));
            }
            return s + sep + screens::hostOf(p.server) + (p.lastStatus.empty() ? "" : sep + p.lastStatus);
        }

        void open(const Profile &p) {
            app.profiles().setActive(p.id);
            app.profiles().save();
            app.replaceAll(screens::makeConnect(app, p));
        }

        // Options on a source: Open, Edit, Refresh Playlist (playlists), Delete, Add Source
        void actions(const Profile &p) {
            std::string id = p.id;
            std::vector<std::string> names = {tr("profiles.open"), tr("common.edit")};
            std::vector<std::function<void()>> acts;
            acts.push_back([this, id] {
                if (const Profile *q = app.profiles().find(id)) {
                    open(*q);
                }
            });
            acts.push_back([this, id] {
                if (const Profile *q = app.profiles().find(id)) {
                    app.push(screens::makeProfileEdit(app, *q));
                }
            });
            if (p.isPlaylist()) {
                names.push_back(tr("m3u.refresh"));
                acts.push_back([this, id] { refreshPlaylist(id); });
            }
            names.push_back(tr("common.delete"));
            acts.push_back([this, id] {
                if (const Profile *q = app.profiles().find(id)) {
                    confirmDelete(*q);
                }
            });
            names.push_back(tr("profiles.add"));
            acts.push_back([this] { app.push(screens::makeSourceChooser(app)); });
            app.push(screens::makeMenu(app, p.name, names, -1, whileAlive([acts](int c) {
                if (c >= 0 && c < (int) acts.size()) {
                    acts[(size_t) c]();
                }
            })));
        }

        // a playlist source that is not open: download it again into its saved copy
        void refreshPlaylist(const std::string &id) {
            const Profile *p = app.profiles().find(id);
            if (p == nullptr) {
                return;
            }
            if ((app.session().connected || app.session().offline) && app.session().profile.id == id) {
                app.refreshPlaylist(true);   // the open source: its list on screen is replaced too
                return;
            }
            if (!refreshingId.empty()) {
                app.toast(tr("m3u.refresh_running"));
                return;
            }
            refreshingId = id;
            list->reload();
            refreshToken = app.m3u().load(*p, APP_DATA_DIR, M3uService::Source::Network, true,
                                          [this, id](M3uService::Outcome &o) {
                refreshingId.clear();
                if (o.ok) {
                    int n = o.info.stats.channels;
                    app.notePlaylistLoaded(id, n);
                    app.toast(i18n::count("m3u.refreshed", n), ToastKind::Success);
                } else {
                    app.toast(tr("m3u.refresh_failed", {o.message}), ToastKind::Error);
                }
                list->reload();
                refresh();
            });
        }

        void confirmDelete(const Profile &p) {
            std::string id = p.id;
            app.push(screens::makeDialog(app, tr("profiles.delete_title"), tr("profiles.delete_text", {p.name}),
                                         {tr("common.cancel"), tr("common.delete")}, [this, id](int c) {
                        if (c != 1) {
                            return;
                        }
                        bool wasActive = (app.session().connected || app.session().offline) && app.session().profile.id == id;
                        bool playlist = app.profiles().find(id) && app.profiles().find(id)->isPlaylist();
                        app.profiles().remove(id);
                        if (playlist) {
                            CatalogCache(APP_DATA_DIR).remove(id, M3uService::CACHE_NAME);   // the saved playlist
                        }
                        std::string err;
                        if (!app.profiles().save(&err)) {
                            LOG_E("profiles", "save failed: %s", err.c_str());
                            app.toast(tr("profiles.save_failed"), ToastKind::Error);
                        }
                        app.syncDownloadProfiles();   // completed downloads stay playable; queued ones stop
                        if (wasActive) {
                            app.resetSession();
                        }
                        if (app.profiles().profiles().empty()) {
                            app.replaceAll(screens::makeOnboarding(app));
                            return;
                        }
                        list->reload();
                        refresh();
                        app.toast(tr("profiles.deleted"), ToastKind::Success);
                    }, true));
        }

        void refresh() {
            const auto &profiles = app.profiles().profiles();
            int index = list->selected();
            if (index >= (int) profiles.size()) {
                detailTitle->setText(tr("profiles.add_title"));
                detailBody->setText(tr("profiles.add_text"));
                hints->setHints({{A::Confirm, tr("common.add")}, {A::Back, tr("common.back")}});
                return;
            }
            const Profile &p = profiles[(size_t) index];
            detailTitle->setText(p.name);
            std::string body = tr("source.type") + "\n" + screens::sourceTypeName(p.type) + "\n\n";
            if (p.isPlaylist()) {
                body += tr("playlist.url") + "\n" + screens::sourceLocation(p) + "\n\n" + tr("playlist.user_agent")
                        + "\n" + (p.userAgent.empty() ? tr("playlist.user_agent_default") : p.userAgent);
            } else if (p.isStalker()) {
                body += tr("stalker.portal_url") + "\n" + screens::hostOf(p.server) + "\n\n" + tr("stalker.mac") + "\n"
                        + stalker::maskedMac(p.mac);
            } else {
                body += tr("profile.server") + "\n" + screens::hostOf(p.server) + "\n\n" + tr("profile.username")
                        + "\n" + p.username + "\n\n" + tr("profile.password") + "\n" + screens::mask(p.password);
            }
            if (!p.lastStatus.empty()) {
                body += "\n\n" + tr("profiles.last_connection") + "\n" + p.lastStatus;
                if (p.lastUsedAt > 0) {
                    body += " (" + clockx::localDate(p.lastUsedAt) + ")";
                }
            }
            detailBody->setText(body);
            hints->setHints({{A::Confirm, tr(p.isPlaylist() ? "profiles.open" : "profiles.connect")},
                             {A::Edit, tr("common.edit")}, {A::DeleteItem, tr("common.delete")},
                             {A::Options, tr("profiles.options")}, {A::Back, tr("common.back")}});
        }

        ui::ListView *list;
        RectangleShape *detail;
        ui::Label *detailTitle;
        ui::Label *detailBody;
        ui::HintBar *hints;
        std::vector<Row> rows;
        std::string refreshingId;   // playlist source being refreshed from this list
        CancelToken refreshToken;
    };

    ////////////////////////////////////////////////////////////////////////////////////////////////
    // Trocar usuário: pick one of the registered users (the circles from the mock artwork) or add a new
    // one; below them sits an "Editar" action that turns the list into an editable one (configure or
    // delete each user). Selecting a user opens the source configuration (Xtream / M3U / EPG) for it.
    class UsersScreen : public Screen {
    public:
        explicit UsersScreen(App &a) : Screen(a) {
            ui::background(this);
            const float cx = theme::SCREEN_W / 2.0f;
            auto *t = ui::label(this, tr("users.title"), theme::DISPLAY, 0, 92, ui::Weight::SemiBold,
                                 theme::textStrong());
            t->setAlign(ui::Align::Center, theme::SCREEN_W);
            auto *s = ui::label(this, tr("users.subtitle"), theme::BODY, 0, 236, ui::Weight::Regular,
                                 theme::textSecondary());
            s->setAlign(ui::Align::Center, theme::SCREEN_W);

            // soft panel behind the circle row (the mock artwork)
            panel = ui::box(this, FloatRect(theme::SAFE_X, 280, theme::SCREEN_W - 2 * theme::SAFE_X, 620),
                            theme::withAlpha(theme::background(), 96), theme::RADIUS_LARGE);
            panel->setOutlineColor(theme::withAlpha(theme::textPrimary(), 24));
            panel->setOutlineThickness(1.0f);

            editButton = ui::box(this, FloatRect(cx - 130, 900, 260, 72), theme::surfaceElevated(), 36);
            editLabel = ui::label(editButton, tr("users.edit"), theme::BODY, 0, 18, ui::Weight::SemiBold,
                                  theme::textPrimary());
            editLabel->setAlign(ui::Align::Center, 260);
            limitNote = ui::label(this, tr("users.limit_reached"), theme::CAPTION, 0, 908,
                                  ui::Weight::SemiBold, theme::warning());
            limitNote->setAlign(ui::Align::Center, theme::SCREEN_W);
            limitNote->setVisibility(Visibility::Hidden);

            hints = screens::hintBar(this, {});
            rebuild();
        }

        const char *name() const override { return "users"; }

        void onResume() override {
            rebuild();
        }

        void handleInput(const InputEvent &e) override {
            const int addIndexL = addIndex;
            const int editIndexL = editIndex;
            if (e.button == PadButton::Left || e.button == PadButton::Right) {
                if (e.repeat) {
                    return;
                }
                if (focus == editIndex) {
                    return;
                }
                const int row = focus / 2;
                const int col = focus % 2;
                const int nextCol = col + (e.button == PadButton::Right ? 1 : -1);
                if (nextCol >= 0 && nextCol < 2) {
                    const int candidate = row * 2 + nextCol;
                    if (candidate < (int) circles.size()) {
                        focus = candidate;
                    }
                }
                refresh();
                return;
            }
            if (e.button == PadButton::Up || e.button == PadButton::Down) {
                if (e.repeat) {
                    return;
                }
                if (focus == editIndex) {
                    if (e.button == PadButton::Up && !circles.empty()) {
                        focus = (int) circles.size() - 1;
                    }
                } else {
                    const int candidate = focus + (e.button == PadButton::Down ? 2 : -2);
                    if (candidate >= 0 && candidate < (int) circles.size()) {
                        focus = candidate;
                    } else if (e.button == PadButton::Down) {
                        focus = editIndex;
                    }
                }
                refresh();
                return;
            }
            if (e.button == PadButton::Cross && !e.repeat) {
                if (focus == editIndexL) {
                    toggleEditMode();
                    return;
                }
                if (focus == addIndexL) {
                    app.push(screens::makeSourceChooser(app));   // add a new user
                    return;
                }
                if (focus < 0 || focus >= (int) profiles().size()) {
                    return;
                }
                const Profile &p = profiles()[(size_t) focus];
                if (editMode) {
                    renameUser(p);   // in edit mode, Cross renames the user
                    return;
                }
                app.profiles().setActive(p.id);
                app.profiles().save();
                app.replaceAll(screens::makeConnect(app, p));    // straight into that profile (saved copy when valid)
                return;
            }
            if (e.button == PadButton::Triangle && !e.repeat && editMode) {
                if (focus >= 0 && focus < (int) profiles().size()) {
                    requestDelete(profiles()[(size_t) focus]);
                }
                return;
            }
            if (e.button == PadButton::Circle && !e.repeat) {
                if (editMode) {
                    toggleEditMode();
                    return;
                }
                app.pop();
                return;
            }
        }

    private:
        struct CircleRef {
            RectangleShape *bg;    // profile card
            RectangleShape *ring;  // focused / active outline
            ui::Label *label;      // avatar monogram
            ui::Label *name;       // profile name
            ui::Label *badge;      // active-user marker
            std::string id;        // profile id ("" for the add card)
        };

        const std::vector<Profile> &profiles() const { return app.profiles().profiles(); }

        void renameUser(const Profile &p) {
            std::string id = p.id;
            app.push(screens::makeKeyboard(app, tr("profile.name"), p.name, screens::KeyboardKind::Default,
                                           [this, id](const std::string &v) {
                std::string name = v;
                while (!name.empty() && name.back() == ' ') {
                    name.pop_back();
                }
                if (name.empty()) {
                    return;
                }
                const Profile *q = app.profiles().find(id);
                if (q == nullptr || q->name == name) {
                    return;
                }
                Profile np = *q;
                np.name = name;
                app.profiles().upsert(np, clockx::unixNow());
                std::string err;
                if (!app.profiles().save(&err)) {
                    app.toast(tr("profile.save_failed"), ToastKind::Error);
                    return;
                }
                rebuild();
            }));
        }

        void toggleEditMode() {
            editMode = !editMode;
            refresh();
        }

        void requestDelete(const Profile &p) {
            std::string id = p.id;
            app.push(screens::makeDialog(app, tr("profiles.delete_title"), tr("profiles.delete_text", {p.name}),
                                          {tr("common.cancel"), tr("common.delete")}, [this, id](int c) {
                         if (c != 1) {
                             return;
                         }
                         bool wasActive = (app.session().connected || app.session().offline)
                                          && app.session().profile.id == id;
                         bool playlist = app.profiles().find(id) && app.profiles().find(id)->isPlaylist();
                         app.profiles().remove(id);
                         if (playlist) {
                             CatalogCache(APP_DATA_DIR).remove(id, M3uService::CACHE_NAME);
                         }
                         CatalogStore(APP_DATA_DIR).remove(id);   // the processed catalog belongs to the deleted user
                         std::string err;
                         if (!app.profiles().save(&err)) {
                             LOG_E("profiles", "save failed: %s", err.c_str());
                             app.toast(tr("profiles.save_failed"), ToastKind::Error);
                         }
                         app.syncDownloadProfiles();
                         if (wasActive) {
                             app.resetSession();
                         }
                         if (app.profiles().profiles().empty()) {
                             app.replaceAll(screens::makeOnboarding(app));
                             return;
                         }
                         rebuild();
                         app.toast(tr("profiles.deleted"), ToastKind::Success);
                     }, true));
        }

        void rebuild() {
            for (auto &c: circles) {
                for (auto *s: {static_cast<C2DObject *>(c.bg), static_cast<C2DObject *>(c.ring),
                               static_cast<C2DObject *>(c.label), static_cast<C2DObject *>(c.name),
                               static_cast<C2DObject *>(c.badge)}) {
                    if (s != nullptr) {
                        remove(s);
                        delete s;
                    }
                }
            }
            circles.clear();
            const auto &list = profiles();
            const bool atLimit = (int) list.size() >= ProfileStore::MAX_PROFILES;
            const int n = (int) list.size() + (atLimit ? 0 : 1);
            addIndex = atLimit ? -1 : (int) list.size();
            editIndex = (int) list.size() + (atLimit ? 0 : 1);
            if (limitNote) {
                limitNote->setVisibility(atLimit ? Visibility::Visible : Visibility::Hidden);
            }

            // Profile picker: compact two-column cards, sized for up to ten users.
            // Each card has a distinct monogram tile, a readable name and an active marker.
            const float cardW = 690.0f;
            const float cardH = 104.0f;
            const float gapX = 54.0f;
            const float gapY = 14.0f;
            const float totalW = cardW * 2.0f + gapX;
            const float left = (theme::SCREEN_W - totalW) / 2.0f;
            const float top = 300.0f;
            for (int i = 0; i < n; ++i) {
                const bool isAdd = addIndex >= 0 && i == addIndex;
                const Profile *p = isAdd ? nullptr : &list[(size_t) i];
                const int col = i % 2;
                const int row = i / 2;
                const float x = left + col * (cardW + gapX);
                const float y = top + row * (cardH + gapY);

                auto *bg = new RectangleShape(FloatRect(x, y, cardW, cardH));
                bg->setCornersRadius(18);
                bg->setCornerPointCount(12);
                bg->setFillColor(isAdd ? theme::surfaceElevated() : theme::card());
                bg->setOutlineColor(theme::none());
                bg->setOutlineThickness(0);
                add(bg);

                auto *ring = new RectangleShape(FloatRect(x - 3, y - 3, cardW + 6, cardH + 6));
                ring->setCornersRadius(21);
                ring->setCornerPointCount(12);
                ring->setFillColor(theme::none());
                ring->setOutlineColor(theme::none());
                ring->setOutlineThickness(0);
                add(ring);

                const std::string initials = isAdd ? "+" : (p->name.empty() ? "?" : p->name.substr(0, 1));
                auto *avatar = new RectangleShape(FloatRect(x + 18, y + 14, 76, 76));
                avatar->setCornersRadius(16);
                avatar->setCornerPointCount(12);
                avatar->setFillColor(isAdd ? theme::surface() : theme::monogram((unsigned)i));
                add(avatar);

                auto *label = ui::label(this, initials, 38, x + 18, y + 30,
                                        ui::Weight::SemiBold, theme::textStrong());
                label->setAlign(ui::Align::Center, 76);
                label->setMaxWidth(76);

                auto *name = ui::label(this, isAdd ? tr("users.add") : p->name,
                                       theme::HEADING, x + 118, y + 20,
                                       ui::Weight::SemiBold, isAdd ? theme::accentText() : theme::textPrimary());
                name->setMaxWidth(cardW - 230);

                auto *badge = ui::label(this,
                    p != nullptr && p->id == app.profiles().activeId() ? tr("profiles.active_badge") : "",
                    theme::CAPTION, x + 118, y + 64, ui::Weight::SemiBold, theme::accentText());
                badge->setMaxWidth(cardW - 230);

                circles.push_back({bg, ring, label, name, badge, p != nullptr ? p->id : std::string()});
            }

            editButton->setPosition(theme::SCREEN_W / 2.0f - 130.0f, 900.0f);
            editLabel->setPosition(0, 18);
            focus = std::clamp(focus, 0, editIndex);
            refresh();
        }

        void refresh() {
            const int addIndex = this->addIndex;
            const int editIndex = this->editIndex;
            for (size_t i = 0; i < circles.size(); i++) {
                CircleRef &c = circles[i];
                const bool isAdd = addIndex >= 0 && (int) i == addIndex;
                const bool focused = (int) i == focus;
                const bool active = !isAdd && !c.id.empty() && c.id == app.profiles().activeId();
                c.bg->setFillColor(focused ? theme::cardHover() : (isAdd ? theme::surfaceElevated() : theme::card()));
                c.ring->setOutlineColor(active || focused ? theme::accent() : theme::none());
                c.ring->setOutlineThickness(focused ? theme::FOCUS_BORDER : (active ? 2.0f : 0.0f));
                c.ring->setScale(focused ? 1.015f : 1.0f, focused ? 1.015f : 1.0f);
                c.name->setColor(isAdd ? theme::accentText() : theme::textPrimary());
                c.badge->setVisibility(active ? Visibility::Visible : Visibility::Hidden);
            }
            // the Editar button sits below the circles; in edit mode it reads as "done"
            editLabel->setText(editMode ? tr("common.done") : tr("users.edit"));
            editButton->setOutlineColor(editMode || focus == editIndex ? theme::accent() : theme::none());
            editButton->setOutlineThickness(editMode || focus == editIndex ? 3.0f : 0.0f);
            editButton->setFillColor(focus == editIndex ? theme::cardHover() : theme::surfaceElevated());
            if (editMode) {
                hints->setHints({{A::Confirm, tr("users.rename")}, {A::DeleteItem, tr("users.delete")},
                                 {A::Back, tr("common.back")}});
            } else {
                hints->setHints({{A::Confirm, tr("common.select")}, {A::Back, tr("common.back")}});
            }
        }

        std::vector<CircleRef> circles;
        RectangleShape *panel = nullptr;
        RectangleShape *editButton = nullptr;
        ui::Label *editLabel = nullptr;
        ui::HintBar *hints = nullptr;
        ui::Label *limitNote = nullptr;
        int addIndex = -1;      // index of the "+" circle; -1 when the 10-profile cap hides it
        int editIndex = 0;      // index of the Editar button (below the circles)
        bool editMode = false;
        int focus = 0;
    };

    ////////////////////////////////////////////////////////////////////////////////////////////////
    // Xtream Codes (name, server, username, password) and Stalker / MAG (name, portal address, MAC address) sources:
    // the same form, tested with the source's own sign-in before it is saved
    class ProfileEditScreen : public Screen {
    public:
        ProfileEditScreen(App &a, const Profile &p) : Screen(a), profile(p) {
            isNew = p.id.empty();
            portal = p.isStalker();
            fieldCount = portal ? 3 : 4;
            if (!isNew) {
                serverInput = p.server;
            }
            macInput = p.mac;
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

            // h1 + subtitle
            const char *titleKey = portal ? (isNew ? "stalker.add_title" : "stalker.edit_title")
                                          : (isNew ? "profile.add_title" : "profile.edit_title");
            auto *t = ui::label(this, tr(titleKey), 56, 110, 88, ui::Weight::SemiBold, theme::textStrong());
            (void) t;
            auto *s = ui::label(this, tr(portal ? "stalker.subtitle" : "profile.subtitle"), 24, 110, 172,
                                ui::Weight::Regular, theme::withAlpha(theme::textPrimary(), 158));
            (void) s;

            static const char *const XTREAM_CAPTIONS[4] = {"profile.name", "profile.server_url", "profile.username",
                                                           "profile.password"};
            static const char *const PORTAL_CAPTIONS[3] = {"profile.name", "stalker.portal_url", "stalker.mac"};
            const char *const *captions = portal ? PORTAL_CAPTIONS : XTREAM_CAPTIONS;
            const float labelYs[4] = {270, 400, 530, 660};
            for (int i = 0; i < fieldCount; i++) {
                auto *cap = ui::label(this, tr(captions[i]), 21, 110, labelYs[i], ui::Weight::SemiBold,
                                      theme::withAlpha(theme::textPrimary(), 158));
                (void) cap;
                fields[i] = new screens::FormField(FloatRect(110, labelYs[i] + 35, 1000, 70), tr(captions[i]));
                add(fields[i]);
            }

            // side panel with provider hints (mock .side)
            statusSide = ui::box(this, FloatRect(1260, 300, 560, 360), theme::withAlpha(theme::textPrimary(), 16), 26);
            auto *st = ui::label(statusSide, tr(titleKey), 24, 36, 34, ui::Weight::SemiBold, theme::textStrong());
            (void) st;
            std::string info = tr(portal ? "source.type_stalker_detail" : "source.type_xtream_detail");
            auto *sb = ui::label(statusSide, info, 22, 36, 86, ui::Weight::Regular, theme::withAlpha(theme::textPrimary(), 192));
            sb->setMaxWidth(560 - 72);
            sb->setMaxLines(8);

            buttons[0] = new screens::PillButton(tr("profile.test"), FloatRect(110, 800, 280, 74), true);
            buttons[1] = new screens::PillButton(tr("common.back"), FloatRect(410, 800, 180, 74), false);
            for (auto *b: buttons) add(b);

            statusLine = ui::label(this, "", 22, 110, 905, ui::Weight::SemiBold, theme::textSecondary());
            statusLine->setMaxWidth(1550);
            statusLine->setMaxLines(2);

            setStatus(tr(isNew ? "profile.status_new" : "profile.status_edit"), theme::textSecondary());
            refresh();
        }

        ~ProfileEditScreen() override {
            testToken.cancel();
            if (bgTex) { delete bgTex; bgTex = nullptr; }
        }

        const char *name() const override { return "profile-edit"; }

        bool animating() const override { return false; }

        void tick(double) override {}

        // physical keyboard: typing on a focused field opens its editor with what was typed
        bool handleKey(const kbd::Event &k) override {
            if (k.key != kbd::Key::Char || focus >= fieldCount || testing) {
                return false;
            }
            seed = k.text;
            activate();
            return true;
        }

        void handleInput(const InputEvent &e) override {
            const int firstButton = fieldCount;
            switch (e.button) {
                case PadButton::Up:
                    if (focus >= firstButton) {
                        focus = firstButton - 1;
                    } else if (focus > 0) {
                        focus--;
                    }
                    break;
                case PadButton::Down:
                    if (focus < firstButton) {
                        focus++;
                    }
                    break;
                case PadButton::Left:
                    if (focus > firstButton) {
                        focus--;
                    }
                    break;
                case PadButton::Right:
                    if (focus >= firstButton && focus < firstButton + 2) {
                        focus++;
                    }
                    break;
                case PadButton::Cross:
                    if (!e.repeat) {
                        activate();
                    }
                    return;
                case PadButton::Circle:
                    app.pop();
                    return;
                default:
                    return;
            }
            refresh();
        }

    private:

        void activate() {
            if (portal) {
                switch (focus) {
                    case 0:
                        edit(tr("profile.name"), profile.name, false, [this](const std::string &v) { profile.name = v; });
                        return;
                    case 1:
                        edit(tr("stalker.portal_url"), serverInput, false, [this](const std::string &v) { serverInput = v; });
                        return;
                    case 2:
                        edit(tr("stalker.mac"), macInput, false, [this](const std::string &v) { macInput = v; });
                        return;
                    case 3:
                        test(false);
                        return;
                    case 4:
                        test(true);
                        return;
                    default:
                        app.pop();
                        return;
                }
            }
            switch (focus) {
                case 0:
                    edit(tr("profile.name"), profile.name, false, [this](const std::string &v) { profile.name = v; });
                    break;
                case 1:
                    edit(tr("profile.server_url"), serverInput, false, Kbd::Url, [this](const std::string &v) { serverInput = v; });
                    break;
                case 2:
                    edit(tr("profile.username"), profile.username, false, [this](const std::string &v) { profile.username = v; });
                    break;
                case 3:
                    edit(tr("profile.password"), profile.password, true, [this](const std::string &v) { profile.password = v; });
                    break;
                case 4:
                    test(false);
                    break;
                case 5:
                    test(true);
                    break;
                default:
                    app.pop();
                    break;
            }
        }

        enum class Kbd { Default, Url, Ascii };
        void edit(const std::string &title, const std::string &value, bool secret, std::function<void(const std::string &)> set) {
            edit(title, value, secret, Kbd::Default, set);
        }
        void edit(const std::string &title, const std::string &value, bool secret, Kbd kind, std::function<void(const std::string &)> set) {
            std::string initial = value + seed;   // a character typed on a physical keyboard opened the editor
            seed.clear();
            screens::KeyboardKind k = kind == Kbd::Url ? screens::KeyboardKind::Url
                                    : secret || kind == Kbd::Ascii ? screens::KeyboardKind::Ascii
                                                                 : screens::KeyboardKind::Default;
            app.push(screens::makeKeyboard(app, title, initial, k, [this, set](const std::string &v) {
                set(v);
                tested = false;
                refresh();
            }));
        }

        // validates the form into `profile`; returns an error message or ""
        std::string validate() {
            if (portal) {
                stalker::Portal pt = stalker::portalFor(serverInput);
                if (!pt.ok) {
                    return tr(pt.error.c_str());
                }
                std::string mac;
                if (!stalker::normalizeMac(macInput, mac)) {
                    return tr("stalker.error.mac");
                }
                std::string server = url::trim(serverInput);
                if (url::lower(server).find("://") == std::string::npos) {
                    server = "http://" + server;   // i18n-exempt: technical value
                }
                profile.type = SourceType::Stalker;
                profile.server = server;
                profile.mac = mac;
                macInput = mac;
                if (url::trim(profile.name).empty()) {
                    profile.name = screens::hostOf(pt.origin);
                }
                return "";
            }
            url::Server s = url::normalizeServer(serverInput);
            if (!s.ok) {
                return s.error;
            }
            if (url::trim(profile.username).empty()) {
                return tr("profile.error.username");
            }
            if (profile.password.empty()) {
                return tr("profile.error.password");
            }
            profile.server = s.base;
            profile.username = url::trim(profile.username);
            if (url::trim(profile.name).empty()) {
                profile.name = s.displayHost;
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
            if (thenSave && tested) {
                save();
                return;
            }
            testing = true;
            setStatus(tr("profile.testing"), theme::textSecondary());
            LOG_I("profiles", "testing connection to %s", m3u::displayUrl(profile.server).c_str());
            testToken = app.sources().authenticate(profile, [this, thenSave](const XtreamService::AuthOutcome &o) {
                testing = false;
                if (o.result.status == AuthStatus::Ok) {
                    tested = true;
                    std::string body = tr("auth.ok") + "\n\n" + screens::accountSummary(o.result.account);
                    if (o.httpsWarning) {
                        body += "\n\n" + tr("profile.https_warning");
                    }
                    setStatus(body, o.httpsWarning ? theme::warning() : theme::success());
                    if (thenSave) {
                        save();
                    }
                } else {
                    tested = false;
                    std::string body = o.message;
                    if (o.result.status == AuthStatus::Expired && o.result.account.expiresAt > 0) {
                        body += "\n" + tr("profile.expired_on", {clockx::localDate(o.result.account.expiresAt)});
                    }
                    if (!o.result.account.message.empty()) {
                        body += "\n\n" + tr("profile.provider_message", {o.result.account.message});
                    }
                    if (thenSave) {
                        body += "\n\n" + tr("profile.not_saved");
                    }
                    setStatus(body, theme::danger());
                }
                refresh();
            });
            refresh();
        }

        void save() {
            Profile p = profile;
            std::string id = app.profiles().upsert(p, clockx::unixNow());
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
            app.syncDownloadProfiles();
            LOG_I("profiles", "profile %s saved (%s)", id.c_str(), screens::sourceLocation(p).c_str());
            app.replaceAll(screens::makeConnect(app, *app.profiles().find(id)));
        }

        void setStatus(const std::string &text, const Color &color) {
            if (statusLine) {
                statusLine->setText(text);
                statusLine->setColor(color);
            }
        }

        void refresh() {
            LOG_I("profiles", "refresh entered (server %zu chars, user %zu chars)", serverInput.size(),
                  profile.username.size());
            fields[0]->setValue(profile.name.empty() ? tr("profile.name_placeholder") : profile.name, profile.name.empty());
            if (portal) {
                fields[1]->setValue(serverInput.empty() ? "http://example.com:8080/c/" : serverInput, serverInput.empty());   // i18n-exempt: technical value
                fields[2]->setValue(macInput.empty() ? "00:1A:79:00:00:00" : macInput, macInput.empty());
            } else {
                fields[1]->setValue(serverInput.empty() ? "http://example.com:8080" : serverInput, serverInput.empty());
                fields[2]->setValue(profile.username.empty() ? tr("profile.required") : profile.username,
                                    profile.username.empty());
                fields[3]->setValue(profile.password.empty() ? tr("profile.required") : screens::mask(profile.password),
                                    profile.password.empty());
            }
            for (int i = 0; i < fieldCount; i++) {
                fields[i]->setFocused(focus == i);
            }
            for (int i = 0; i < 2; i++) {
                buttons[i]->setFocused(focus == fieldCount + i);
                buttons[i]->setEnabled(!testing);
            }
            LOG_I("profiles", "refresh exited");
        }

        Profile profile;
        std::string serverInput;
        std::string macInput;
        std::string seed;          // character typed on a physical keyboard that opened the field's editor
        bool isNew;
        bool portal = false;       // Stalker / MAG source
        int fieldCount = 4;
        bool tested = false;
        bool testing = false;
        int focus = 0;
        screens::FormField *fields[4] = {};
        screens::PillButton *buttons[2] = {};
        RectangleShape *statusSide = nullptr;
        ui::Label *statusLine = nullptr;
        c2d::C2DTexture *bgTex = nullptr;
        CancelToken testToken;
    };
}

namespace screens {
    Screen *makeOnboarding(App &app) {
        return new OnboardingScreen(app);
    }

    Screen *makeProfiles(App &app) {
        return new ProfilesScreen(app);
    }

    Screen *makeUsers(App &app) {
        return new UsersScreen(app);
    }

    Screen *makeProfileEdit(App &app, const Profile &profile) {
        if (profile.isPlaylist()) {
            return makePlaylistEdit(app, profile);
        }
        return new ProfileEditScreen(app, profile);
    }
}
