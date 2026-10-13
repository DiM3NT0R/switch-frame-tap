/*
 * v0.7.10: streaming memory lent by a helper program (tier4/donor).
 *
 * The browser applets need nearly the whole Applet pool (2210-1024 with any
 * sysmodule resident there), and streaming needs ~42 MB only that pool has.
 * While a viewer is connected, the module launches sft-donor (0100000000000C21)
 * through pm:shell; it takes the memory in the Applet pool and hands it over
 * as transfer memory (sftap command 10). Terminating the donor gives it back.
 */
#pragma once
#include <stratosphere.hpp>

namespace ams::mitm::applet {

    /* Launches the donor and maps its memory; true with the region on success.
     * Blocks up to a few seconds. Not for IPC handler threads. */
    bool DonorObtain(uintptr_t *out_addr, size_t *out_size);
    /* Unmaps the region and terminates the donor. */
    void DonorRelease();
    bool DonorActive();
    /* An applet that needs the pool is open (the browser: manuals, news,
     * eShop) - the donor must not take its memory now. out_mb: Applet group
     * memory in use. */
    bool AppletBusy(u64 *out_mb = nullptr);

    /* Set when an applet starts while the donor's memory is in use: the
     * stream stops and the memory goes back at once (the browser's big
     * allocation follows its start by ~0.5-3 s). Cleared by the next setup. */
    extern std::atomic<bool> g_applet_needs_memory;
    void StartAppletWatch();
    /* v0.7.10: am is about to start a browser applet (applet_mitm_am.cpp):
     * stop the stream and give the memory back first; no donor for a few
     * seconds after, so the applet gets its share. Blocks up to ~1.5 s. */
    void PrepareForApplet(u32 applet_id);
    bool AppletHoldActive();

    /* sftap commands 10 / 11, from the donor */
    void DonorAccept(u64 size, os::NativeHandle tmem);
    void DonorFailed(u32 rc);

}
