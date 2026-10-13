#include "applet_mitm_log.hpp"
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace ams::mitm::applet {

    constinit Stats g_stats = {};

    namespace {

        const char *LogPath  = "sdmc:/applet-mitm.log";
        const char *LastPath = "sdmc:/applet-mitm.last";
        /* M98: a release install logs into its config folder, and stops at
         * 4 MB - a test run's log is read the same day; a user's can run
         * for weeks of boots without anyone looking */
        constexpr s64 ReleaseLogCap = 4 * 1024 * 1024;
        constinit os::SdkMutex g_lock;
        constinit bool g_ready = false;
        /* v0.7.10: held in memory while a teardown races an applet - each
         * flushed line costs the SD ~15 ms, and 20 of them lost the race */
        constinit bool g_defer = false;
        constinit char g_defer_buf[8192];
        constinit size_t g_defer_len = 0;

        void AppendToLog(const char *data, size_t n) {
            fs::FileHandle f;
            if (R_FAILED(fs::OpenFile(std::addressof(f), LogPath, fs::OpenMode_Write | fs::OpenMode_AllowAppend))) { return; }
            s64 size = 0;
            static_cast<void>(fs::GetFileSize(std::addressof(size), f));
            if (g_release_mode && size > ReleaseLogCap) { fs::CloseFile(f); return; }
            static_cast<void>(fs::WriteFile(f, size, data, n, fs::WriteOption::Flush));
            fs::CloseFile(f);
        }

        void WriteWhole(const char *path, const char *data, size_t len) {
            fs::DeleteFile(path);
            if (R_FAILED(fs::CreateFile(path, static_cast<s64>(len)))) { return; }
            fs::FileHandle f;
            if (R_FAILED(fs::OpenFile(std::addressof(f), path, fs::OpenMode_Write))) { return; }
            static_cast<void>(fs::WriteFile(f, 0, data, len, fs::WriteOption::Flush));
            fs::CloseFile(f);
        }

    }

    constinit bool g_release_mode = false;
    constinit u32 g_arm_probe_rc = 0;

    void LogInit() {
        std::scoped_lock lk(g_lock);
        if (R_FAILED(fs::MountSdCard("sdmc"))) {
            return;
        }
        {
            /* logged by Main (LogLine here would take g_lock twice) */
            fs::DirectoryEntryType t;
            const Result r = fs::GetEntryType(std::addressof(t), "sdmc:/applet-mitm.armed");
            g_arm_probe_rc = r.GetValue();
            g_release_mode = R_FAILED(r);
        }
        if (g_release_mode) {
            static_cast<void>(fs::CreateDirectory("sdmc:/config"));
            static_cast<void>(fs::CreateDirectory("sdmc:/config/switch-frame-tap"));
            LogPath  = "sdmc:/config/switch-frame-tap/log.txt";
            LastPath = "sdmc:/config/switch-frame-tap/last.txt";
        }
        /* truncate */
        fs::DeleteFile(LogPath);
        fs::CreateFile(LogPath, 0);
        g_ready = true;
        WriteWhole(LastPath, "LogInit\n", 8);
    }

    void LogMark(const char *what) {
        {
            std::scoped_lock lk(g_lock);
            if (g_ready && !g_defer) {
                char b[128];
                const int n = std::snprintf(b, sizeof(b), "%s\n", what);
                if (n > 0) { WriteWhole(LastPath, b, static_cast<size_t>(n)); }
            }
        }
        LogLine("-> %s", what);
    }

    void LogLine(const char *fmt, ...) {
        std::scoped_lock lk(g_lock);
        if (!g_ready) {
            return;
        }

        char line[512];
        const u64 ms = armTicksToNs(armGetSystemTick()) / UINT64_C(1000000);
        int n = std::snprintf(line, sizeof(line), "[%6llu.%03llu] ",
                              static_cast<unsigned long long>(ms / 1000),
                              static_cast<unsigned long long>(ms % 1000));

        std::va_list ap;
        va_start(ap, fmt);
        n += std::vsnprintf(line + n, sizeof(line) - n - 2, fmt, ap);
        va_end(ap);
        if (n < 0) { return; }
        if (static_cast<size_t>(n) > sizeof(line) - 2) { n = sizeof(line) - 2; }
        line[n++] = '\n';

        if (g_defer) {
            if (g_defer_len + static_cast<size_t>(n) <= sizeof(g_defer_buf)) {
                std::memcpy(g_defer_buf + g_defer_len, line, static_cast<size_t>(n));
                g_defer_len += static_cast<size_t>(n);
            }
            return;
        }
        /* open/append/close every line so a crash can't lose it */
        AppendToLog(line, static_cast<size_t>(n));
    }

    void LogDefer(bool on) {
        std::scoped_lock lk(g_lock);
        if (on) { g_defer = true; return; }
        g_defer = false;
        if (g_ready && g_defer_len != 0) { AppendToLog(g_defer_buf, g_defer_len); }
        g_defer_len = 0;
    }

}
