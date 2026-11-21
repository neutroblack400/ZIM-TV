// TV ao vivo: CyberTV-inspired three-panel layout (Categorias | Canais | Now/Next) with neon-violet
// framing and real provider logos.
// Everything on screen comes from the active profile: categories from the playlist/Xtream groups,
// channels with their tvg-logo (never invented), EPG only when the profile actually carries one.

#include <algorithm>
#include <ctime>
#include <string>
#include <unordered_map>
#include <vector>

#include "common.h"
#include "../core/format.h"
#include "../epg/xmltv.h"
#include "../platform/clock.h"

using namespace c2d;
using namespace iptv;

namespace {

    struct CatRow { std::string id; std::string name; int count = 0; bool fav = false; };

    // the panel rectangle shared by all three panes (CyberTV mock proportions, neon-violet border)
    constexpr float PANEL_TOP = 128;
    constexpr float PANEL_H = 692;
    constexpr float PANEL_R = 18;
    const c2d::Color PANEL_BG = theme::withAlpha(theme::surface(), 242);
    const c2d::Color NEON = theme::accent();

    // initials + a stable hue for a category's icon tile
    struct CatIcon {
        std::string text;   // up to 2 letters
        c2d::Color bg;
    };

    CatIcon catIcon(const std::string &name) {
        std::string key = name;
        size_t sep = key.find('|');
        if (sep != std::string::npos) {
            key = key.substr(sep + 1);
        }
        key.erase(std::remove_if(key.begin(), key.end(), [](unsigned char c) { return std::isspace(c); }),
                  key.end());
        std::string letters;
        for (size_t i = 0; i < key.size() && letters.size() < 2; i++) {
            if (std::isalpha((unsigned char) key[i])) {
                letters += (char) std::toupper((unsigned char) key[i]);
            }
        }
        if (letters.empty()) {
            letters = "?";
        }
        unsigned hash = 0;
        for (unsigned char c: name) {
            hash = hash * 31 + c;
        }
        CatIcon icon;
        icon.text = letters;
        icon.bg = theme::swatch(hash % (unsigned) palette::ACCENT_COUNT);
        return icon;
    }

    std::string formatLocalTime(int64_t unixTime) {
        std::time_t adjusted = (std::time_t) (unixTime + (int64_t) clockx::utcOffsetMinutes() * 60);
        std::tm tm{};
        char buf[8] = {};
        if (gmtime_r(&adjusted, &tm) != nullptr) {
            std::snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
        }
        return buf;
    }

    class LiveScreen : public Screen {
    public:
        explicit LiveScreen(App &a, bool startOnFavorites = false) : Screen(a) {
            (void) startOnFavorites;
            ui::background(this);

            // ambient violet glow (top-right and bottom-left, the mock's signature streaks)
            add(glowEllipse(PANEL_TOP + PANEL_H * 0.5f, theme::SCREEN_W + theme::SCREEN_H * 0.30f, 900));
            add(glowEllipse(PANEL_TOP + PANEL_H * 0.55f, -theme::SCREEN_W * 0.25f, 1000));

            // header nav
            auto *zim = ui::label(this, "ZIM", 44, theme::SAFE_X + 16, 30, ui::Weight::SemiBold, theme::success());
            (void) zim;
            auto *tv = ui::label(this, "TV", 44, theme::SAFE_X + 26, 64, ui::Weight::SemiBold, theme::accent());
            (void) tv;
            float nx = theme::SAFE_X + 210;
            const char *keys[] = {"home.home", "home.live_tv", "home.movies", "home.series"};
            for (int i = 0; i < 4; i++) {
                auto *t = ui::label(this, tr(keys[i]), 22, nx, 54, i == 1 ? ui::Weight::SemiBold : ui::Weight::Regular,
                                    i == 1 ? theme::textStrong() : theme::textSecondary());
                if (i == 1) ui::box(this, FloatRect(nx, 86, t->width(), 4), theme::accent(), 2);
                nx += t->width() + 50;
            }
            clockLabel = ui::label(this, "", 20, 0, 54, ui::Weight::Regular, theme::textSecondary());
            clockLabel->setAlign(ui::Align::Right, theme::SCREEN_W - 48);

            buildPanels();
            hints = screens::hintBar(this, {{A::Confirm, tr("common.open")}, {A::Search, tr("home.search")},
                                            {A::Favorite, tr("home.settings")}, {A::Back, tr("common.back")}});
            rebuild();
            setFocusPane(1);
        }

