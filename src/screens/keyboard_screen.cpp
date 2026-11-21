// Text input: PS4 native On-Screen Keyboard (ImeDialog) on console, custom keyboard on host builds.

#include "common.h"
#include "../platform/log.h"

using namespace c2d;

namespace {

    class DialogScreen : public Screen {

    public:

        DialogScreen(App &a, const std::string &title, const std::string &message,
                     const std::vector<std::string> &buttons, std::function<void(int)> choice, bool destructive)
                : Screen(a, true), onChoice(std::move(choice)) {
            const float w = 980;
            auto *msg = new ui::Label(message, theme::BODY, ui::Weight::Regular, theme::textSecondary());
            msg->setMaxWidth(w - 120);
            msg->setMaxLines(8);
            float msgH = message.empty() ? 0 : msg->height();
            float h = 120 + msgH + 40 + 76 + 56;
            auto *panel = ui::box(this, FloatRect((theme::SCREEN_W - w) / 2, (theme::SCREEN_H - h) / 2, w, h),
                                  theme::card(), theme::RADIUS_LARGE);
            ui::label(panel, title, theme::HEADING, 60, 48, ui::Weight::SemiBold);
            msg->setPosition(60, 112);
            panel->add(msg);
            float bw = 260;
            float total = (float) buttons.size() * bw + (float) (buttons.size() - 1) * 24;
            float x = w - 60 - total;
            for (size_t i = 0; i < buttons.size(); i++) {
                auto *b = new ui::Button(buttons[i], FloatRect(x, h - 56 - 76, bw, 76));
                b->setDanger(destructive && i == buttons.size() - 1);
                panel->add(b);
                btns.push_back(b);
                x += bw + 24;
            }
            screens::hintBar(this, {{A::Confirm, tr("common.select")}, {A::Back, tr("common.back")}});
            refresh();
        }

        const char *name() const override { return "dialog"; }

        void handleInput(const InputEvent &e) override {
            if (e.button == PadButton::Left && focus > 0) {
                focus--;
            } else if (e.button == PadButton::Right && focus + 1 < (int) btns.size()) {
                focus++;
            } else if (e.button == PadButton::Cross && !e.repeat) {
                int chosen = focus;
                auto cb = onChoice;
                app.pop();
                if (cb) {
                    cb(chosen);
                }
                return;
            } else if (e.button == PadButton::Circle) {
                auto cb = onChoice;
                app.pop();
                if (cb) {
                    cb(-1);
                }
                return;
            }
            refresh();
        }

    private:

        void refresh() {
            for (size_t i = 0; i < btns.size(); i++) {
                btns[i]->setFocused((int) i == focus);
            }
        }

        std::function<void(int)> onChoice;
        std::vector<ui::Button *> btns;
        int focus = 0;
    };

    // Options menus: a list in a panel on the right, over a dimmed screen
    class MenuScreen : public Screen, public ui::ListView::Adapter {

    public:

        MenuScreen(App &a, const std::string &title, std::vector<std::string> opts, int checkedIndex,
                   std::function<void(int)> choice, std::vector<std::string> extra)
                : Screen(a, true), options(std::move(opts)), details(std::move(extra)), checked(checkedIndex),
                  onChoice(std::move(choice)) {
            namespace P = layout::player;
            auto *panel = ui::box(this, FloatRect(P::OPTIONS_X, 0, P::OPTIONS_W, theme::SCREEN_H), theme::panel(), 0);
            ui::fade(panel, FloatRect(-P::OPTIONS_EDGE_W, 0, P::OPTIONS_EDGE_W, theme::SCREEN_H), theme::panel(),
                     ui::Edge::Right);
            auto *t = ui::label(panel, title, theme::TITLE, P::OPTIONS_PAD, 70, ui::Weight::SemiBold);
            t->setMaxWidth(P::OPTIONS_LIST_W);
            list = new ui::ListView(FloatRect(P::OPTIONS_PAD, 170, P::OPTIONS_LIST_W + ui::ScrollBar::WIDTH
                                                                    + ui::ScrollBar::GAP, 760), 84, 4, this);
            panel->add(list);
            auto *hints = new ui::HintBar();
            hints->setOwner(this);
            hints->setPosition(P::OPTIONS_PAD, layout::HINTS_Y);
            hints->setHints({{A::Confirm, tr("common.select")}, {A::Back, tr("common.back")}});
            panel->add(hints);
            list->setSelected(checked >= 0 ? checked : 0);
        }

