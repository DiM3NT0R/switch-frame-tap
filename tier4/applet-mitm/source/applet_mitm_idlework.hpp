/* v0.7.12 (DiM3NT0R, PR #12): the pipelined stream's "collect the previous
 * encode" callback, polled whenever the capture side is idle. */
#pragma once

namespace ams::mitm::applet {

    // Single-worker callback: poll a pending encode while capture is idle.
    // True means it was collected; never invoke that callback again this capture.
    struct EncodeIdleWork {
        bool (*fn)(void *) = nullptr;
        void *ctx = nullptr;

        bool Poll() {
            if (fn != nullptr && fn(ctx)) {
                fn = nullptr;
                ctx = nullptr;
                return true;
            }
            return false;
        }
    };

}
