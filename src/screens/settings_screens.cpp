// Settings (main page, Storage & downloads, Diagnostics) and About.
//
// Settings rows: caption on the left, the value in one right-aligned column - a choice (with dots showing
// its position among the options), a switch for On/Off settings, or a short info text for actions. Rows are
// separators on the background; the focused one gets the tinted surface and the accent edge. The column on
// the right (no box) explains the focused setting and its current value. Up/Down move, Left/Right or X
// change a value, X runs an action. Appearance shows the accent presets as a 3 x 3 palette: the D-pad moves
// inside it, X applies a colour. Geometry: ui/layout.h (layout::settings).
//
// Language changes apply at once: the screens are rebuilt (App::applyLanguage) and Settings opens again.

#include <algorithm>
#include <cmath>
#include <iterator>

#include "common.h"
#include "../ui/option_input.h"
#include "build_info.h"
#include "../platform/log.h"
#include "../storage/catalog_cache.h"

using namespace c2d;
using screens::SettingsPage;

namespace {

    // settings: panel composed from the theme tokens (no colour literals; tests require it)
    const c2d::Color PANEL_BG = theme::panel();
    const c2d::Color PANEL_EDGE = theme::withAlpha(theme::accent(), 115);
    const c2d::Color PILL = theme::accent();
    const c2d::Color PILL_RING = theme::withAlpha(theme::textStrong(), 110);
    const c2d::Color TOGGLE_ON = theme::accent();
    const c2d::Color TOGGLE_ON_GLOW = theme::withAlpha(theme::accent(), 70);
    const c2d::Color TOGGLE_OFF = theme::switchOff();
    const c2d::Color NAV_IDLE = theme::textSecondary();
    const c2d::Color NAV_ACTIVE = theme::textStrong();
    const c2d::Color ROW_TEXT = theme::textPrimary();
    const c2d::Color DESC_TEXT = theme::textSecondary();
    const c2d::Color MUTED_TEXT = theme::textMuted();
    const c2d::Color STATUS_BG = theme::withAlpha(theme::textPrimary(), 26);
    const c2d::Color STATUS_TEXT = theme::textPrimary();
    const c2d::Color STATUS_ERR_BG = theme::withAlpha(theme::danger(), 51);
    const c2d::Color STATUS_ERR_TEXT = theme::danger();
    const c2d::Color DOT_ON = theme::accent();
    const c2d::Color DOT_ON_GLOW = theme::withAlpha(theme::accent(), 80);
    const c2d::Color DOT_OFF = theme::withAlpha(theme::textPrimary(), 64);
    const c2d::Color SEP = theme::withAlpha(theme::textPrimary(), 22);

    // three columns: categories | the category's settings | what the focused setting does
    namespace L = layout::settings;

    const float PANEL_PAD = 26;

    const float VALUE_RIGHT = 24;      // right padding of the value column
    const float VALUE_W = 300;
    const float SWITCH_W = 88;
    const float SWITCH_H = 44;
    const float SWATCH_R = 14;
    const float CELL_GAP = 8;          // between palette cells

    enum class Kind {
        Choice,
        Toggle,
        Action,
        Palette     // a row of accent presets (Appearance): Left / Right pick one, X applies it
    };

    struct SettingItem {
        Kind kind = Kind::Action;
        std::string caption;
        std::vector<std::string> options;         // Choice
        std::function<int()> choice;              // Choice: current option index
        std::function<void(int)> setChoice;
        std::function<bool()> toggle;             // Toggle
        std::function<void(bool)> setToggle;
        std::function<std::string()> info;        // Action: short text in the value column
        std::function<void()> run;                // Action, Swatch
        std::function<std::string()> describe;    // text for the description panel
        std::vector<int> swatches;                // Palette: palette::ACCENTS indices, left to right
    };

    struct Category {
        std::string name;
        std::vector<SettingItem> items;
    };

    // one cell of the accent palette: swatch, name, a check mark on the colour in use
    struct PaletteCell {
        RectangleShape *bg;
        CircleShape *swatch;
        ui::Label *name;
        ui::Label *check;
    };

    // one settings row (re-bound to different items while scrolling)
    class SettingRow : public ui::RowFrame {
    public:
        SettingRow(float w, float h) : ui::RowFrame(w, h, 0) {
            caption = ui::label(this, "", theme::BODY, layout::ROW_PAD + 4, ui::Label::centerOffset(theme::BODY, h));
            caption->setMaxWidth(w - layout::ROW_PAD - VALUE_W - 40);
            value = ui::label(this, "", theme::LABEL + 2, 0, 0, ui::Weight::SemiBold, ROW_TEXT);
            value->setAlign(ui::Align::Right, w - VALUE_RIGHT);
            value->setMaxWidth(VALUE_W);
            // the choice dots: small rounded bars (mock); the focused option stretches
            for (int i = 0; i < 6; i++) {
                auto *d = new RectangleShape(FloatRect(0, 0, 10, 5));
                d->setFillColor(DOT_OFF);
                d->setCornersRadius(2.5f);
                d->setCornerPointCount(4);
                d->setVisibility(Visibility::Hidden);
                add(d);
                dots.push_back(d);
            }
            dotGlow = new RectangleShape(FloatRect(0, 0, 30, 9));
            dotGlow->setFillColor(DOT_ON_GLOW);
            dotGlow->setCornersRadius(4.5f);
            dotGlow->setCornerPointCount(4);
            dotGlow->setVisibility(Visibility::Hidden);
            add(dotGlow);
            float sx = w - VALUE_RIGHT - SWITCH_W;
            toggleGlow = new RectangleShape(FloatRect(sx - 5, (h - SWITCH_H) / 2 - 5, SWITCH_W + 10, SWITCH_H + 10));
            // the row on violet: a faint separator at rest, a purple edge + a light surface when focused
            sep = new RectangleShape(FloatRect(0, h - 1, w, 1));
            sep->setFillColor(SEP);
            add(sep);
            focusEdge = new RectangleShape(FloatRect(0, std::round(h * 0.22f), 4, h - 2 * std::round(h * 0.22f)));
            focusEdge->setFillColor(PILL);
            focusEdge->setCornersRadius(2);
            focusEdge->setCornerPointCount(4);
            focusEdge->setVisibility(Visibility::Hidden);
            add(focusEdge);
            toggleGlow->setFillColor(TOGGLE_ON_GLOW);
            toggleGlow->setCornersRadius((SWITCH_H + 10) / 2);
            toggleGlow->setCornerPointCount(8);
            toggleGlow->setVisibility(Visibility::Hidden);
            add(toggleGlow);
            track = ui::box(this, FloatRect(sx, (h - SWITCH_H) / 2, SWITCH_W, SWITCH_H), TOGGLE_OFF, SWITCH_H / 2);
            knob = new CircleShape(SWITCH_H / 2 - 4);
            knob->setPointCount(24);
            knob->setFillColor(theme::textStrong());
            track->add(knob);
            // palette cells: one per row, stacked (each as tall/wide as the row allows)
            const float cw = std::floor((w - (L::PALETTE_COLUMNS - 1) * CELL_GAP) / L::PALETTE_COLUMNS);
            for (int i = 0; i < L::PALETTE_COLUMNS; i++) {
                PaletteCell c;
                c.bg = ui::box(this, FloatRect((float) i * (cw + CELL_GAP), 6, cw, h - 12), theme::none(), theme::RADIUS);
                c.swatch = new CircleShape(SWATCH_R);
                c.swatch->setPointCount(24);
                c.swatch->setPosition(20, (h - 12) / 2 - SWATCH_R);
                c.swatch->setOutlineColor(theme::textStrong());
                c.bg->add(c.swatch);
                c.name = ui::label(c.bg, "", theme::LABEL + 2, 20 + 2 * SWATCH_R + 16,
                                   ui::Label::centerOffset(theme::LABEL + 2, h - 12));
                c.name->setMaxWidth(cw - (20 + 2 * SWATCH_R + 16) - 44);
                c.check = ui::label(c.bg, "\xE2\x9C\x93", theme::BODY, 0, ui::Label::centerOffset(theme::BODY, h - 12),
                                    ui::Weight::SemiBold, theme::accentText());
                c.check->setAlign(ui::Align::Right, cw - 18);
                cells.push_back(c);
            }
        }

