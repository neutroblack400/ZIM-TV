// ZIMTV's dark violet / indigo TV palette, inspired by the supplied streaming-app reference.
// Accents remain user-selectable (Settings > Appearance > Accent color).
// Screens never use these values directly: they use the theme:: tokens (ui/theme.h).

#ifndef ZIMTV_UI_PALETTE_H
#define ZIMTV_UI_PALETTE_H

#include <cmath>
#include <cstdint>
#include <cstring>

namespace palette {

    struct Rgba {
        uint8_t r, g, b, a;
    };

    constexpr Rgba rgb(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) { return {r, g, b, a}; }

    // ---------------------------------------------------------------- surfaces (darkest to lightest) - midnight violet
    constexpr Rgba BACKGROUND = rgb(8, 6, 20);                // deep indigo-violet
    constexpr Rgba BACKGROUND_SECONDARY = rgb(14, 10, 32);    // gradient bottom
    constexpr Rgba SURFACE = rgb(24, 17, 43);                 // panels, list rows
    constexpr Rgba CARD = rgb(30, 22, 52);                    // tiles, cards
    constexpr Rgba SURFACE_ELEVATED = rgb(42, 31, 68);        // buttons, chips, elevated panels
    constexpr Rgba CARD_HOVER = rgb(58, 41, 91);             // focused card / field
    constexpr Rgba LOGO_TILE = rgb(45, 34, 70);              // behind channel logos
    constexpr Rgba DIVIDER = rgb(65, 51, 93);
    constexpr Rgba DIVIDER_SOFT = rgb(25, 19, 43);            // separators
    constexpr Rgba PANEL = rgb(15, 10, 33, 246);              // side panels
    constexpr Rgba FOCUS_NEUTRAL = rgb(43, 31, 66);

    // ---------------------------------------------------------------- text
    constexpr Rgba TEXT_STRONG = rgb(255, 253, 255);
    constexpr Rgba TEXT_PRIMARY = rgb(239, 235, 248);
    constexpr Rgba TEXT_SECONDARY = rgb(190, 181, 210);
    constexpr Rgba TEXT_MUTED = rgb(143, 132, 168);
    constexpr Rgba TEXT_ON_LIGHT = rgb(17, 10, 28);

    struct Accent {
        const char *key;
        const char *nameKey;
        Rgba accent;
        Rgba text;
        Rgba muted;
        Rgba focus;
        Rgba onAccent;
    };

    constexpr Rgba W = TEXT_STRONG;
    constexpr Rgba D = TEXT_ON_LIGHT;

    // Violet is the reference theme; blue and the other presets stay available in Appearance.
    constexpr Accent ACCENTS[] = {
            {"green", "appearance.accent.green", rgb(120, 255, 80), rgb(170, 255, 140), rgb(40, 70, 42), rgb(38, 58, 40), D},
            {"magenta", "appearance.accent.magenta", rgb(255, 90, 190), rgb(255, 160, 220), rgb(70, 40, 60), rgb(54, 38, 48), D},
            {"purple", "appearance.accent.purple", rgb(160, 90, 255), rgb(208, 174, 255), rgb(61, 39, 82), rgb(53, 37, 76), W},
            {"cyan", "appearance.accent.cyan", rgb(0, 230, 200), rgb(120, 245, 230), rgb(34, 62, 62), rgb(34, 50, 50), D},        // alien cyan
            {"red", "appearance.accent.red", rgb(255, 70, 90), rgb(255, 150, 160), rgb(70, 38, 42), rgb(52, 36, 38), W},         // alert red
            {"orange", "appearance.accent.orange", rgb(255, 150, 40), rgb(255, 200, 120), rgb(70, 52, 36), rgb(52, 44, 36), D},
            {"blue", "appearance.accent.blue", rgb(70, 150, 255), rgb(150, 200, 255), rgb(40, 50, 70), rgb(38, 44, 56), D},
            {"teal", "appearance.accent.teal", rgb(40, 210, 170), rgb(140, 235, 210), rgb(36, 60, 55), rgb(36, 50, 46), D},
            {"white", "appearance.accent.white", rgb(230, 240, 230), rgb(240, 250, 240), rgb(58, 64, 58), rgb(46, 52, 46), D}};

