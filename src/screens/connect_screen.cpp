// Sign-in and initial loading: account, then Live categories + streams. Movies and Series are loaded lazily when
// their section opens. Xtream and Stalker / MAG sources share this flow (App::sources routes by source type).
//
// When the provider cannot be reached (no network, server down, expired account...), "Continue offline" opens
// Home with the saved channel / movie / series lists and the Downloads section: completed downloads play
// without any provider request. It is focused first when the failure is a network one and downloads exist.
//
// Playlist (M3U / M3U8) sources have no sign-in: the saved playlist opens Home at once and is refreshed in the
// background when it is older than M3uService::REFRESH_AFTER. Only a source without a saved playlist waits for
// the download (a failure offers Retry / Edit / Profiles; there is nothing to continue offline with).

#include "common.h"
#include "../iptv/xtream.h"
#include "../platform/clock.h"
#include "../platform/log.h"

using namespace c2d;
using namespace iptv;

namespace {

    constexpr int STEPS = 2;

    class ConnectScreen : public Screen {
    public:
        ConnectScreen(App &a, const Profile &p) : Screen(a), profile(p) {
            // ---------------- background: deep violet radial glow
            ui::background(this);
            auto *glow = new CircleShape(560.0f);
            glow->setPointCount(56);
            glow->setFillColor(theme::withAlpha(theme::accent(), 36));
            glow->setOrigin(Origin::Center);
            glow->setPosition(theme::SCREEN_W / 2.0f, 330.0f);
            add(glow);

            // ---------------- small wordmark; the screen is just the list on the violet background
            ui::label(this, "ZIMTV", 44, 0, 120, ui::Weight::SemiBold, theme::withAlpha(theme::textStrong(), 235))
                ->setAlign(ui::Align::Center, theme::SCREEN_W);
            // which profile is opening (Netflix-style: the profile you picked is named before it loads)
            ui::label(this, p.name, theme::TITLE, 0, 235, ui::Weight::SemiBold, theme::accentText())
                ->setAlign(ui::Align::Center, theme::SCREEN_W);

            // ---------------- step cards (the list)
            const char *playlistNames[] = {"connect.step_saved_playlist", "connect.step_download_playlist"};
            const char *xtreamNames[] = {"connect.step_account", "connect.step_live"};
            const char **names = p.isPlaylist() ? playlistNames : xtreamNames;
            for (int i = 0; i < STEPS; i++) {
                Step &s = steps[i];
                float y = 420 + (float) i * 110;
                s.bg = ui::box(this, FloatRect(460, y, 1000, 94), theme::withAlpha(theme::card(), 200), 22);
                s.accent = ui::box(s.bg, FloatRect(0, 20, 6, 56), theme::accent(), 3);
                s.accent->setVisibility(Visibility::Hidden);
                s.spinner = new ui::Spinner(18);
                s.spinner->setPosition(36, 47);
                s.bg->add(s.spinner);
                s.check = ui::label(s.bg, "", theme::HEADING, 28, 24, ui::Weight::SemiBold);
                ui::label(s.bg, tr(names[i]), theme::LABEL, 82, 12, ui::Weight::SemiBold, theme::textStrong());
                ui::label(s.bg, tr("splash.step_status_k", {std::to_string(i + 1)}), theme::CAPTION, 82, 46,
                          ui::Weight::SemiBold, theme::withAlpha(theme::textPrimary(), 132));
                s.detail = ui::label(s.bg, "", theme::CAPTION, 0, 40, ui::Weight::Regular, theme::textSecondary());
                s.detail->setAlign(ui::Align::Right, 928);
                s.detail->setMaxWidth(500);
                s.detail->setMaxLines(1);
                s.state = State::Pending;
                updateStepUI(s);
            }

            // ---------------- failure buttons (shown only when the connection fails)
            const float bw = 300;
            const float gap = 24;
            float bx = (theme::SCREEN_W - (2 * bw + gap)) / 2;
            retryBtn = new ui::Button(tr("common.retry"), FloatRect(bx, 760, bw, 78), true);
            backBtn = new ui::Button(tr("connect.profiles"), FloatRect(bx + bw + gap, 760, bw, 78), false);
            retryBtn->setVisibility(Visibility::Hidden);
            backBtn->setVisibility(Visibility::Hidden);
            add(retryBtn);
            add(backBtn);

            // ---------------- failure message
            message = ui::label(this, "", theme::LABEL, 0, 640, ui::Weight::Regular, theme::danger());
            message->setAlign(ui::Align::Center, theme::SCREEN_W);
            message->setMaxWidth(1400);
            message->setMaxLines(3);
            message->setVisibility(Visibility::Hidden);

            // ---------------- hints
            hintBar = screens::hintBar(this, {{A::Back, tr("common.cancel")}});

            failed = false;
        }