        // column: the focused palette cell (Palette rows)
        void bind(const SettingItem &item, bool focused, int column) {
            float w = getSize().x;
            float h = getSize().y;
            bool isPalette = item.kind == Kind::Palette;
            bool isToggle = item.kind == Kind::Toggle;
            setDivider(false);   // the row draws its own separator (the theme one is invisible on the glow)
            setFillColor(focused && !isPalette ? theme::withAlpha(theme::textStrong(), 18) : theme::none());
            focusEdge->setVisibility(focused && !isPalette ? Visibility::Visible : Visibility::Hidden);
            sep->setVisibility(focused || isPalette ? Visibility::Hidden : Visibility::Visible);
            caption->setVisibility(isPalette ? Visibility::Hidden : Visibility::Visible);
            caption->setText(item.caption);
            caption->setColor(ROW_TEXT);
            track->setVisibility(isToggle ? Visibility::Visible : Visibility::Hidden);
            toggleGlow->setVisibility(Visibility::Hidden);
            value->setVisibility(isToggle || isPalette ? Visibility::Hidden : Visibility::Visible);
            for (auto *d: dots) {
                d->setVisibility(Visibility::Hidden);
            }
            dotGlow->setVisibility(Visibility::Hidden);
            for (size_t i = 0; i < cells.size(); i++) {
                PaletteCell &c = cells[i];
                bool shown = isPalette && i < item.swatches.size();
                c.bg->setVisibility(shown ? Visibility::Visible : Visibility::Hidden);
                if (!shown) {
                    continue;
                }
                int preset = item.swatches[i];
                bool current = preset == theme::accentIndex();
                bool cellFocused = focused && (int) i == column;
                c.bg->setFillColor(cellFocused ? theme::focus() : theme::none());
                c.bg->setOutlineColor(theme::accent());
                c.bg->setOutlineThickness(cellFocused ? theme::ROW_FOCUS_BORDER : 0);
                // the preset's own colour, a white ring and a check mark on the one in use
                c.swatch->setFillColor(theme::swatch(preset));
                c.swatch->setOutlineThickness(current ? 3 : 0);
                c.name->setText(tr(palette::ACCENTS[preset].nameKey));
                c.name->setWeight(current ? ui::Weight::SemiBold : ui::Weight::Regular);
                c.name->setColor(cellFocused ? theme::textStrong() : current ? theme::textPrimary() : theme::textSecondary());
                c.check->setVisibility(current ? Visibility::Visible : Visibility::Hidden);
            }
            if (isPalette) {
                return;
            }
            if (isToggle) {
                bool on = item.toggle();
                track->setFillColor(on ? TOGGLE_ON : TOGGLE_OFF);
                toggleGlow->setVisibility(on ? Visibility::Visible : Visibility::Hidden);
                float r = SWITCH_H / 2 - 4;
                knob->setPosition(on ? SWITCH_W - 4 - 2 * r : 4, 4);
                return;
            }
            const unsigned vs = theme::LABEL + 2;
            if (item.kind == Kind::Choice) {
                int current = item.choice();
                // position dots only for short choices (language lists are long)
                int n = (int) item.options.size() <= (int) dots.size() ? (int) item.options.size() : 0;
                value->setText(item.options[(size_t) current]);
                value->setColor(ROW_TEXT);
                value->setPosition(0, ui::Label::centerOffset(vs, h) - (n > 0 ? 11 : 0));
                // option position: small bars under the value, right-aligned with it (the current one stretches)
                float total = 0;
                for (int i = 0; i < n; i++) {
                    total += (i == current ? 22.0f : 10.0f) + (i + 1 < n ? 7.0f : 0.0f);
                }
                float x = w - VALUE_RIGHT - total;
                for (int i = 0; i < n; i++) {
                    RectangleShape *d = dots[(size_t) i];
                    float bw = i == current ? 22.0f : 10.0f;
                    d->setSize(bw, 5);
                    d->setPosition(x, h - 20);
                    d->setFillColor(i == current ? DOT_ON : DOT_OFF);
                    d->setVisibility(Visibility::Visible);
                    if (i == current) {
                        dotGlow->setSize(30, 11);
                        dotGlow->setPosition(x - 4, h - 20 - 3);
                        dotGlow->setVisibility(Visibility::Visible);
                    }
                    x += bw + 7;
                }
                return;
            }
            value->setText(item.info ? item.info() : "");
            value->setColor(focused ? ROW_TEXT : MUTED_TEXT);
            value->setPosition(0, ui::Label::centerOffset(vs, h));
        }

    private:
        ui::Label *caption;
        ui::Label *value;
        std::vector<RectangleShape *> dots;
        RectangleShape *dotGlow;
        RectangleShape *toggleGlow;
        RectangleShape *sep;
        RectangleShape *focusEdge;
        RectangleShape *track;
        CircleShape *knob;
        std::vector<PaletteCell> cells;
    };