        const char *name() const override { return "live"; }

        void onResume() override {
            rebuild();
        }

        void tick(double) override {
            updateClock();
            if (app.session().liveGeneration != liveGen) {
                rebuild();
            } else if (app.session().epgGeneration != epgGen) {
                updatePreview();
                epgGen = app.session().epgGeneration;
            } else if (app.images().generation() != imgGen) {
                updateChannels();
                updatePreview();
                imgGen = app.images().generation();
            }
        }

        void handleInput(const InputEvent &e) override {
            if (e.button == PadButton::Circle && !e.repeat) {
                if (focusPane == 2 || focusPane == 1) setFocusPane(focusPane - 1);
                else app.pop();
                return;
            }
            if (e.button == PadButton::Triangle && !e.repeat) { app.push(screens::makeSearch(app, 3)); return; }
            if (e.button == PadButton::Square && !e.repeat) { app.push(screens::makeSettings(app)); return; }

            if (focusPane == 0) { // categories
                if (e.button == PadButton::Up || e.button == PadButton::Down) {
                    catIndex = std::clamp(catIndex + (e.button == PadButton::Down ? 1 : -1), 0, std::max(0, (int) cats.size() - 1));
                    selectCategory(catIndex);
                } else if (e.button == PadButton::Right && !e.repeat) {
                    if (!visible.empty()) setFocusPane(1);
                } else if (e.button == PadButton::Cross && !e.repeat) {
                    if (!visible.empty()) setFocusPane(1);
                }
            } else if (focusPane == 1) { // channels
                if (e.button == PadButton::Up || e.button == PadButton::Down) {
                    chIndex = std::clamp(chIndex + (e.button == PadButton::Down ? 1 : -1), 0, std::max(0, (int) visible.size() - 1));
                    updateChannels();
                    updatePreview();
                } else if (e.button == PadButton::Left && !e.repeat) {
                    setFocusPane(0);
                } else if (e.button == PadButton::Right && !e.repeat) {
                    setFocusPane(2);
                } else if (e.button == PadButton::Cross && !e.repeat) {
                    playSelected();
                }
            } else { // preview / details pane
                if (e.button == PadButton::Left && !e.repeat) setFocusPane(1);
                else if (e.button == PadButton::Cross && !e.repeat) playSelected();
            }
        }

    private:
        // a soft, oversized violet ellipse for the ambient background glow
        static CircleShape *glowEllipse(float cy, float cx, float radius) {
            auto *glow = new CircleShape(radius);
            glow->setPointCount(40);
            glow->setFillColor(theme::withAlpha(theme::accent(), 18));
            glow->setOutlineThickness(0.0f);
            glow->setPosition(cx, cy);
            return glow;
        }

        static RectangleShape *neonPanel(float x, float y, float w, float h) {
            auto *panel = ui::box(nullptr, FloatRect(x, y, w, h), PANEL_BG, PANEL_R);
            panel->setOutlineColor(NEON);
            panel->setOutlineThickness(2.0f);
            panel->setCornerPointCount(14);
            return panel;
        }

        void updateClock() {
            clockLabel->setText(clockx::localTime());
        }

