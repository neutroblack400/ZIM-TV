// Home's L1 / R1 (keyboard: Shift+Tab / Tab): move focus through its four top-level tabs from anywhere on Home.
// The mapping and clamped index are host-testable. The ends stop (no wrap); the D-pad still navigates Home content.

#ifndef ZIMTV_APP_HOME_NAV_H
#define ZIMTV_APP_HOME_NAV_H

#include <algorithm>

#include "../platform/input.h"

namespace homenav {

    constexpr int TAB_COUNT = 4;    // Início, TV ao vivo, Filmes, Séries

    inline int direction(PadButton button) {
        if (button == PadButton::L1) return -1;
        if (button == PadButton::R1) return 1;
        return 0;
    }

    inline bool platformsStale(unsigned builtForVodGeneration, unsigned currentVodGeneration) {
        return builtForVodGeneration != currentVodGeneration;
    }

    inline bool hasHeroContent(bool hasResumeItem, bool hasHighlight) {
        return hasResumeItem || hasHighlight;
    }

    // the navigation item L1 (dir -1) / R1 (dir +1) focuses; `navIndex`: the navigation's last focus, `count`: tabs
    inline int shoulder(int navIndex, int count, int dir) {
        if (count <= 0) {
            return 0;
        }
        int at = std::min(std::max(navIndex, 0), count - 1);
        return std::min(std::max(at + (dir < 0 ? -1 : 1), 0), count - 1);
    }
}

#endif // ZIMTV_APP_HOME_NAV_H