        ~ConnectScreen() override {
            token.cancel();
        }

        const char *name() const override { return "connect"; }

        void onEnter() override {
            start();
        }

        void tick(double now) override {
            bool changed = false;
            for (auto &s: steps) {
                if (s.state == State::Running) {
                    changed |= s.spinner->tick(now);
                }
            }
            if (changed) {
                redraw();
            }
        }

        void handleInput(const InputEvent &e) override {
            if (failed) {
                if (e.button == PadButton::Left || e.button == PadButton::Right) {
                    failFocus = (failFocus + 1) % 2;
                } else if (e.button == PadButton::Cross && !e.repeat) {
                    if (failFocus == 0) {
                        start();
                    } else {
                        app.replaceAll(screens::makeUsers(app));
                    }
                    return;
                } else if (e.button == PadButton::Circle) {
                    app.replaceAll(screens::makeUsers(app));
                    return;
                }
                retryBtn->setFocused(failFocus == 0);
                backBtn->setFocused(failFocus == 1);
            } else if (e.button == PadButton::Circle) {
                token.cancel();
                LOG_I("connect", "canceled by the user");
                app.replaceAll(screens::makeUsers(app));
            }
        }

    private:
        enum class State {
            Pending,
            Running,
            Done,
            Failed
        };

        struct Step {
            RectangleShape *bg = nullptr;
            RectangleShape *accent = nullptr;
            ui::Spinner *spinner = nullptr;
            ui::Label *check = nullptr;
            ui::Label *detail = nullptr;
            State state = State::Pending;
        };

        void updateStepUI(Step &s) {
            const bool active = s.state == State::Running;
            const bool done = s.state == State::Done;
            const bool failedStep = s.state == State::Failed;
            s.spinner->setVisibility(active ? Visibility::Visible : Visibility::Hidden);
            s.accent->setVisibility(active ? Visibility::Visible : Visibility::Hidden);
            s.check->setText(done ? "\xE2\x9C\x93" : (failedStep ? "!" : ""));
            s.check->setColor(done ? theme::success() : theme::danger());
            s.check->setVisibility(done || failedStep ? Visibility::Visible : Visibility::Hidden);
            s.detail->setColor(failedStep ? theme::danger() : theme::textSecondary());
            s.bg->setFillColor(active ? theme::surfaceElevated() : theme::withAlpha(theme::card(), 200));
        }

        void start() {
            failed = false;
            offline = false;
            message->setVisibility(Visibility::Hidden);
            retryBtn->setVisibility(Visibility::Hidden);
            backBtn->setVisibility(Visibility::Hidden);
            for (int i = 0; i < STEPS; i++) {
                steps[i].state = State::Pending;
                steps[i].detail->setText("");
                updateStepUI(steps[i]);
            }
                        hintBar->setHints({{A::Back, tr("common.cancel")}});
            app.resetSession();
            if (profile.isPlaylist()) {
                startPlaylist();
                return;
            }
            steps[0].state = State::Running;
            steps[0].detail->setText(tr("connect.signing_in"));
            updateStepUI(steps[0]);
                        token = app.sources().authenticate(profile, [this](const XtreamService::AuthOutcome &o) {
                onAuth(o);
            });
            redraw();
        }