        void buildPanels() {
            // ------------------------------------------------ Painel 1: Categorias (sem barra de pesquisa)
            catPanel = neonPanel(40, PANEL_TOP, 460, PANEL_H);
            add(catPanel);
            ui::label(catPanel, tr("live.categories"), theme::HEADING, 26, 22, ui::Weight::SemiBold, theme::textStrong());

            // ------------------------------------------------ Painel 2: Canais (logos verdadeiras)
            chPanel = neonPanel(525, PANEL_TOP, 460, PANEL_H);
            add(chPanel);
            ui::label(chPanel, tr("live.channels"), theme::HEADING, 26, 22, ui::Weight::SemiBold, theme::textStrong());

            // ------------------------------------------------ Painel 3: informações do canal
            infoPanel = neonPanel(1010, PANEL_TOP, 870, PANEL_H);
            add(infoPanel);

            // black stage with the channel's own identity (its logo, ampliada)
            stage = ui::box(infoPanel, FloatRect(18, 18, 870 - 36, 300), theme::videoBackground(), 16);
            prevLogo = new ui::LogoView(FloatRect((870 - 36 - 360) / 2, 34, 360, 232), 72, 18, 1.7f);
            prevLogo->setFillColor(theme::none());   // stage is pure black; never a tile on the mock's stage
            stage->add(prevLogo);

            prevName = ui::label(infoPanel, "", theme::TITLE, 28, 336, ui::Weight::SemiBold, theme::textStrong());
            prevName->setMaxWidth(870 - 56);
            prevMeta = ui::label(infoPanel, "", theme::LABEL, 28, 392, ui::Weight::Regular, theme::textSecondary());
            prevMeta->setMaxWidth(870 - 56);

            ui::box(infoPanel, FloatRect(28, 440, 870 - 56, 1), theme::withAlpha(theme::accent(), 90), 0);

            ui::label(infoPanel, tr("live.description"), theme::CAPTION, 28, 452, ui::Weight::SemiBold, theme::accentText());
            prevDesc = ui::label(infoPanel, "", theme::BODY, 28, 482, ui::Weight::Regular, theme::textSecondary());
            prevDesc->setMaxWidth(870 - 56);
            prevDesc->setMaxLines(2);

            // A seguir: the label, a hairline that runs to the right edge, then the programme row
            auto *nextLabel = ui::label(infoPanel, tr("guide.next"), theme::LABEL, 28, 545, ui::Weight::SemiBold, theme::accentText());
            float labelW = nextLabel->width();
            ui::box(infoPanel, FloatRect(28 + labelW + 14, 554, 870 - 56 - labelW - 14, 1), theme::withAlpha(theme::accent(), 90), 0);

            progTile = ui::box(infoPanel, FloatRect(28, 578, 214, 108), theme::withAlpha(theme::videoBackground(), 220), 14);
            progTile->setOutlineColor(theme::withAlpha(theme::accent(), 110));
            progTile->setOutlineThickness(1.5f);
            // generic "programme" glyph: a play triangle inside a rounded screen outline
            auto *screen = new RectangleShape(FloatRect(27, 20, 160, 68));
            screen->setFillColor(theme::none());
            screen->setOutlineColor(theme::withAlpha(theme::textStrong(), 170));
            screen->setOutlineThickness(2.0f);
            screen->setCornersRadius(10);
            screen->setCornerPointCount(8);
            progTile->add(screen);
            auto *glyph = new ConvexShape(3);
            glyph->setPoint(0, Vector2f(103, 44));
            glyph->setPoint(1, Vector2f(103, 64));
            glyph->setPoint(2, Vector2f(121, 54));
            glyph->setFillColor(theme::accent());
            progTile->add(glyph);

            progName = ui::label(infoPanel, "", theme::HEADING, 262, 580, ui::Weight::SemiBold, theme::textStrong());
            progName->setMaxWidth(870 - 262 - 40);
            progName->setMaxLines(2);
            progTime = ui::label(infoPanel, "", theme::LABEL, 262, 646, ui::Weight::Regular, theme::textSecondary());
            progTime->setMaxWidth(870 - 262 - 40);
        }

        void rebuild() {
            liveGen = app.session().liveGeneration;
            epgGen = app.session().epgGeneration;
            imgGen = app.images().generation();
            const auto &live = app.session().live;
            cats.clear();
            // Favoritos first (the mock), then "All channels"
            int favs = 0;
            for (const auto &ch: live.channels()) {
                if (app.library().isFavorite(ContentType::Live, ch.id)) favs++;
            }
            cats.push_back({"__fav__", tr("home.favorites"), favs, true});
            cats.push_back({"__all__", tr("live.all_channels"), (int) live.channels().size(), false});
            for (const auto &c: live.categories()) {
                cats.push_back({c.id, c.name, (int) live.inCategory(c.id).size(), false});
            }
            if (catIndex >= (int) cats.size()) catIndex = 0;
            selectCategory(catIndex, false);
            renderCategories();
            updateChannels();
            updatePreview();
        }

