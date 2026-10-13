/*
 * v0.7.10: no resident connection to sm.
 *
 * A connection to sm ("sm:", svcConnectToNamedPort) is charged to the process
 * that makes it - and ours is in the Applet resource-limit group, whose 6
 * sessions are the applets' (5 + 1 for Atmosphere's memlet). Held for the
 * whole boot, it left the offline web applet (Smash's manual and news) one
 * session short: 2210-1024 (SooraMaru). Sessions sm makes FOR us (services
 * we get) are charged to sm, not us - only our own sm connection counts.
 *
 * So the boot reference is dropped once startup is done, and every later use
 * of sm holds one only for as long as it needs it: SmUse around it. libnx
 * counts the references under a mutex (service_guard.h) and closes the
 * session at zero. libstratosphere's mitm acknowledgements open their own
 * short-lived sm session already.
 *
 * The last close must DetachClient first. sm treats a client session that
 * closes while still registered as the process going away and unregisters
 * every service it hosts (Atmosphere sm OnClientDisconnected) - sftap:s
 * included, after which the next wrapped object waited on sm forever and
 * every game hung on its loading screen (v0.7.10-test5). libnx's smExit does
 * not detach (libstratosphere's own short sessions do), so we count the users
 * here and detach when ours is the last reference.
 */
#pragma once
#include <stratosphere.hpp>

namespace ams::mitm::applet {

    namespace sm_use {
        inline constinit os::SdkMutex g_lock;
        inline constinit int g_users = 0;
        inline constinit bool g_boot_held = true;
    }

    class SmUse {
        public:
            SmUse() {
                std::scoped_lock lk(sm_use::g_lock);
                m_ok = R_SUCCEEDED(::smInitialize());
                if (m_ok) { m_fresh = ++sm_use::g_users == 1 && !sm_use::g_boot_held; }
            }
            ~SmUse() {
                if (!m_ok) { return; }
                std::scoped_lock lk(sm_use::g_lock);
                if (--sm_use::g_users == 0 && !sm_use::g_boot_held) { static_cast<void>(::smDetachClient()); }
                ::smExit();
            }
            SmUse(const SmUse &) = delete;
            SmUse &operator=(const SmUse &) = delete;
            bool ok() const { return m_ok; }
            /* A new sm connection was made for this use (none was open). */
            bool fresh() const { return m_fresh; }
        private:
            bool m_ok;
            bool m_fresh = false;
    };

    /* Drops the boot reference (sm::Initialize) once startup is done. */
    inline Result SmReleaseBoot() {
        std::scoped_lock lk(sm_use::g_lock);
        if (!sm_use::g_boot_held) { R_SUCCEED(); }
        sm_use::g_boot_held = false;
        Result rc = ResultSuccess();
        if (sm_use::g_users == 0) { rc = ::smDetachClient(); }
        ::smExit();
        R_RETURN(rc);
    }

}