        const char *name() const override { return "menu"; }

        int count() override { return (int) options.size(); }

        C2DObject *createRow(float w, float h) override {
            Row r;
            r.bg = new ui::RowFrame(w, h, -1);
            r.check = ui::label(r.bg, "", theme::BODY, 20, ui::Label::centerOffset(theme::BODY, h), ui::Weight::SemiBold,
                                theme::accentText());
            r.name = ui::label(r.bg, "", theme::BODY, 56, ui::Label::centerOffset(theme::BODY, h));
            r.name->setMaxWidth(w - 56 - 24);
            r.detail = ui::label(r.bg, "", theme::CAPTION, 56, ui::Label::centerOffset(theme::BODY, h) + 22,
                                 ui::Weight::Regular, theme::textMuted());
            r.detail->setMaxWidth(w - 56 - 24);
            rows.push_back(r);
            return r.bg;
        }

        void bindRow(C2DObject *obj, int i, bool, bool focused) override {
            bool hasDetail = i < (int) details.size() && !details[(size_t) i].empty();
            for (auto &r: rows) {
                if (r.bg != obj) {
                    continue;
                }
                r.check->setText(i == checked ? "\xE2\x9C\x93" : "");
                r.name->setText(options[(size_t) i]);
                r.name->setPosition(56, ui::Label::centerOffset(theme::BODY, 84) - (hasDetail ? 12 : 0));
                r.detail->setText(hasDetail ? details[(size_t) i] : "");
                r.name->setColor(focused ? theme::textStrong() : theme::textPrimary());
                r.name->setWeight(focused ? ui::Weight::SemiBold : ui::Weight::Regular);
                r.bg->setState(focused);
            }
        }

        void handleInput(const InputEvent &e) override {
            switch (e.button) {
                case PadButton::Up:
                    list->moveSelection(-1);
                    return;
                case PadButton::Down:
                    list->moveSelection(1);
                    return;
                case PadButton::Cross:
                case PadButton::Circle:
                case PadButton::Options: {
                    if (e.repeat) {
                        return;
                    }
                    int chosen = e.button == PadButton::Cross ? list->selected() : -1;
                    auto cb = onChoice;
                    app.pop();
                    if (cb) {
                        cb(chosen);
                    }
                    return;
                }
                default:
                    return;
            }
        }

    private:

        struct Row {
            ui::RowFrame *bg;
            ui::Label *check;
            ui::Label *name;
            ui::Label *detail;
        };

        std::vector<std::string> options;
        std::vector<std::string> details;
        int checked;
        std::function<void(int)> onChoice;
        ui::ListView *list = nullptr;
        std::vector<Row> rows;
    };

} // anonymous namespace

#ifdef __PS4__

#include <vector>
#include <cstring>
#include <cstdint>
#include "../core/utf8.h"
#include <orbis/libkernel.h>
#include "../iptv/m3u.h"
#include <orbis/Sysmodule.h>
#include <orbis/UserService.h>
#include <orbis/ImeDialog.h>

namespace {

    // The PS4 system ABI (SCE) uses a 2-byte wchar_t (UTF-16LE) for the IME dialog, no matter what the
    // build toolchain picks for the C++ type (ours is the 4-byte FreeBSD wchar_t). So every buffer that
    // touches SceImeDialog is handled as uint16_t units explicitly.