        void renderCategories() {
            for (auto *o: catRows) { catPanel->remove(o); delete o; }
            catRows.clear();
            float y = 64;
            int start = std::max(0, catIndex - 8);
            for (int i = start; i < (int) cats.size() && y < PANEL_H - 16; i++) {
                bool sel = (i == catIndex);
                bool focused = (focusPane == 0 && sel);
                Color bg = sel ? theme::accent() : theme::none();
                auto *row = ui::box(catPanel, FloatRect(12, y, 460 - 24, 52), bg, 12);
                // icon: star for favorites, a hue-tinted monogram otherwise
                if (cats[i].fav) {
                    auto *starBg = ui::box(row, FloatRect(8, 6, 40, 40), theme::withAlpha(theme::textStrong(), sel ? 40 : 26), 10);
                    drawStar(starBg, 20, 20, 11, 5, theme::textStrong());
                } else {
                    CatIcon icon = catIcon(cats[i].name);
                    auto *tile = ui::box(row, FloatRect(8, 6, 40, 40), icon.bg, 10);
                    auto *letters = ui::label(tile, icon.text, 17, 0, ui::Label::centerOffset(17, 40), ui::Weight::SemiBold,
                                              theme::background());
                    letters->setAlign(ui::Align::Center, 40);
                }
                Color text = sel ? theme::textStrong() : theme::textPrimary();
                auto *name = ui::label(row, cats[i].name, theme::LABEL, 60, ui::Label::centerOffset(theme::LABEL, 52),
                                       sel ? ui::Weight::SemiBold : ui::Weight::Regular, text);
                name->setMaxWidth(300);
                auto *cnt = ui::label(row, std::to_string(cats[i].count), theme::LABEL, 0, ui::Label::centerOffset(theme::LABEL, 52),
                                      ui::Weight::SemiBold, sel ? theme::textStrong() : theme::textSecondary());
                cnt->setAlign(ui::Align::Right, 460 - 24 - 10);
                catRows.push_back(row);
                y += 58;
            }
        }

        static void drawStar(RectangleShape *parent, float cx, float cy, float outerR, float innerR, const Color &c) {
            constexpr int POINTS = 10;
            auto *star = new ConvexShape(POINTS);
            for (int i = 0; i < POINTS; i++) {
                float r = (i % 2 == 0) ? outerR : innerR;
                float a = (float) i * 0.6283185f - 1.5707963f;
                star->setPoint((size_t) i, Vector2f(cx + r * std::cos(a), cy + r * std::sin(a)));
            }
            star->setFillColor(c);
            parent->add(star);
        }

        void selectCategory(int idx, bool resetChannel = true) {
            if (idx < 0 || idx >= (int) cats.size()) return;
            catIndex = idx;
            visible.clear();
            const auto &live = app.session().live;
            if (cats[idx].id == "__all__") {
                for (size_t i = 0; i < live.channels().size(); i++) visible.push_back((int) i);
            } else if (cats[idx].id == "__fav__") {
                for (size_t i = 0; i < live.channels().size(); i++) {
                    if (app.library().isFavorite(ContentType::Live, live.channels()[i].id)) visible.push_back((int) i);
                }
            } else {
                visible = live.inCategory(cats[idx].id);
            }
            if (resetChannel) chIndex = 0; else chIndex = std::clamp(chIndex, 0, std::max(0, (int) visible.size() - 1));
            renderCategories();
            updateChannels();
            updatePreview();
        }

        void updateChannels() {
            for (auto *o: chRows) { chPanel->remove(o); delete o; }
            chRows.clear();
            const auto &chs = app.session().live.channels();
            float y = 64;
            int start = std::max(0, chIndex - 5);
            std::vector<ImageRequest> req;
            for (int i = start; i < (int) visible.size() && y < PANEL_H - 16; i++) {
                const LiveChannel &ch = chs[(size_t) visible[i]];
                bool sel = (i == chIndex);
                bool focused = (focusPane == 1 && sel);
                Color bg = sel ? theme::accent() : theme::none();
                auto *row = ui::box(chPanel, FloatRect(12, y, 460 - 24, 56), bg, 12);
                auto *logo = new ui::LogoView(FloatRect(8, (56 - 46) / 2, 88, 46), 20, 3, 1.3f);
                logo->setCornersRadius(8);
                row->add(logo);
                req.push_back({ImageKind::Logo, ch.icon});
                std::shared_ptr<ImageSet> img = app.images().get(ImageKind::Logo, ch.icon);
                logo->set(ch.name, img ? img->at(0).texture : nullptr, img ? img->at(0).size : Vector2i());
                Color tc = sel ? theme::textStrong() : theme::textPrimary();
                auto *name = ui::label(row, ch.name, theme::BODY, 108, ui::Label::centerOffset(theme::BODY, 56),
                                       sel ? ui::Weight::SemiBold : ui::Weight::Regular, tc);
                name->setMaxWidth(460 - 108 - 18);
                chRows.push_back(row);
                y += 64;
            }
            if (app.settings().get().loadImages) {
                app.images().want(req);
            }
            renderCategories(); // keep highlight consistent
        }