    class SettingsScreen : public Screen, public ui::ListView::Adapter {
    public:
        SettingsScreen(App &a, SettingsPage p) : Screen(a), categoriesAdapter(this) {
            // v2 (mock): the purple radial glow from the reference artwork, like the onboarding
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
            // the footer band of the reference: a bordered, darkened strip (the hints sit on it)
            auto *band = new RectangleShape(FloatRect(0, layout::HINTS_Y - 14, theme::SCREEN_W,
                                                      theme::SCREEN_H - (layout::HINTS_Y - 14)));
            band->setFillColor(theme::withAlpha(theme::background(), 38));
            add(band);
            auto *bandLine = new RectangleShape(FloatRect(0, layout::HINTS_Y - 14, theme::SCREEN_W, 1));
            bandLine->setFillColor(theme::withAlpha(theme::textStrong(), 18));
            add(bandLine);
            tagline = ui::label(this, tr("settings.footer_tagline") + "  v" + APP_VERSION, 17, 0, layout::HINTS_Y + 8,
                                ui::Weight::Regular, MUTED_TEXT);
            tagline->setAlign(ui::Align::Right, layout::SAFE_RIGHT);
            buildCategories();
            switch (p) {
                default:
                    catIndex = CAT_APPEARANCE;
                    break;
            }
            categoryList = new ui::ListView(FloatRect(layout::RAIL_X, L::RAIL_Y, layout::RAIL_W,
                                                      layout::CONTENT_BOTTOM - L::RAIL_Y), L::CAT_ROW_H,
                                            layout::RAIL_ROW_GAP, &categoriesAdapter);
            add(categoryList);
            // the open category as the big heading (the mock), with a save-status pill at the right edge
            categoryTitle = ui::label(this, "", theme::TITLE, L::LIST_X, 60, ui::Weight::SemiBold, ROW_TEXT);
            categoryTitle->setMaxWidth(L::LIST_W - 210);
            statusPill = new RectangleShape(FloatRect(L::LIST_X + L::LIST_W - 178, 68, 178, 36));
            statusPill->setFillColor(STATUS_BG);
            statusPill->setCornersRadius(18);
            statusPill->setCornerPointCount(10);
            statusPill->setVisibility(Visibility::Hidden);   // shown only while saving / saved / error
            add(statusPill);
            statusText = ui::label(statusPill, tr("settings.status_saved"), 19, 0,
                                   ui::Label::centerOffset(19, 36), ui::Weight::SemiBold, STATUS_TEXT);
            statusText->setAlign(ui::Align::Center, 178);
            list = new ui::ListView(FloatRect(L::LIST_X, L::LIST_Y, L::LIST_W, L::LIST_H), L::ROW_H, 0, this);
            add(list);
            // the description panel (the mock): a purple-tinted rounded panel that hugs its content
            panelBox = ui::box(this, FloatRect(L::PANEL_X, 60, L::PANEL_W, 120), PANEL_BG, theme::RADIUS_LARGE);
            panelBox->setOutlineColor(PANEL_EDGE);
            panelBox->setOutlineThickness(1.5f);
            panelTitle = ui::label(this, "", 26, L::PANEL_X + PANEL_PAD, 0, ui::Weight::SemiBold, ROW_TEXT);
            panelTitle->setMaxWidth(L::PANEL_W - 2 * PANEL_PAD);
            panelTitle->setMaxLines(2);
            panelText = ui::label(this, "", theme::LABEL, L::PANEL_X + PANEL_PAD, 0, ui::Weight::Regular,
                                  DESC_TEXT);
            panelText->setMaxWidth(L::PANEL_W - 2 * PANEL_PAD);
            panelText->setMaxLines(12);
            panelDivider = new RectangleShape(FloatRect(L::PANEL_X + PANEL_PAD, 0, L::PANEL_W - 2 * PANEL_PAD, 1));
            panelDivider->setFillColor(theme::withAlpha(theme::textStrong(), 36));
            panelDivider->setVisibility(Visibility::Hidden);
            add(panelDivider);
            summaryTitle = ui::label(this, tr("settings.summary.title"), 18, L::PANEL_X + PANEL_PAD, 0,
                                     ui::Weight::SemiBold, MUTED_TEXT);
            summaryTitle->setVisibility(Visibility::Hidden);
            for (int i = 0; i < 3; i++) {
                auto *k = ui::label(this, "", theme::LABEL, L::PANEL_X + PANEL_PAD, 0, ui::Weight::Regular, DESC_TEXT);
                auto *v = ui::label(this, "", theme::LABEL, 0, 0, ui::Weight::SemiBold, ROW_TEXT);
                k->setVisibility(Visibility::Hidden);
                v->setVisibility(Visibility::Hidden);
                summaryKeys[i] = k;
                summaryValues[i] = v;
            }
            hints = screens::hintBar(this, {});
            categoryList->setSelected(catIndex);
            if (p == SettingsPage::Appearance) {
                // back after an accent change: on the preset that was just applied
                list->setSelected(theme::accentIndex() / L::PALETTE_COLUMNS);
                paletteColumn = theme::accentIndex() % L::PALETTE_COLUMNS;
                setFocus(1);
            } else {
                list->setSelected(0);
                setFocus(p == SettingsPage::Main ? 0 : 1);
            }
            selectCategory(catIndex, false);
        }

        void onResume() override {
            list->reload();
            refreshPanel();
        }

        void onInputDeviceChanged() override {
            refreshPanel();   // descriptions may name a key
        }

        const char *name() const override { return "settings"; }

        void tick(double now) override {
            // the "Saved ✓" pill hides itself after a couple of seconds
            if (statusKind == 2) {
                if (savedAt < 0) {
                    savedAt = now;
                } else if (now - savedAt > 2.5) {
                    setStatus(0);
                }
            }
        }

        // ------------------------------------------------------------------ settings list (middle column)
        int count() override { return (int) items().size(); }

        C2DObject *createRow(float w, float h) override {
            return new SettingRow(w, h);
        }

        void bindRow(C2DObject *obj, int index, bool, bool focused) override {
            ((SettingRow *) obj)->bind(items()[(size_t) index], focused, paletteColumn);
        }