    std::u16string utf8ToUtf16(const std::string &s) {
        std::u16string out;
        const std::u32string codepoints = utf8::decode(s);
        out.reserve(codepoints.size());
        for (char32_t value : codepoints) {
            uint32_t cp = static_cast<uint32_t>(value);
            if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
            if (cp > 0xFFFF) {
                cp -= 0x10000;
                out.push_back(static_cast<char16_t>(0xD800u + (cp >> 10)));
                out.push_back(static_cast<char16_t>(0xDC00u + (cp & 0x3FFu)));
            } else {
                out.push_back(static_cast<char16_t>(cp));
            }
        }
        return out;
    }

    // Bounded, surrogate-aware UTF-16 -> UTF-8. Never emits malformed UTF-8, even if the
    // system IME returns a truncated or malformed sequence.
    std::string utf16ToUtf8(const uint16_t *ws, size_t maxUnits) {
        std::string out;
        if (!ws) return out;
        for (size_t i = 0; i < maxUnits && ws[i] != 0; ++i) {
            uint32_t cp = ws[i];
            if (cp >= 0xD800 && cp <= 0xDBFF) {
                if (i + 1 < maxUnits) {
                    uint32_t low = ws[i + 1];
                    if (low >= 0xDC00 && low <= 0xDFFF) {
                        cp = 0x10000u + ((cp - 0xD800u) << 10) + (low - 0xDC00u);
                        ++i;
                    } else {
                        cp = 0xFFFD;
                    }
                } else {
                    cp = 0xFFFD;
                }
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                cp = 0xFFFD;
            }
            out += utf8::encode(static_cast<char32_t>(cp));
        }
        return out;
    }