        void updatePreview() {
            prevLogo->set("", nullptr, Vector2i());
            progName->setText("");
            progTime->setText("");
            if (visible.empty() || chIndex < 0 || chIndex >= (int) visible.size()) {
                prevName->setText("");
                prevMeta->setText(tr("guide.no_information"));
                prevDesc->setText(tr("live.description_unavailable"));
                progName->setText(tr("live.epg_unavailable"));
                return;
            }
            const LiveChannel &ch = app.session().live.channels()[(size_t) visible[chIndex]];
            prevName->setText(ch.name);
            // the category line, with the provider's "Canais |" prefix stripped for display
            std::string cat = app.session().live.categoryName(ch.categoryId);
            size_t sep = cat.find('|');
            if (sep != std::string::npos && sep + 1 < cat.size()) {
                cat = cat.substr(sep + 1);
            }
            while (!cat.empty() && (cat.front() == ' ' || cat.front() == '\t')) cat.erase(cat.begin());
            prevMeta->setText(cat.empty() ? tr("live.all_channels") : cat);
            prevDesc->setText(tr("live.description_unavailable"));   // no channel synopsis exists in the data: never invent
            std::shared_ptr<ImageSet> img = app.images().get(ImageKind::Logo, ch.icon);
            prevLogo->set(ch.name, img ? img->at(1).texture : nullptr, img ? img->at(1).size : Vector2i());
            // A seguir: the guide's next programme (or the current one), only when the profile has an EPG
            std::string nextTitle, nextTime;
            const auto *guide = app.session().epg.get();
            if (guide != nullptr && !ch.epgId.empty()) {
                auto it = guide->byChannel.find(ch.epgId);
                if (it != guide->byChannel.end()) {
                    int64_t tnow = clockx::unixNow();
                    const epg::Program *nowP = nullptr, *nextP = nullptr;
                    for (const auto &p : it->second) {
                        if (p.start <= tnow && tnow < p.stop) nowP = &p;
                        else if (p.start > tnow) { nextP = &p; break; }
                    }
                    const epg::Program *shown = nextP ? nextP : nowP;
                    if (shown != nullptr) {
                        nextTitle = shown->title;
                        nextTime = formatLocalTime(shown->start) + " – " + formatLocalTime(shown->stop);
                    }
                }
            }
            if (nextTitle.empty()) {
                progName->setText(tr("live.epg_unavailable"));   // e.g. this provider ships no EPG
                progTime->setText("");
            } else {
                progName->setText(nextTitle);
                progTime->setText(nextTime);
            }
            if (app.settings().get().loadImages) {
                app.images().want({{ImageKind::Logo, ch.icon}});
            }
        }

        void setFocusPane(int p) {
            focusPane = p;
            catPanel->setOutlineColor(p == 0 ? theme::textStrong() : NEON);
            catPanel->setOutlineThickness(p == 0 ? 3.0f : 2.0f);
            chPanel->setOutlineColor(p == 1 ? theme::textStrong() : NEON);
            chPanel->setOutlineThickness(p == 1 ? 3.0f : 2.0f);
            infoPanel->setOutlineColor(p == 2 ? theme::textStrong() : NEON);
            infoPanel->setOutlineThickness(p == 2 ? 3.0f : 2.0f);
            renderCategories();
            updateChannels();
        }

        void playSelected() {
            if (visible.empty() || chIndex < 0 || chIndex >= (int) visible.size()) return;
            app.push(screens::makeLivePlayer(app, visible, chIndex));
        }

        ui::Label *clockLabel = nullptr;
        RectangleShape *catPanel = nullptr, *chPanel = nullptr, *infoPanel = nullptr, *stage = nullptr, *progTile = nullptr;
        ui::LogoView *prevLogo = nullptr;
        ui::Label *prevName = nullptr, *prevMeta = nullptr, *prevDesc = nullptr;
        ui::Label *progName = nullptr, *progTime = nullptr;
        ui::HintBar *hints = nullptr;
        std::vector<C2DObject *> catRows, chRows;
        std::vector<CatRow> cats;
        std::vector<int> visible;
        int focusPane = 1, catIndex = 0, chIndex = 0;
        unsigned liveGen = 0, imgGen = 0, epgGen = 0;
    };
}

namespace screens {
    Screen *makeLive(App &app, bool startOnFavorites) { return new LiveScreen(app, startOnFavorites); }
}