        void handleInput(const InputEvent &e) override {
            if (focus == 0) {
                categoryInput(e);
                return;
            }
            SettingItem &item = items()[(size_t) list->selected()];
            switch (e.button) {
                case PadButton::Up:
                    list->moveSelection(-1);
                    refreshPanel();
                    return;
                case PadButton::Down:
                    list->moveSelection(1);
                    refreshPanel();
                    return;
                case PadButton::Left:
                case PadButton::Right:
                    if (item.kind == Kind::Palette) {
                        // inside the palette; Left on its first column goes back to the categories
                        int col = paletteColumn + (e.button == PadButton::Right ? 1 : -1);
                        if (col < 0) {
                            if (!e.repeat) {
                                setFocus(0);
                            }
                        } else if (col < (int) item.swatches.size()) {
                            paletteColumn = col;
                            list->reload();
                            refreshPanel();
                        }
                        return;
                    }
                    // the D-pad only navigates (ui/option_input.h): Left goes back to the categories, Right does nothing
                    if (ui::optionCommand(e, optionKind(item)) == ui::OptionCommand::Back) {
                        setFocus(0);
                    }
                    return;
                case PadButton::L1:
                case PadButton::R1:
                    if (!e.repeat) {
                        stepCategory(e.button == PadButton::L1 ? -1 : 1);
                    }
                    return;
                case PadButton::Cross:
                    // a value changes only here: 3+ values in the shared picker, On / Off and 2 values in place
                    switch (ui::optionCommand(e, optionKind(item), (int) item.options.size())) {
                        case ui::OptionCommand::OpenPicker:
                            openPicker(list->selected());
                            break;
                        case ui::OptionCommand::Toggle:
                        case ui::OptionCommand::Cycle:
                        case ui::OptionCommand::Activate:
                            change(item, 1, true);
                            break;
                        default:
                            break;
                    }
                    return;
                case PadButton::Circle:
                    if (!e.repeat) {
                        setFocus(0);
                    }
                    return;
                default:
                    return;
            }
        }

    private:
        enum CategoryId {
            CAT_APPEARANCE,
            CAT_PLAYBACK,
            CAT_AUDIO,
            CAT_VIDEO,
            CAT_LANGUAGE,
            CAT_ABOUT
        };

        // ------------------------------------------------------------------ categories (left column)
        struct CategoriesAdapter : ui::ListView::Adapter {
            explicit CategoriesAdapter(SettingsScreen *s) : screen(s) {}

            int count() override { return (int) screen->cats.size(); }

            C2DObject *createRow(float w, float h) override {
                Row r;
                r.bg = new RectangleShape(FloatRect(0, 0, w, h));
                r.bg->setFillColor(theme::none());
                r.bg->setCornersRadius(10);
                r.bg->setCornerPointCount(6);
                r.name = ui::label(r.bg, "", theme::LABEL, 20, ui::Label::centerOffset(theme::LABEL, h),
                                   ui::Weight::Regular, NAV_IDLE);
                r.name->setMaxWidth(w - 20 - 40);
                r.chev1 = new RectangleShape(FloatRect(0, 0, 9, 2.6f));
                r.chev1->setFillColor(NAV_ACTIVE);
                r.chev1->setCornersRadius(1.3f);
                r.chev1->setOrigin(Origin::Center);
                r.bg->add(r.chev1);
                r.chev2 = new RectangleShape(FloatRect(0, 0, 9, 2.6f));
                r.chev2->setFillColor(NAV_ACTIVE);
                r.chev2->setCornersRadius(1.3f);
                r.chev2->setOrigin(Origin::Center);
                r.bg->add(r.chev2);
                rows.push_back(r);
                return r.bg;
            }

            void bindRow(C2DObject *obj, int index, bool selected, bool focused) override {
                bool railFocused = screen->focus == 0;
                for (auto &r: rows) {
                    if (r.bg != obj) {
                        continue;
                    }
                    Row &row = r;
                    float h = row.bg->getSize().y;
                    bool active = selected;
                    // the open category keeps the purple pill (the mock); focused on the rail: a white ring too
                    row.bg->setFillColor(active ? PILL : theme::none());
                    row.bg->setOutlineColor(active && railFocused ? PILL_RING : theme::none());
                    row.bg->setOutlineThickness(active && railFocused ? 2.0f : 0.0f);
                    row.name->setText(screen->cats[(size_t) index].name);
                    row.name->setWeight(active ? ui::Weight::SemiBold : ui::Weight::Regular);
                    row.name->setColor(active ? NAV_ACTIVE : NAV_IDLE);
                    // a chevron on the open row only (two bars meeting on the right)
                    float cx = row.bg->getSize().x - 24, cy = h / 2;
                    row.chev1->setPosition(cx - 2, cy - 3);
                    row.chev1->setRotation(45);
                    row.chev2->setPosition(cx - 2, cy + 3);
                    row.chev2->setRotation(-45);
                    row.chev1->setVisibility(active ? Visibility::Visible : Visibility::Hidden);
                    row.chev2->setVisibility(active ? Visibility::Visible : Visibility::Hidden);
                }
            }

            struct Row {
                RectangleShape *bg;
                ui::Label *name;
                RectangleShape *chev1;
                RectangleShape *chev2;
            };
            SettingsScreen *screen;
            std::vector<Row> rows;
        };