    // UTF-32 -> UTF-8: safety net in case the system ever expects 4-byte units.
    std::string utf32ToUtf8(const uint32_t *ws, size_t maxUnits) {
        std::string out;
        if (!ws) return out;
        for (size_t i = 0; i < maxUnits && ws[i] != 0; ++i) {
            uint32_t cp = ws[i];
            if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
                cp = 0xFFFD;
            }
            out += utf8::encode(static_cast<char32_t>(cp));
        }
        return out;
    }

    size_t countReplacementChar(const std::string &utf8) {
        const std::u32string cps = utf8::decode(utf8);
        size_t n = 0;
        for (char32_t c: cps) {
            if (c == 0xFFFD) {
                n++;
            }
        }
        return n;
    }

    struct ImeApi {
        int (*init)(const OrbisImeDialogSetting *param, void *unk) = nullptr;
        OrbisDialogStatus (*getStatus)() = nullptr;
        int (*getResult)(OrbisDialogResult *result) = nullptr;
        void (*term)() = nullptr;

        bool load() {
            // Dependency chain: libSceIme -> libSceImeBackend -> libSceImeDialog. If a dependency is
            // missing, libSceImeDialog never shows up in the module list and the OSK silently never opens.
            int dep;
#if defined(ORBIS_SYSMODULE_LIBIME)
            dep = (int) sceSysmoduleLoadModule(ORBIS_SYSMODULE_LIBIME);
            LOG_I("ime", "sceSysmoduleLoadModule(LIBIME) rc=0x%08x", (unsigned) dep);
#endif
#if defined(ORBIS_SYSMODULE_IME_BACKEND)
            dep = (int) sceSysmoduleLoadModule(ORBIS_SYSMODULE_IME_BACKEND);
            LOG_I("ime", "sceSysmoduleLoadModule(IME_BACKEND) rc=0x%08x", (unsigned) dep);
#endif
            int rc;
#if defined(ORBIS_SYSMODULE_IME_DIALOG)
            rc = (int) sceSysmoduleLoadModule(ORBIS_SYSMODULE_IME_DIALOG);
#else
            rc = (int) sceSysmoduleLoadModule(0x0096);
#endif
            if (rc != 0) {
                LOG_W("ime", "sceSysmoduleLoadModule(IME_DIALOG) rc=0x%08x, trying internal id", (unsigned) rc);
                rc = (int) sceSysmoduleLoadModuleInternal(0x0096);
                LOG_W("ime", "sceSysmoduleLoadModuleInternal(IME_DIALOG) rc=0x%08x", (unsigned) rc);
            } else {
                LOG_I("ime", "sceSysmoduleLoadModule(IME_DIALOG) ok");
            }
            int module = -1;
            OrbisKernelModule handles[1024];
            size_t count = 0;
            if (sceKernelGetModuleList(handles, sizeof(handles) / sizeof(handles[0]), &count) == 0) {
                LOG_I("ime", "module list has %zu entries", count);
                for (size_t i = 0; i < count; i++) {
                    OrbisKernelModuleInfo info;
                    memset(&info, 0, sizeof(info));
                    info.size = sizeof(info);
                    if (sceKernelGetModuleInfo(handles[i], &info) == 0 && strstr(info.name, "libSceImeDialog") != nullptr) {
                        module = (int)handles[i];
                        break;
                    }
                }
            }
            if (count == 0) {
                LOG_W("ime", "module list empty or sceKernelGetModuleList failed");
            }
            if (module < 0) {
                LOG_E("ime", "libSceImeDialog.sprx not found among %zu loaded modules", count);
                return false;
            }
            LOG_I("ime", "libSceImeDialog module handle %d", module);
            auto sym = [&](const char *name, auto &fn) -> bool {
                void *addr = nullptr;
                if (sceKernelDlsym(module, name, &addr) != 0 || !addr) {
                    LOG_E("ime", "sceKernelDlsym(%s) failed", name);
                    return false;
                }
                LOG_V("ime", "%s resolved", name);
                fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(addr);
                return true;
            };
            bool ok = sym("sceImeDialogInit", init) && sym("sceImeDialogGetStatus", getStatus)
                      && sym("sceImeDialogGetResult", getResult) && sym("sceImeDialogTerm", term);
            if (!ok) {
                LOG_E("ime", "libSceImeDialog symbols unavailable");
            }
            return ok;
        }
    };

    class ImeKeyboardScreen : public Screen {
    public:
        ImeKeyboardScreen(App &a, const std::string &title, const std::string &initial, bool isPassword, bool isUrl,
                          std::function<void(const std::string &)> done)
                : Screen(a, true), password(isPassword), url(isUrl), onDone(std::move(done)) {
            // The system IME owns the keyboard UI. Do not draw a second caption or opaque panel:
            // keep the underlying ZIMTV form visible and let the console render its native keyboard.
            // Buffers are uint16_t units because the PS4 ABI (SCE) is 2-byte wchar_t.
            title16 = utf8ToUtf16(title);
            initial16 = utf8ToUtf16(initial);
            buf.assign(1024, 0);
            if (!initial16.empty()) {
                size_t n = std::min(initial16.size(), buf.size() - 1);
                // Do not leave half of a UTF-16 surrogate pair at the end of the initial buffer.
                if (n > 0 && initial16[n - 1] >= 0xD800 && initial16[n - 1] <= 0xDBFF) {
                    --n;
                }
                memcpy(buf.data(), initial16.data(), n * sizeof(uint16_t));
                buf[n] = 0;
            }
        }

        const char *name() const override { return "ime-keyboard"; }

        void onEnter() override {
            if (opened) return;
            opened = true;
            if (!openDialog()) {
                fail();
            }
        }

        void tick(double) override {
            if (!running || finished) return;
            OrbisDialogStatus st = api.getStatus();
            if (st != lastStatus) {
                LOG_I("ime", "IME dialog status %d -> %d", (int) lastStatus, (int) st);
                lastStatus = st;
            }
            if (st == ORBIS_DIALOG_STATUS_STOPPED) {
                OrbisDialogResult res;
                memset(&res, 0, sizeof(res));
                int grc = api.getResult(&res);
                api.term();
                running = false;
                finished = true;
                bool ok = (grc == 0) && (res.endstatus == ORBIS_DIALOG_OK);
                // the system wrote its ABI's wchar into buf: candidates are UTF-16 (the SCE ABI) and UTF-32;
                // a usable candidate carries no replacement characters; the longer usable one wins (ties: utf-16)
                std::string asUtf16 = ok ? utf16ToUtf8(buf.data(), buf.size()) : std::string();
                std::string asUtf32 = ok ? utf32ToUtf8(reinterpret_cast<const uint32_t *>(buf.data()), buf.size() / 2)
                                         : std::string();
                const size_t repl16 = ok ? countReplacementChar(asUtf16) : 0;
                const size_t repl32 = ok ? countReplacementChar(asUtf32) : 0;
                const long score16 = repl16 == 0 ? (long) asUtf16.size() : -1;
                const long score32 = repl32 == 0 ? (long) asUtf32.size() : -1;
                const bool useUtf16 = score16 >= score32;   // tie -> utf-16 (the SCE ABI)
                const char *picked = useUtf16 ? "utf-16" : "utf-32";
                std::string out = ok ? (useUtf16 ? asUtf16 : asUtf32) : std::string();
                LOG_I("ime", "buffer decode utf-16: %zu byte(s), %zu replacement(s); utf-32: %zu byte(s), %zu replacement(s); using %s",
                      asUtf16.size(), repl16, asUtf32.size(), repl32, picked);
                const std::string safeText = out.find("://") != std::string::npos ? m3u::displayUrl(out)
                                                                                     : std::string("<redacted>");
                LOG_I("ime", "IME dialog closed (getResult rc=%d, endstatus=%d, %zu bytes as utf-8, %s)", (int) grc,
                      (int) res.endstatus, out.size(), safeText.c_str());
                auto cb = onDone;
                app.pop();
                if (ok && cb) {
                    cb(out);
                    LOG_I("ime", "editor updated with the text");
                } else {
                    LOG_I("ime", "cancelled, nothing applied");
                }
            }
        }

        void handleInput(const InputEvent &) override {
            // IME dialog handles input
        }

    private:
        bool openDialog() {
            if (!api.load()) {
                LOG_E("ime", "libSceImeDialog not available");
                return false;
            }
            int32_t user = -1;
            if (sceUserServiceGetInitialUser(&user) != 0) {
                LOG_W("ime", "sceUserServiceGetInitialUser failed, falling back to user 0");
                user = 0;
            }
            memset(&param, 0, sizeof(param));
            param.userId = (uint32_t)(user >= 0 ? user : 0);
            param.type = url ? ORBIS_TYPE_TYPE_URL : password ? ORBIS_TYPE_BASIC_LATIN : ORBIS_TYPE_DEFAULT;
            param.supportedLanguages = 0;
            param.enterLabel = ORBIS_BUTTON_LABEL_DEFAULT;
            param.inputMethod = ORBIS__DEFAULT;
            param.filter = nullptr;
            param.option = 0;
            param.maxTextLength = (uint32_t)(buf.size() - 1);
            param.inputTextBuffer = reinterpret_cast<wchar_t *>(buf.data());
            param.posx = theme::SCREEN_W / 2.0f;
            param.posy = theme::SCREEN_H / 2.0f;
            param.horizontalAlignment = ORBIS_H_CENTER;
            param.verticalAlignment = ORBIS_V_CENTER;
            param.placeholder = nullptr;
            param.title = title16.empty() ? nullptr : reinterpret_cast<const wchar_t *>(title16.c_str());
            memset(param.reserved, 0, sizeof(param.reserved));
            LOG_I("ime", "opening IME dialog for user %u, type %u, maxTextLength %u, initial %zu chars",
                  (unsigned) param.userId, (unsigned) param.type, (unsigned) param.maxTextLength, initial16.size());
            int rc = api.init(&param, nullptr);
            if (rc < 0) {
                LOG_E("ime", "sceImeDialogInit failed: 0x%08x", (unsigned) rc);
                return false;
            }
            LOG_I("ime", "sceImeDialogInit ok (rc=0x%08x)", (unsigned) rc);
            running = true;
            return true;
        }

        void fail() {
            finished = true;
            LOG_E("ime", "IME dialog unavailable, returning to the editor with an error");
            app.pop();
            app.push(screens::makeDialog(app, tr("keyboard.native_title"),
                                         tr("keyboard.native_error") + tr("keyboard.native_log_detail"),
                                         std::vector<std::string>{tr("common.ok")}, std::function<void(int)>{}, false));
        }

        bool password = false;
        bool url = false;
        bool opened = false;
        bool running = false;
        bool finished = false;
        OrbisDialogStatus lastStatus = ORBIS_DIALOG_STATUS_NONE;
        std::function<void(const std::string &)> onDone;
        std::u16string title16;
        std::u16string initial16;
        std::vector<uint16_t> buf;
        OrbisImeDialogSetting param;
        ImeApi api;
    };

} // anonymous namespace