        void onAuth(const XtreamService::AuthOutcome &o) {
            int64_t now = clockx::unixNow();
            app.profiles().setLastStatus(profile.id, o.message, now);
            app.profiles().save();
            if (o.result.status != AuthStatus::Ok) {
                steps[0].state = State::Failed;
                steps[0].detail->setText(o.message);
                updateStepUI(steps[0]);
                std::string msg = o.message;
                if (o.result.status == AuthStatus::Expired && o.result.account.expiresAt > 0) {
                    msg = tr("connect.expired_on", {msg, clockx::localDate(o.result.account.expiresAt)});
                } else if (o.result.status == AuthStatus::Network) {
                    msg = tr("connect.check_network", {msg});
                }
                fail(msg, o.result.status == AuthStatus::Network);
                return;
            }
            Session &s = app.session();
            s.profile = profile;
            s.account = o.result.account;
            s.httpsWarning = o.httpsWarning;
            app.library().setProfile(profile.id);
            app.vod().reset();   // Movies/Series of this profile load when first opened
            app.vod().setOffline(false);
            steps[0].state = State::Done;
            steps[0].detail->setText(o.result.account.expiresAt > 0
                                    ? tr("connect.active_until", {clockx::localDate(o.result.account.expiresAt)})
                                    : tr("account.active"));
            updateStepUI(steps[0]);
                        loadLive(false);
        }

        // the provider cannot be reached: the saved lists and the downloads
        void continueOffline() {
            LOG_I("connect", "continuing offline with profile %s", profile.id.c_str());
            failed = false;
            offline = true;
            for (auto *b: {retryBtn, backBtn}) {
                b->setVisibility(Visibility::Hidden);
            }
            message->setText("");
            app.resetSession();
            Session &s = app.session();
            s.profile = profile;
            s.offline = true;
            app.library().setProfile(profile.id);
            app.vod().reset();
            app.vod().setOffline(true);
            loadLive(true);
        }

        void loadLive(bool cacheOnly) {
            steps[1].state = State::Running;
            steps[1].detail->setText(tr(cacheOnly ? "connect.loading_saved" : "connect.loading_channels"));
            updateStepUI(steps[1]);
                        token = app.sources().loadLive(profile, APP_DATA_DIR, [this](XtreamService::LiveOutcome &o) {
                Session &s = app.session();
                if (o.ok) {
                    s.categories[0] = o.categories;
                    s.categoriesLoaded[0] = true;
                    s.live.assign(std::move(o.categories), std::move(o.channels));
                    s.live.fromCache = o.fromCache;
                    s.live.loadedAt = o.fromCache ? o.savedAt : clockx::unixNow();
                    s.liveLoaded = true;
                    s.liveNotice = o.fromCache ? o.message : "";
                    std::string detail = i18n::count("connect.categories", (long long) s.live.categories().size())
                                         + "  \xE2\x80\xA2  "
                                         + i18n::count("home.channels", (long long) s.live.channels().size());
                    steps[1].state = State::Done;
                    steps[1].detail->setText(o.fromCache ? tr("connect.saved_list", {detail}) : detail);
                    updateStepUI(steps[1]);
                                    } else {
                    steps[1].state = State::Failed;
                    steps[1].detail->setText(o.message);
                    updateStepUI(steps[1]);
                                    }
                s.connected = !offline;
                LOG_I("connect", "ready%s: live %d categories / %d channels", offline ? " (offline)" : "",
                      (int) s.categories[0].size(), (int) s.live.channels().size());
                app.refreshEpgAuto();   // Xtream: /xmltv.php for the signed-in source (background job)
                app.replaceAll(screens::makeHome(app));
            }, cacheOnly);
            redraw();
        }

