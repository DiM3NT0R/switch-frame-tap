/*
 * Switch Frame Tap memory donor (v0.7.10).
 *
 * The browser applets (manuals, news, eShop) need nearly the whole Applet
 * memory pool: any sysmodule resident there while idle makes them abort with
 * 2210-1024. Streaming needs ~42 MB that only the Applet pool has room for.
 * So the module lives elsewhere and launches this program only while a
 * viewer is connected: it takes the memory from the Applet pool and lends it
 * to the module as transfer memory, then waits. When the module gives it back
 * (unmaps it), this frees it and exits.
 */
#include <switch.h>

u32 __nx_applet_type = AppletType_None;
u32 __nx_fs_num_sessions = 1;

/* libnx's own heap stays tiny and static, so svcSetHeapSize is ours alone */
#define INNER_HEAP_SIZE 0x4000
static char g_inner_heap[INNER_HEAP_SIZE];

void __libnx_initheap(void) {
    extern char *fake_heap_start;
    extern char *fake_heap_end;
    fake_heap_start = g_inner_heap;
    fake_heap_end   = g_inner_heap + sizeof(g_inner_heap);
}

void __appInit(void) {
    if (R_FAILED(smInitialize())) { diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_SM)); }
}

void __appExit(void) { smExit(); }

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    Service ctl;
    if (R_FAILED(smGetService(&ctl, "sftap"))) { return 1; }
    smExit();   /* sm only for this */

    /* the largest the Applet pool grants (2 MB steps, the heap unit) */
    static const u32 sizes_mb[] = { 42, 38, 34, 30, 26, 22, 18, 14, 12 };
    void *addr = NULL;
    u64 size = 0;
    for (u32 i = 0; i < sizeof(sizes_mb) / sizeof(sizes_mb[0]); ++i) {
        if (R_SUCCEEDED(svcSetHeapSize(&addr, (u64)sizes_mb[i] << 20))) { size = (u64)sizes_mb[i] << 20; break; }
    }

    Handle tmem = INVALID_HANDLE;
    Result rc = size != 0 ? svcCreateTransferMemory(&tmem, addr, size, Perm_None) : MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
    /* Donate (cmd 10, the handle copied), or DonateFailed (cmd 11) so the
     * module stops waiting and logs why */
    if (R_SUCCEEDED(rc)) {
        serviceDispatchIn(&ctl, 10, size, .in_num_handles = 1, .in_handles = { tmem });
        svcCloseHandle(tmem);   /* the module holds its own copy; the memory stays lent */
    } else {
        const u32 urc = rc;
        serviceDispatchIn(&ctl, 11, urc);
    }
    serviceClose(&ctl);

    /* Hold it while it is lent. Shrinking the heap is refused while the pages
     * are transferred; once the module unmaps and closes its handle, the next
     * try succeeds and the memory is back in the Applet pool within ~5 ms -
     * pm:shell's TerminateProcess took ~200 ms, too slow for a browser applet
     * that starts while we stream. The module still terminates us as a
     * backstop. */
    if (size != 0) {
        while (R_FAILED(svcSetHeapSize(&addr, 0))) { svcSleepThread(5000000ull); }
    }
    return 0;
}
