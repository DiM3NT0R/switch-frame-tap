/*
 * v0.7.12: one key of sdmc:/config/switch-frame-tap/config.ini, read or set
 * in place - for the overlay, which changes single settings ("screen off
 * while streaming") without the manager app's whole-file rewrite. Lines are
 * "key = value"; a key that is not there is added at the end.
 */
#pragma once
#include <cstdio>
#include <cstring>
#include <string>

namespace sftini {

    constexpr const char *Path = "sdmc:/config/switch-frame-tap/config.ini";

    inline std::string ReadAll() {
        std::string s;
        FILE *f = std::fopen(Path, "rb");
        if (f == nullptr) { return s; }
        char b[1024];
        size_t n;
        while ((n = std::fread(b, 1, sizeof(b), f)) > 0) { s.append(b, n); }
        std::fclose(f);
        return s;
    }

    /* the line's [start, end) for key, or npos */
    inline size_t FindKey(const std::string &s, const char *key, size_t *end) {
        const size_t klen = std::strlen(key);
        size_t i = 0;
        while (i < s.size()) {
            size_t e = s.find('\n', i);
            if (e == std::string::npos) { e = s.size(); }
            size_t p = i;
            while (p < e && (s[p] == ' ' || s[p] == '\t')) { ++p; }
            if (e - p > klen && s.compare(p, klen, key) == 0) {
                size_t q = p + klen;
                while (q < e && (s[q] == ' ' || s[q] == '\t')) { ++q; }
                if (q < e && s[q] == '=') { *end = e; return i; }
            }
            i = e + 1;
        }
        return std::string::npos;
    }

    inline bool Get(const char *key, std::string *out) {
        const std::string s = ReadAll();
        size_t end = 0;
        const size_t i = FindKey(s, key, &end);
        if (i == std::string::npos) { return false; }
        size_t q = s.find('=', i) + 1;
        while (q < end && (s[q] == ' ' || s[q] == '\t')) { ++q; }
        size_t e = end;
        while (e > q && (s[e - 1] == ' ' || s[e - 1] == '\r' || s[e - 1] == '\t')) { --e; }
        *out = s.substr(q, e - q);
        return true;
    }

    inline bool Set(const char *key, const char *value) {
        std::string s = ReadAll();
        const std::string line = std::string(key) + " = " + value;
        size_t end = 0;
        const size_t i = FindKey(s, key, &end);
        if (i != std::string::npos) { s.replace(i, end - i, line); }
        else {
            if (!s.empty() && s.back() != '\n') { s += '\n'; }
            s += line + "\n";
        }
        FILE *f = std::fopen(Path, "wb");
        if (f == nullptr) { return false; }
        const bool ok = std::fwrite(s.data(), 1, s.size(), f) == s.size();
        std::fclose(f);
        return ok;
    }

}