        void categoryInput(const InputEvent &e) {
            switch (e.button) {
                case PadButton::Up:
                case PadButton::Down:
                    if (categoryList->moveSelection(e.button == PadButton::Up ? -1 : 1)) {
                        selectCategory(categoryList->selected(), true);
                    }
                    return;
                case PadButton::L1:
                case PadButton::R1:
                    if (!e.repeat) {
                        stepCategory(e.button == PadButton::L1 ? -1 : 1);
                    }
                    return;
                case PadButton::Right:
                case PadButton::Cross:
                    if (!e.repeat && !items().empty()) {
                        setFocus(1);
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
        }

        void stepCategory(int d) {
            int target = std::min(std::max(catIndex + d, 0), (int) cats.size() - 1);
            if (target != catIndex) {
                categoryList->setSelected(target);
                selectCategory(target, true);
            }
        }

        void selectCategory(int index, bool resetSelection) {
            catIndex = index;
            if (resetSelection) {
                list->setSelected(0);
            }
            list->reload();
            categoryTitle->setText(cats[(size_t) catIndex].name);
            refreshPanel();
        }

        void setStatus(int kind) {   // 0 idle (pill hidden), 1 saving, 2 saved, 3 error
            statusKind = kind;
            statusPill->setVisibility(kind == 0 ? Visibility::Hidden : Visibility::Visible);
            if (kind == 1) {
                statusText->setText(tr("settings.status_saving"));
                statusPill->setFillColor(STATUS_BG);
                statusText->setColor(STATUS_TEXT);
            } else if (kind == 2) {
                savedAt = -1;
                statusText->setText(tr("settings.status_saved"));
                statusPill->setFillColor(STATUS_BG);
                statusText->setColor(STATUS_TEXT);
            } else if (kind == 3) {
                statusText->setText(tr("settings.status_error"));
                statusPill->setFillColor(STATUS_ERR_BG);
                statusText->setColor(STATUS_ERR_TEXT);
            }
        }

        void setFocus(int f) {
            focus = f;
            categoryList->setFocused(focus == 0);
            list->setFocused(focus == 1);
            categoryList->reload();   // the other categories step back while the settings have focus
            if (focus == 0) {
                hints->setHints({{A::Confirm, tr("common.open")},
                                 {A::Tabs, tr("live.category")}, {A::Back, tr("common.back")}});
            } else {
                bool swatches = catIndex == CAT_APPEARANCE;
                // the D-pad only navigates: Cross changes a value (Change is shown only for the palette's swatches)
                screens::Hints h = {{A::Confirm, tr(swatches ? "settings.hint_apply" : "common.select")}};
                if (swatches) {
                    h.push_back({A::Change, tr("settings.hint_change")});
                }
                h.push_back({A::Tabs, tr("live.category")});
                h.push_back({A::Back, tr("settings.hint_categories")});
                hints->setHints(h);
            }
            refreshPanel();
        }

        bool playbackRebuild = false;   // Auto-play changed: Playback's rows are built again after the toggle

        std::vector<SettingItem> &items() { return cats[(size_t) catIndex].items; }

        // Playback's rows depend on Auto-play (the card timing row): built again, the selection kept on its row
        void rebuildPlayback() {
            playbackRebuild = false;
            int at = list->selected();
            cats[CAT_PLAYBACK].items.clear();
            buildPlayback(cats[CAT_PLAYBACK].items);
            if (catIndex == CAT_PLAYBACK) {
                list->reload();
                list->setSelected(std::min(at, (int) items().size() - 1));
                refreshPanel();
            }
        }

        static ui::OptionKind optionKind(const SettingItem &item) {
            return item.kind == Kind::Toggle ? ui::OptionKind::Toggle : item.kind == Kind::Choice ? ui::OptionKind::Choice
                                                                                                 : ui::OptionKind::Action;
        }

        // the shared choice picker for a Choice row; the value applies when it closes (the row shows it at once)
        void openPicker(int row) {
            const SettingItem &item = items()[(size_t) row];
            int category = catIndex;
            app.push(screens::makeChoicePicker(app, item.caption, item.options, item.choice(), whileAlive([this, category, row](int c) {
                if (c < 0 || category != catIndex || row >= (int) items().size()) {
                    return;   // cancelled (or the page changed meanwhile)
                }
                SettingItem &it = items()[(size_t) row];
                if (it.kind != Kind::Choice || c == it.choice()) {
                    return;
                }
                it.setChoice(c);   // may rebuild every screen (language): nothing of this screen is used after it
            })));
        }

        void change(SettingItem &item, int delta, bool activate) {
            if (item.kind == Kind::Choice) {
                int n = (int) item.options.size();
                int next = item.choice() + delta;
                if (activate) {
                    next = (next + n) % n;      // X cycles
                } else if (next < 0 || next >= n) {
                    return;                     // Left/Right stop at the ends
                }
                item.setChoice(next);
            } else if (item.kind == Kind::Toggle) {
                bool on = item.toggle();
                bool want = activate ? !on : delta > 0;
                if (want == on) {
                    return;
                }
                item.setToggle(want);
                if (playbackRebuild) {
                    rebuildPlayback();   // `item` belongs to the old list: nothing of it is used after this
                    return;
                }
            } else if (activate && item.kind == Kind::Palette) {
                if (paletteColumn < (int) item.swatches.size()) {
                    applyAccent(item.swatches[(size_t) paletteColumn]);
                }
                return;   // the screens are rebuilt with the new accent: this screen is on its way out
            } else if (activate && item.run) {
                item.run();
                return;   // an action may have rebuilt the screens (accent): this screen may be on its way out
            } else {
                return;
            }
            list->reload();
            refreshPanel();
        }

        // saves the preset and rebuilds every screen with it (this one included: nothing of it is used after)
        void applyAccent(int preset) {
            if (preset == theme::accentIndex()) {
                return;
            }
            app.settings().get().accentColor = palette::ACCENTS[preset].key;
            save();
            App &a = app;
            std::string name = tr(palette::ACCENTS[preset].nameKey);
            a.applyAccent(true);   // back here, on Appearance, with the new accent
            a.toast(tr("appearance.accent.applied", {name}), ToastKind::Success);
        }

        // the aside panel (the mock): title, description and — on the Playback page — the current summary;
        // the panel hugs its content (resized on every refresh)
        void refreshPanel() {
            if (items().empty()) {
                return;
            }
            const SettingItem &item = focus == 0 ? items()[0] : items()[(size_t) list->selected()];
            panelTitle->setText(focus == 0 ? cats[(size_t) catIndex].name : item.caption);
            std::string text = focus == 0 ? categoryDesc(catIndex) : item.describe ? item.describe() : "";
            panelText->setText(text);
            const float pad = PANEL_PAD;
            const float x = L::PANEL_X + pad;
            float y = 60 + pad;
            panelTitle->setPosition(x, y);
            float cy = y + panelTitle->height() + 12;
            panelText->setPosition(x, cy);
            float end = cy + panelText->height();
            const bool playback = catIndex == CAT_PLAYBACK && focus == 0;
            panelDivider->setVisibility(playback ? Visibility::Visible : Visibility::Hidden);
            summaryTitle->setVisibility(playback ? Visibility::Visible : Visibility::Hidden);
            for (int i = 0; i < 3; i++) {
                summaryKeys[i]->setVisibility(playback ? Visibility::Visible : Visibility::Hidden);
                summaryValues[i]->setVisibility(playback ? Visibility::Visible : Visibility::Hidden);
            }
            if (playback) {
                const Settings &s = app.settings().get();
                const char *formatKey = s.streamFormat == StreamFormat::Ts ? "settings.stream_format.ts"
                                        : s.streamFormat == StreamFormat::Hls ? "settings.stream_format.hls"
                                                                              : "settings.stream_format.auto";
                const char *stabilityKey = s.stability == StabilityPreset::Fast ? "stability.fast"
                                           : s.stability == StabilityPreset::MaxStability ? "stability.max"
                                                                                         : "stability.balanced";
                const std::string values[3] = {tr(formatKey), tr(stabilityKey),
                                               tr(s.autoPlayNextEpisode ? "settings.summary.auto" : "settings.summary.manual")};
                const char *keys[3] = {"settings.summary.format", "settings.summary.stability",
                                       "settings.summary.next_episode"};
                float dy = end + 18;
                panelDivider->setPosition(L::PANEL_X + pad, dy);
                dy += 14;
                summaryTitle->setPosition(x, dy);
                dy += summaryTitle->height() + 12;
                for (int i = 0; i < 3; i++) {
                    // the label in the quiet colour, its value semibold right after it
                    std::string k = tr(keys[i], {""});
                    size_t at = k.find("{0}");
                    std::string label = at == std::string::npos ? k : k.substr(0, at);
                    summaryKeys[i]->setText(label);
                    summaryKeys[i]->setPosition(x, dy);
                    float lx = x + summaryKeys[i]->width() + 4;
                    summaryValues[i]->setText(values[i]);
                    summaryValues[i]->setPosition(lx, dy);
                    dy += summaryKeys[i]->height() + 8;
                }
                end = dy + 4;
            }
            panelBox->setSize(L::PANEL_W, std::round(end + pad - 60));
        }

        // the aside's category descriptions (the mock); the focused item's own text replaces them on the rows
        std::string categoryDesc(int index) const {
            switch (index) {
                case CAT_APPEARANCE: return tr("settings.cat.appearance.desc");
                case CAT_PLAYBACK: return tr("settings.cat.playback.desc");
                case CAT_AUDIO: return tr("settings.cat.audio_subtitles.desc");
                case CAT_VIDEO: return tr("settings.cat.video.desc");
                case CAT_LANGUAGE: return tr("settings.cat.language.desc");
                case CAT_ABOUT: return tr("settings.about.desc");
                default: return "";
            }
        }

        void save() {
            setStatus(1);   // Saving…
            std::string err;
            if (!app.settings().save(&err)) {
                LOG_E("settings", "save failed: %s", err.c_str());
                setStatus(3);   // Not saved
                app.toast(tr("settings.save_failed"), ToastKind::Error);
            } else {
                setStatus(2);   // Saved ✓ — reverts to Pronto after a while (tick)
                savedAt = -1;   // armed: the first tick with it stores the time
            }
        }

        // "" (Auto) + common languages + languages met in played files + the saved choices
        std::vector<std::string> languageCodes() const {
            std::vector<std::string> codes = {""};
            auto addCode = [&codes](const std::string &c) {
                std::string n = tracks::normalizeLanguage(c);
                if (!n.empty() && std::find(codes.begin(), codes.end(), n) == codes.end()) {
                    codes.push_back(n);
                }
            };
            for (const auto &c: tracks::commonLanguages()) {
                addCode(c);
            }
            for (const auto &c: app.session().seenLanguages) {
                addCode(c);
            }
            addCode(app.settings().get().audioLanguage);
            addCode(app.settings().get().subtitleLanguage);
            return codes;
        }

        // every category, in CategoryId order; `items` is filled by each builder
        void buildCategories() {
            const char *names[] = {"appearance.title", "settings.cat.playback", "settings.cat.audio_subtitles",
                                   "settings.cat.video", "settings.language",
                                   "settings.cat.about"};
            for (const char *n: names) {
                cats.push_back({tr(n), {}});
            }
            buildAppearance(cats[CAT_APPEARANCE].items);
            buildPlayback(cats[CAT_PLAYBACK].items);
            buildAudio(cats[CAT_AUDIO].items);
            buildVideo(cats[CAT_VIDEO].items);
            buildLanguage(cats[CAT_LANGUAGE].items);
            buildAbout(cats[CAT_ABOUT].items);
        }

        // ------------------------------------------------------------------ Appearance
        void buildAppearance(std::vector<SettingItem> &items) {
            Settings &s = app.settings().get();
            (void) s;
            // the presets as rows of three: ZIM Blue Cyan Teal / Green Purple Magenta / Red Orange White
            for (int i = 0; i < palette::ACCENT_COUNT; i += L::PALETTE_COLUMNS) {
                SettingItem row;
                row.kind = Kind::Palette;
                row.caption = tr("appearance.accent");
                for (int j = i; j < std::min(palette::ACCENT_COUNT, i + L::PALETTE_COLUMNS); j++) {
                    row.swatches.push_back(j);
                }
                row.describe = [] {
                    return tr("appearance.accent.desc") + "\n\n"
                           + tr("appearance.accent.current", {tr(theme::accentPreset().nameKey),
                                                               screens::keyName(A::Confirm)});
                };
                items.push_back(row);
            }
        }

        // ------------------------------------------------------------------ Playback
        void buildPlayback(std::vector<SettingItem> &items) {
            Settings &s = app.settings().get();
            SettingItem format;
            format.kind = Kind::Choice;
            format.caption = tr("settings.stream_format");
            format.options = {tr("settings.stream_format.auto"), tr("settings.stream_format.ts"),
                              tr("settings.stream_format.hls")};
            format.choice = [&s] { return s.streamFormat == StreamFormat::Ts ? 1 : s.streamFormat == StreamFormat::Hls ? 2 : 0; };
            format.setChoice = [this, &s](int i) {
                s.streamFormat = i == 1 ? StreamFormat::Ts : i == 2 ? StreamFormat::Hls : StreamFormat::Auto;
                save();
            };
            format.describe = [&s] {
                return tr(s.streamFormat == StreamFormat::Ts ? "settings.stream_format.ts_desc"
                          : s.streamFormat == StreamFormat::Hls ? "settings.stream_format.hls_desc"
                                                                : "settings.stream_format.auto_desc");
            };
            items.push_back(format);

            SettingItem stab;
            stab.kind = Kind::Choice;
            stab.caption = tr("settings.stability");
            stab.options = {tr("stability.fast"), tr("stability.balanced"), tr("stability.max")};
            stab.choice = [&s] { return s.stability == StabilityPreset::Fast ? 0 : s.stability == StabilityPreset::MaxStability ? 2 : 1; };
            stab.setChoice = [this, &s](int i) {
                s.stability = i == 0 ? StabilityPreset::Fast : i == 2 ? StabilityPreset::MaxStability
                                                                      : StabilityPreset::Balanced;
                save();
            };
            stab.describe = [&s] {
                return tr(s.stability == StabilityPreset::Fast ? "settings.stability.fast_desc"
                          : s.stability == StabilityPreset::MaxStability ? "settings.stability.max_desc"
                                                                         : "settings.stability.balanced_desc");
            };
            items.push_back(stab);

            SettingItem retry;
            retry.kind = Kind::Toggle;
            retry.caption = tr("settings.retry_stall");
            retry.toggle = [&s] { return s.retryOnStall; };
            retry.setToggle = [this, &s](bool on) {
                s.retryOnStall = on;
                save();
            };
            retry.describe = [&s] { return tr(s.retryOnStall ? "settings.retry_stall.on_desc" : "settings.retry_stall.off_desc"); };
            items.push_back(retry);

            SettingItem resume;
            resume.kind = Kind::Toggle;
            resume.caption = tr("settings.resume");
            resume.toggle = [&s] { return s.resumeVod; };
            resume.setToggle = [this, &s](bool on) {
                s.resumeVod = on;
                save();
            };
            resume.describe = [] { return tr("settings.resume.desc"); };
            items.push_back(resume);

            SettingItem autoNext;
            autoNext.kind = Kind::Toggle;
            autoNext.caption = tr("settings.autoplay");
            autoNext.toggle = [&s] { return s.autoPlayNextEpisode; };
            autoNext.setToggle = [this, &s](bool on) {
                s.autoPlayNextEpisode = on;
                save();
                playbackRebuild = true;   // the card timing row appears / disappears (after this call: change())
            };
            autoNext.describe = [] { return tr("settings.autoplay.desc"); };
            items.push_back(autoNext);

            // when the Next Episode card appears (only with Auto-play on); its 7 s countdown never changes
            if (s.autoPlayNextEpisode) {
                SettingItem cardTime;
                cardTime.kind = Kind::Choice;
                cardTime.caption = tr("settings.next_card_time");
                for (int v: nextCardChoices()) {
                    cardTime.options.push_back(i18n::count("settings.next_card_seconds", (long long) v));
                }
                cardTime.choice = [&s] {
                    const std::vector<int> &c = nextCardChoices();
                    int v = clampNextCardSeconds(s.nextCardSeconds);
                    for (size_t i = 0; i < c.size(); i++) {
                        if (c[i] == v) {
                            return (int) i;
                        }
                    }
                    return 0;
                };
                cardTime.setChoice = [this, &s](int i) {
                    const std::vector<int> &c = nextCardChoices();
                    s.nextCardSeconds = c[(size_t) std::min(std::max(i, 0), (int) c.size() - 1)];
                    save();
                };
                cardTime.describe = [] { return tr("settings.next_card_time.desc"); };
                items.push_back(cardTime);
            }

        }

        // ------------------------------------------------------------------ Audio & subtitles (movies / episodes)
        void buildAudio(std::vector<SettingItem> &items) {
            Settings &s = app.settings().get();
            std::vector<std::string> codes = languageCodes();
            std::vector<std::string> names;
            for (const auto &c: codes) {
                names.push_back(c.empty() ? "" : tracks::languageName(c));
            }
            auto indexOfCode = [codes](const std::string &code) {
                for (size_t i = 0; i < codes.size(); i++) {
                    if (codes[i] == code) {
                        return (int) i;
                    }
                }
                return 0;
            };

            SettingItem audio;
            audio.kind = Kind::Choice;
            audio.caption = tr("settings.audio_language");
            audio.options = names;
            audio.options[0] = tr("settings.audio_language.auto");
            audio.choice = [&s, indexOfCode] { return indexOfCode(s.audioLanguage); };
            audio.setChoice = [this, &s, codes](int i) {
                s.audioLanguage = codes[(size_t) i];
                save();
            };
            audio.describe = [] { return tr("settings.audio_language.desc"); };
            items.push_back(audio);

            SettingItem subMode;
            subMode.kind = Kind::Choice;
            subMode.caption = tr("settings.subtitles");
            subMode.options = {tr("common.off"), tr("settings.subtitles.auto"), tr("settings.subtitles.always")};
            subMode.choice = [&s] { return (int) s.subtitleMode; };
            subMode.setChoice = [this, &s](int i) {
                s.subtitleMode = (tracks::SubtitleMode) i;
                save();
            };
            subMode.describe = [&s] {
                return tr(s.subtitleMode == tracks::SubtitleMode::Off ? "settings.subtitles.off_desc"
                          : s.subtitleMode == tracks::SubtitleMode::Always ? "settings.subtitles.always_desc"
                                                                           : "settings.subtitles.auto_desc");
            };
            items.push_back(subMode);

            SettingItem subLang;
            subLang.kind = Kind::Choice;
            subLang.caption = tr("settings.subtitle_language");
            subLang.options = names;
            subLang.options[0] = tr("settings.subtitle_language.same");
            subLang.choice = [&s, indexOfCode] { return indexOfCode(s.subtitleLanguage); };
            subLang.setChoice = [this, &s, codes](int i) {
                s.subtitleLanguage = codes[(size_t) i];
                save();
            };
            subLang.describe = [] { return tr("settings.subtitle_language.desc"); };
            items.push_back(subLang);

            SettingItem subSize;
            subSize.kind = Kind::Choice;
            subSize.caption = tr("subtitle.size");
            subSize.options = {tr("subtitle.size.small"), tr("subtitle.size.medium"), tr("subtitle.size.large")};
            subSize.choice = [&s] { return std::min(std::max(s.subtitleSize, 0), 2); };
            subSize.setChoice = [this, &s](int i) {
                s.subtitleSize = i;
                save();
            };
            subSize.describe = [] { return tr("settings.subtitle_size.desc"); };
            items.push_back(subSize);

            SettingItem subPos;
            subPos.kind = Kind::Choice;
            subPos.caption = tr("subtitle.position");
            subPos.options = {tr("subtitle.position.bottom"), tr("subtitle.position.raised")};
            subPos.choice = [&s] { return s.subtitlePosition == 1 ? 1 : 0; };
            subPos.setChoice = [this, &s](int i) {
                s.subtitlePosition = i;
                save();
            };
            subPos.describe = [] { return tr("settings.subtitle_position.desc"); };
            items.push_back(subPos);

            SettingItem subShadow;
            subShadow.kind = Kind::Toggle;
            subShadow.caption = tr("subtitle.shadow");
            subShadow.toggle = [&s] { return s.subtitleShadow; };
            subShadow.setToggle = [this, &s](bool on) {
                s.subtitleShadow = on;
                save();
            };
            subShadow.describe = [] { return tr("settings.subtitle_shadow.desc"); };
            items.push_back(subShadow);
        }

        // ------------------------------------------------------------------ Video (defaults; the Options panel changes only the current playback)
        void buildVideo(std::vector<SettingItem> &items) {
            Settings &s = app.settings().get();
            SettingItem aspect;
            aspect.kind = Kind::Choice;
            aspect.caption = tr("settings.aspect");
            for (int i = 0; i < display::ASPECT_COUNT; i++) {
                aspect.options.push_back(screens::aspectName((display::Aspect) i));
            }
            aspect.choice = [&s] { return (int) s.videoAspect; };
            aspect.setChoice = [this, &s](int i) {
                s.videoAspect = (display::Aspect) i;
                save();
            };
            aspect.describe = [&s] { return tr("settings.aspect.desc", {screens::aspectName(s.videoAspect)}); };
            items.push_back(aspect);

            SettingItem crop;
            crop.kind = Kind::Choice;
            crop.caption = tr("settings.crop");
            for (int i = 0; i < display::CROP_COUNT; i++) {
                crop.options.push_back(screens::cropName((display::Crop) i));
            }
            crop.choice = [&s] { return (int) s.videoCrop; };
            crop.setChoice = [this, &s](int i) {
                s.videoCrop = (display::Crop) i;
                save();
            };
            crop.describe = [&s] { return tr("settings.crop.desc", {screens::cropName(s.videoCrop)}); };
            items.push_back(crop);

            SettingItem zoom;
            zoom.kind = Kind::Choice;
            zoom.caption = tr("settings.zoom");
            for (int z: display::zoomSteps()) {
                zoom.options.push_back(screens::zoomName(z));
            }
            zoom.choice = [&s] {
                const auto &steps = display::zoomSteps();
                for (size_t i = 0; i < steps.size(); i++) {
                    if (steps[i] == display::clampZoom(s.zoomPercent)) {
                        return (int) i;
                    }
                }
                return 0;
            };
            zoom.setChoice = [this, &s](int i) {
                s.zoomPercent = display::zoomSteps()[(size_t) i];
                save();
            };
            zoom.describe = [] { return tr("settings.zoom.desc"); };
            items.push_back(zoom);
        }

        // ------------------------------------------------------------------ Downloads & storage
        // ------------------------------------------------------------------ Sources
        // 1) Trocar usuário: the mock-style circle switcher; selecting a user reconfigures that same user's
        //    source (Xtream / M3U / EPG) instead of going back to the home screen

        // ------------------------------------------------------------------ Language
        void buildLanguage(std::vector<SettingItem> &items) {
            Settings &s = app.settings().get();
            SettingItem language;
            language.kind = Kind::Choice;
            language.caption = tr("settings.language");
            // each language in its own name: whoever picked the wrong one can still find theirs
            static const i18n::Language ORDER[] = {i18n::Language::English, i18n::Language::Turkish,
                                                   i18n::Language::Portuguese, i18n::Language::Spanish,
                                                   i18n::Language::French, i18n::Language::Italian,
                                                   i18n::Language::German};
            static const char *CODES[] = {"en", "tr", "pt", "es", "fr", "it", "de"};
            language.options.clear();
            for (const i18n::Language l: ORDER) {
                language.options.push_back(i18n::nativeName(l));
            }
            language.choice = [&s] {
                for (size_t i = 0; i < std::size(CODES); i++) {
                    if (s.language == CODES[i]) {
                        return (int) i;
                    }
                }
                return 2;
            };
            language.setChoice = [this, &s](int i) {
                if (i < 0 || i >= (int) std::size(ORDER)) {
                    return;
                }
                std::string code = i18n::code(ORDER[i]);
                if (code == s.language) {
                    return;
                }
                s.language = code;
                save();
                app.applyLanguage(true);   // rebuilds every screen in the new language
            };
            language.describe = [] { return tr("settings.language.desc"); };
            items.push_back(language);

            // a physical USB / Bluetooth keyboard: which characters its keys type
            SettingItem layout;
            layout.kind = Kind::Choice;
            layout.caption = tr("settings.keyboard_layout");
            layout.options = {tr("settings.keyboard_layout.auto"), tr("settings.keyboard_layout.us"),
                              tr("settings.keyboard_layout.tr")};
            layout.choice = [&s] { return s.keyboardLayout == "us" ? 1 : s.keyboardLayout == "tr" ? 2 : 0; };   // i18n-exempt: technical value
            layout.setChoice = [this, &s](int i) {
                s.keyboardLayout = i == 1 ? "us" : i == 2 ? "tr" : "auto";   // i18n-exempt: technical value
                save();
                app.applyKeyboardLayout();
            };
            layout.describe = [] { return tr("settings.keyboard_layout.desc"); };
            items.push_back(layout);
        }

        // ------------------------------------------------------------------ About
        void buildAbout(std::vector<SettingItem> &items) {
            SettingItem about;
            about.caption = tr("about.title");
            about.info = [] { return std::string("v") + APP_VERSION; };
            about.run = [this] { app.push(screens::makeAbout(app)); };
            about.describe = [] { return tr("settings.about.desc"); };
            items.push_back(about);
        }

        CategoriesAdapter categoriesAdapter;
        std::vector<Category> cats;
        int catIndex = 0;
        int paletteColumn = 0;   // the focused cell of a palette row
        int focus = 0;
        c2d::C2DTexture *bgTex = nullptr;
        ui::ListView *categoryList = nullptr;
        ui::ListView *list = nullptr;
        ui::Label *categoryTitle = nullptr;
        ui::Label *tagline = nullptr;
        ui::Label *statusText = nullptr;
        RectangleShape *statusPill = nullptr;
        int statusKind = 0;        // 0 pronta, 1 salvando, 2 salva, 3 erro
        double savedAt = -1;
        ui::Label *panelTitle = nullptr;
        ui::Label *panelText = nullptr;
        RectangleShape *panelBox = nullptr;
        RectangleShape *panelDivider = nullptr;
        ui::Label *summaryTitle = nullptr;
        ui::Label *summaryKeys[3] = {nullptr, nullptr, nullptr};
        ui::Label *summaryValues[3] = {nullptr, nullptr, nullptr};
        ui::HintBar *hints = nullptr;
    };

    class AboutScreen : public Screen {
    public:
        explicit AboutScreen(App &a) : Screen(a) {
            ui::background(this);
            screens::header(this, tr("settings.cat.about"));
            auto *name = ui::label(this, "ZIMTV IPTV", theme::DISPLAY, 0, 300, ui::Weight::SemiBold,
                                   theme::textStrong());
            name->setAlign(ui::Align::Center, theme::SCREEN_W);
            auto *version = ui::label(this, std::string("v") + APP_VERSION, theme::TITLE, 0, 470,
                                      ui::Weight::Regular, theme::accentText());
            version->setAlign(ui::Align::Center, theme::SCREEN_W);
            screens::hintBar(this, {{A::Back, tr("common.back")}});
        }

        const char *name() const override { return "about"; }

        void handleInput(const InputEvent &e) override {
            if (e.button == PadButton::Circle || e.button == PadButton::Cross) {
                app.pop();
            }
        }
    };
}

namespace screens {
    Screen *makeSettings(App &app, SettingsPage page) {
        return new SettingsScreen(app, page);
    }

    Screen *makeAbout(App &app) {
        return new AboutScreen(app);
    }
}