    constexpr int ACCENT_COUNT = (int) (sizeof(ACCENTS) / sizeof(ACCENTS[0]));
    constexpr int DEFAULT_ACCENT = 2;   // reference violet

    inline int accentIndex(const char *key) {
        for (int i = 0; i < ACCENT_COUNT; i++) {
            if (key && std::strcmp(ACCENTS[i].key, key) == 0) {
                return i;
            }
        }
        return -1;
    }

    // ---------------------------------------------------------------- states
    constexpr Rgba SUCCESS = rgb(107, 206, 159);
    constexpr Rgba WARNING = rgb(255, 210, 80);
    constexpr Rgba DANGER = rgb(255, 90, 110);
    constexpr Rgba SUCCESS_SURFACE = rgb(20, 55, 43);
    constexpr Rgba DANGER_SURFACE = rgb(80, 20, 30);

    // ---------------------------------------------------------------- overlays
    constexpr Rgba OVERLAY = rgb(7, 4, 18, 220);
    constexpr Rgba OVERLAY_PANEL = rgb(16, 10, 34, 235);
    constexpr Rgba SCRIM = rgb(0, 0, 0, 195);
    constexpr Rgba VIDEO_BACKGROUND = rgb(0, 0, 0);
    constexpr Rgba PROGRESS_TRACK = rgb(218, 196, 255, 46);
    constexpr Rgba PROGRESS_TRACK_ON_IMAGE = rgb(0, 0, 0, 170);
    constexpr Rgba SCROLL_TRACK = rgb(186, 137, 255, 18);
    constexpr Rgba SCROLL_THUMB = rgb(186, 137, 255, 130);
    constexpr Rgba SCROLL_THUMB_IDLE = rgb(186, 137, 255, 52);
    constexpr Rgba SWITCH_OFF = rgb(68, 61, 83);
    constexpr Rgba SWITCH_KNOB_OFF = rgb(207, 201, 220);
    constexpr Rgba BACKDROP_TINT = rgb(201, 201, 201);
    constexpr Rgba BACKDROP_SHADE = rgb(8, 6, 20, 160);
    constexpr Rgba ARTWORK_DIM = rgb(220, 220, 220);

    // DualShock symbol colours - keep recognizable but tinted
    constexpr Rgba PAD_CROSS = rgb(140, 190, 255);
    constexpr Rgba PAD_CIRCLE = rgb(255, 120, 150);
    constexpr Rgba PAD_SQUARE = rgb(255, 150, 230);
    constexpr Rgba PAD_TRIANGLE = rgb(125, 224, 185);

    // monogram tiles: low-saturation violet / indigo tones
    constexpr Rgba MONOGRAM[8] = {rgb(57, 38, 84), rgb(48, 43, 91), rgb(36, 58, 88), rgb(77, 49, 81),
                                  rgb(79, 42, 69), rgb(48, 57, 92), rgb(61, 50, 100), rgb(40, 65, 84)};

    inline Rgba over(Rgba fg, Rgba bg) {
        double a = fg.a / 255.0;
        auto mix = [a](uint8_t f, uint8_t b) { return (uint8_t) std::lround(f * a + b * (1 - a)); };
        return rgb(mix(fg.r, bg.r), mix(fg.g, bg.g), mix(fg.b, bg.b));
    }

    inline double luminance(Rgba c) {
        auto lin = [](uint8_t v) {
            double s = v / 255.0;
            return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * lin(c.r) + 0.7152 * lin(c.g) + 0.0722 * lin(c.b);
    }

    inline double contrast(Rgba a, Rgba b) {
        double la = luminance(a);
        double lb = luminance(b);
        return (std::fmax(la, lb) + 0.05) / (std::fmin(la, lb) + 0.05);
    }
}

#endif // ZIMTV_UI_PALETTE_H
