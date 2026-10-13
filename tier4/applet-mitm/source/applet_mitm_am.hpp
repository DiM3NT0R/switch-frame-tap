/*
 * v0.7.10: the game's am connection (appletOE), to stop streaming BEFORE a
 * browser applet starts.
 *
 * The browser applets (manuals, news, eShop) need nearly the whole Applet
 * pool, and while a viewer is connected the memory donor holds 42 MB of it.
 * Reacting once the applet is running was too late (v0.7.10: 62 ms after it
 * appeared, it had already failed its allocation - 2210-1024). The game asks
 * am for the applet first: ILibraryAppletCreator::CreateLibraryApplet. Here
 * that request waits until the stream has stopped and the memory is back,
 * then goes on to am unchanged.
 *
 * Wrapped: the application's appletOE root (OpenApplicationProxy, forwarded
 * with the game's pid), its IApplicationProxy (GetLibraryAppletCreator only)
 * and the creator (CreateLibraryApplet only). Every other command forwards
 * untouched (domain objects we do not own, undeclared commands).
 */
#pragma once
#include <stratosphere.hpp>

#define AMS_AM_CREATOR_MITM_INTERFACE_INFO(C, H) \
    AMS_SF_METHOD_INFO(C, H, 0, Result, CreateLibraryApplet, (u32 applet_id, u32 mode), (applet_id, mode))

AMS_SF_DEFINE_MITM_INTERFACE(ams::mitm::applet, IAmCreatorMitm, AMS_AM_CREATOR_MITM_INTERFACE_INFO, 0x2AB1E150)

#define AMS_AM_PROXY_MITM_INTERFACE_INFO(C, H) \
    AMS_SF_METHOD_INFO(C, H, 11, Result, GetLibraryAppletCreator, (sf::Out<sf::SharedPointer<ams::mitm::applet::IAmCreatorMitm>> out), (out))

AMS_SF_DEFINE_MITM_INTERFACE(ams::mitm::applet, IAmProxyMitm, AMS_AM_PROXY_MITM_INTERFACE_INFO, 0x2AB1E151)

#define AMS_AM_OE_MITM_INTERFACE_INFO(C, H) \
    AMS_SF_METHOD_INFO(C, H, 0, Result, OpenApplicationProxy, (sf::Out<sf::SharedPointer<ams::mitm::applet::IAmProxyMitm>> out, const sf::ClientProcessId &client_pid, sf::CopyHandle &&process), (out, client_pid, std::move(process)))

AMS_SF_DEFINE_MITM_INTERFACE(ams::mitm::applet, IAmOeMitm, AMS_AM_OE_MITM_INTERFACE_INFO, 0x2AB1E152)

namespace ams::mitm::applet {

    class AmCreatorMitm : public sf::MitmServiceImplBase {
        public:
            using MitmServiceImplBase::MitmServiceImplBase;
            static bool ShouldMitm(const sm::MitmProcessInfo &) { return true; }
        public:
            Result CreateLibraryApplet(u32 applet_id, u32 mode);
    };
    static_assert(IsIAmCreatorMitm<AmCreatorMitm>);

    class AmProxyMitm : public sf::MitmServiceImplBase {
        public:
            using MitmServiceImplBase::MitmServiceImplBase;
            static bool ShouldMitm(const sm::MitmProcessInfo &) { return true; }
        public:
            Result GetLibraryAppletCreator(sf::Out<sf::SharedPointer<IAmCreatorMitm>> out);
    };
    static_assert(IsIAmProxyMitm<AmProxyMitm>);

    class AmOeMitm : public sf::MitmServiceImplBase {
        public:
            using MitmServiceImplBase::MitmServiceImplBase;
            static bool ShouldMitm(const sm::MitmProcessInfo &client_info) {
                return ncm::IsApplicationId(client_info.program_id) && !client_info.override_status.IsHbl();
            }
        public:
            Result OpenApplicationProxy(sf::Out<sf::SharedPointer<IAmProxyMitm>> out, const sf::ClientProcessId &client_pid, sf::CopyHandle &&process);
    };
    static_assert(IsIAmOeMitm<AmOeMitm>);

    /* main.cpp: register appletOE's mitm on its own server (domains) */
    void StartAmMitm();

}