        // ------------------------------------------------------------------ playlist sources
        void startPlaylist() {
            Session &s = app.session();
            s.profile = profile;
            app.library().setProfile(profile.id);
            app.vod().reset();
            app.vod().setOffline(false);
            steps[0].state = State::Running;
            steps[0].detail->setText(tr("connect.loading_saved"));
            updateStepUI(steps[0]);
                        token = app.m3u().load(profile, APP_DATA_DIR, M3uService::Source::Cache, false, [this](M3uService::Outcome &o) {
                if (!o.ok) {
                    steps[0].state = State::Done;
                    steps[0].detail->setText(tr("connect.no_saved_playlist"));
                    updateStepUI(steps[0]);
                                        downloadPlaylist();
                    return;
                }
                steps[0].state = State::Done;
                steps[0].detail->setText(playlistDetail(o));
                updateStepUI(steps[0]);
                bool stale = clockx::unixNow() - o.info.savedAt > M3uService::REFRESH_AFTER;
                steps[1].state = State::Done;
                steps[1].detail->setText(tr(stale ? "connect.update_background" : "connect.playlist_current"));
                updateStepUI(steps[1]);
                                finishPlaylist(o, stale);
            });
            redraw();
        }

        void downloadPlaylist() {
            steps[1].state = State::Running;
            steps[1].detail->setText(tr("connect.downloading_playlist"));
            updateStepUI(steps[1]);
                        token = app.m3u().load(profile, APP_DATA_DIR, M3uService::Source::Network, true, [this](M3uService::Outcome &o) {
                if (!o.ok) {
                    steps[1].state = State::Failed;
                    steps[1].detail->setText(o.message);
                    updateStepUI(steps[1]);
                    app.profiles().setLastStatus(profile.id, o.message, clockx::unixNow());
                    app.profiles().save();
                    fail(o.networkFailure ? tr("connect.check_playlist_network", {o.message}) : o.message, false);
                    return;
                }
                steps[1].state = State::Done;
                steps[1].detail->setText(playlistDetail(o));
                updateStepUI(steps[1]);
                                finishPlaylist(o, false);
            });
        }

        static std::string playlistDetail(const M3uService::Outcome &o) {
            return i18n::count("connect.categories", (long long) o.catalog->categories().size()) + "  \xE2\x80\xA2  "
                   + i18n::count("home.channels", (long long) o.catalog->channels().size());
        }

        void finishPlaylist(M3uService::Outcome &o, bool refreshAfter) {
            int channels = o.info.stats.channels;
            Session &s = app.session();
            s.connected = true;
            app.setPlaylist(o.catalog, o.info, {o.movies, o.series, o.seriesInfo});
            app.notePlaylistLoaded(profile.id, channels);
            LOG_I("connect", "playlist ready: %d live groups / %d live channels (%d entries in the playlist)%s",
                  (int) s.live.categories().size(), (int) s.live.channels().size(), channels,
                  refreshAfter ? ", refreshing in the background" : "");
            app.replaceAll(screens::makeHome(app));
            if (refreshAfter) {
                app.refreshPlaylist(false);
            }
        }

        void fail(const std::string &msg, bool networkProblem) {
            failed = true;
            failFocus = 0;
            retryBtn->setVisibility(Visibility::Visible);
            backBtn->setVisibility(Visibility::Visible);
            message->setText(msg);
            message->setVisibility(Visibility::Visible);
                        retryBtn->setFocused(failFocus == 0);
            backBtn->setFocused(failFocus == 1);
            hintBar->setHints({{A::Confirm, tr("common.select")}, {A::Back, tr("connect.profiles")}});
            LOG_W("connect", "connect failed: %s", msg.c_str());
        }

        Profile profile;
        Step steps[STEPS];
        ui::Label *message = nullptr;
        ui::Button *retryBtn = nullptr;
        ui::Button *backBtn = nullptr;
        ui::HintBar *hintBar = nullptr;
        CancelToken token;
        bool failed = false;
        bool offline = false;
        int failFocus = 0;
    };
}

namespace screens {
    Screen *makeConnect(App &app, const Profile &profile) {
        return new ConnectScreen(app, profile);
    }
}
