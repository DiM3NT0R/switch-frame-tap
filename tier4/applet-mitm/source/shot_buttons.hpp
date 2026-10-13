/*
 * v0.7.11: the custom screenshot combo, shared by the sysmodule (config.ini ->
 * arm tokens) and the manager app (recording and showing it).
 *
 * config.ini: screenshot_buttons = 4, screenshot_custom = ZR+Down+Capture.
 * The names are the controller's (libnx HidNpadButton bits 0-15); Capture is
 * not an Npad button - the console reads it apart - so it is a flag of its own.
 */
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace sft::shotbuttons {

    struct Button { const char *name; uint32_t bit; };

    inline constexpr Button List[] = {
        { "A",     1u << 0  }, { "B",     1u << 1  }, { "X",     1u << 2  }, { "Y",     1u << 3  },
        { "L3",    1u << 4  }, { "R3",    1u << 5  }, { "L",     1u << 6  }, { "R",     1u << 7  },
        { "ZL",    1u << 8  }, { "ZR",    1u << 9  }, { "Plus",  1u << 10 }, { "Minus", 1u << 11 },
        { "Left",  1u << 12 }, { "Up",    1u << 13 }, { "Right", 1u << 14 }, { "Down",  1u << 15 },
    };
    inline constexpr uint32_t AllButtons = 0xFFFFu;

    inline bool NameIs(const char *s, size_t n, const char *name) {
        if (std::strlen(name) != n) { return false; }
        for (size_t i = 0; i < n; ++i) {
            char a = s[i], b = name[i];
            if (a >= 'a' && a <= 'z') { a = static_cast<char>(a - 32); }
            if (b >= 'a' && b <= 'z') { b = static_cast<char>(b - 32); }
            if (a != b) { return false; }
        }
        return true;
    }

    /* "ZR+Down+Capture" -> the button bits, and whether Capture is in it;
     * unknown names are skipped */
    inline uint32_t Parse(const char *s, bool *capture) {
        uint32_t mask = 0;
        bool cap = false;
        while (s != nullptr && *s != 0) {
            while (*s == ' ' || *s == '+') { ++s; }
            const char *e = s;
            while (*e != 0 && *e != '+' && *e != ' ' && *e != '\r' && *e != '\n') { ++e; }
            const size_t n = static_cast<size_t>(e - s);
            if (n == 0) { break; }
            if (NameIs(s, n, "Capture")) { cap = true; }
            for (const auto &b : List) { if (NameIs(s, n, b.name)) { mask |= b.bit; } }
            s = e;
            if (*s == '\r' || *s == '\n') { break; }
        }
        if (capture != nullptr) { *capture = cap; }
        return mask;
    }

    /* the other way: "ZR+Down+Capture" ("" for none) */
    inline void Format(uint32_t mask, bool capture, char *out, size_t cap) {
        size_t w = 0;
        if (cap == 0) { return; }
        out[0] = 0;
        for (const auto &b : List) {
            if ((mask & b.bit) == 0) { continue; }
            const int k = std::snprintf(out + w, cap - w, "%s%s", w ? "+" : "", b.name);
            if (k < 0 || static_cast<size_t>(k) >= cap - w) { return; }
            w += static_cast<size_t>(k);
        }
        if (capture) { std::snprintf(out + w, cap - w, "%sCapture", w ? "+" : ""); }
    }

}