#else // host/non-PS4 fallback keeps custom keyboard behavior minimal? reuse previous simple approach by including keyboard model if present

#include "../ui/keyboard_model.h"

namespace {

    const float PANEL_W = 1440;
    const float PANEL_H = 820;
    const float KEY_UNIT = 100;
    const float KEY_GAP = 10;
    const float KEY_H = 74;

    class KeyboardScreen : public Screen {
    public:
        KeyboardScreen(App &a, const std::string &title, const std::string &initial, bool isPassword,
                       std::function<void(const std::string &)> done)
                : Screen(a, true), model(initial, 512), password(isPassword), onDone(std::move(done)) {
            float px = (theme::SCREEN_W - PANEL_W) / 2;
            float py = (theme::SCREEN_H - PANEL_H) / 2 - 20;
            auto *panel = ui::box(this, FloatRect(px, py, PANEL_W, PANEL_H), theme::surface(), 24);
            ui::label(panel, title, theme::HEADING, 60, 40, ui::Weight::SemiBold);
            auto *field = ui::box(panel, FloatRect(60, 104, PANEL_W - 120, 84), theme::background(), theme::RADIUS_SMALL);
            field->setOutlineColor(theme::accent());
            field->setOutlineThickness(2);
            preview = ui::label(field, "", theme::HEADING, 24, ui::Label::centerOffset(theme::HEADING, 84));
            preview->setMaxWidth(PANEL_W - 120 - 48);
            float gridW = KeyboardModel::COLUMNS * KEY_UNIT + (KeyboardModel::COLUMNS - 1) * KEY_GAP;
            float gx = (PANEL_W - gridW) / 2;
            float gy = 220;
            const auto &rows = model.rows();
            for (int r = 0; r < (int) rows.size(); r++) {
                std::vector<KeyView> line;
                for (int c = 0; c < (int) rows[(size_t) r].size(); c++) {
                    const Key &k = rows[(size_t) r][(size_t) c];
                    float x = gx + (float) model.keyStart(r, c) * (KEY_UNIT + KEY_GAP);
                    float w = (float) k.span * KEY_UNIT + (float) (k.span - 1) * KEY_GAP;
                    KeyView v;
                    v.bg = ui::box(panel, FloatRect(x, gy + (float) r * (KEY_H + KEY_GAP), w, KEY_H), theme::surfaceElevated(), 12);
                    bool special = k.action != KeyAction::Char;
                    v.label = ui::label(v.bg, "", special ? theme::LABEL : theme::HEADING, 0,
                                        ui::Label::centerOffset(special ? theme::LABEL : theme::HEADING, KEY_H), ui::Weight::SemiBold);
                    v.label->setAlign(ui::Align::Center, w);
                    if (special) {
                        v.label->setText(model.label(k));
                        if (v.label->width() > w - 16) {
                            v.label->setCharSize(theme::CAPTION - 2);
                            v.label->setPosition(0, ui::Label::centerOffset(theme::CAPTION - 2, KEY_H));
                        }
                        v.label->setMaxWidth(w - 12);
                    }
                    line.push_back(v);
                }
                keys.push_back(line);
            }
            screens::hintBar(this, {{A::Type, tr("keyboard.hint_type")}, {A::TextBackspace, tr("keyboard.hint_backspace")},
                                    {A::TextSpace, tr("keyboard.hint_space")}, {A::TextShift, tr("keyboard.hint_shift")},
                                    {A::TextDone, tr("keyboard.hint_done")}, {A::Back, tr("common.cancel")}});
            refresh();
        }
        const char *name() const override { return "keyboard"; }
        bool handleKey(const kbd::Event &k) override {
            switch (model.applyKey(k)) {
                case KeyboardModel::KeyResult::Ok: finish(true); return true;
                case KeyboardModel::KeyResult::Cancel: finish(false); return true;
                case KeyboardModel::KeyResult::Unhandled: return false;
                default: refresh(); return true;
            }
        }
        void handleInput(const InputEvent &e) override {
            switch (e.button) {
                case PadButton::Up: model.move(0, -1); break;
                case PadButton::Down: model.move(0, 1); break;
                case PadButton::Left: model.move(-1, 0); break;
                case PadButton::Right: model.move(1, 0); break;
                case PadButton::Cross:
                    if (e.repeat) return;
                    switch (model.press()) {
                        case KeyboardModel::Result::Ok: finish(true); return;
                        case KeyboardModel::Result::Cancel: finish(false); return;
                        default: break;
                    }
                    break;
                case PadButton::Square: model.backspace(); break;
                case PadButton::Triangle: model.space(); break;
                case PadButton::L2: if (!e.repeat) model.toggleShift(); break;
                case PadButton::R2: case PadButton::Options: if (!e.repeat) finish(true); return;
                case PadButton::Circle: finish(false); return;
                default: return;
            }
            refresh();
        }
    private:
        struct KeyView { RectangleShape *bg; ui::Label *label; };
        void finish(bool ok) { app.pop(); if (ok && onDone) onDone(model.text()); }
        void refresh() {
            std::string shown = password ? screens::mask(model.text()) : model.text();
            preview->setText(shown + "|");
            const auto &rows = model.rows();
            for (int r = 0; r < (int) rows.size(); r++) {
                for (int c = 0; c < (int) rows[(size_t) r].size(); c++) {
                    const Key &k = rows[(size_t) r][(size_t) c];
                    KeyView &v = keys[(size_t) r][(size_t) c];
                    bool focused = r == model.focusRow() && c == model.focusCol();
                    bool active = k.action == KeyAction::Shift && model.shift();
                    v.label->setText(model.label(k));
                    Color bg = focused ? theme::accent() : active ? theme::accentMuted()
                                                                  : k.action == KeyAction::Ok ? theme::accentMuted()
                                                                  : k.action == KeyAction::Char ? theme::surfaceElevated()
                                                                  : theme::cardHover();
                    v.bg->setFillColor(bg);
                    v.bg->setOutlineThickness(0);
                    v.label->setColor(focused ? theme::onAccent() : theme::textPrimary());
                }
            }
        }
        KeyboardModel model;
        bool password;
        std::function<void(const std::string &)> onDone;
        ui::Label *preview = nullptr;
        std::vector<std::vector<KeyView>> keys;
    };

} // anonymous namespace

#endif

namespace screens {

    Screen *makeMenu(App &app, const std::string &title, const std::vector<std::string> &options, int checked,
                     std::function<void(int)> onChoice, const std::vector<std::string> &details) {
        return new MenuScreen(app, title, options, checked, std::move(onChoice), details);
    }

    Screen *makeKeyboard(App &app, const std::string &title, const std::string &initial, KeyboardKind kind,
                         std::function<void(const std::string &)> onDone) {
        const bool ascii = kind == KeyboardKind::Ascii;
#ifdef __PS4__
        return new ImeKeyboardScreen(app, title, initial, ascii, kind == KeyboardKind::Url, std::move(onDone));
#else
        return new KeyboardScreen(app, title, initial, ascii, std::move(onDone));
#endif
    }

    Screen *makeDialog(App &app, const std::string &title, const std::string &message,
                       const std::vector<std::string> &buttons, std::function<void(int)> onChoice, bool destructive) {
        return new DialogScreen(app, title, message, buttons, std::move(onChoice), destructive);
    }
}
