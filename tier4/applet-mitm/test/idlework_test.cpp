#include "applet_mitm_idlework.hpp"
#include <cassert>
#include <cstdio>

using ams::mitm::applet::EncodeIdleWork;

struct PendingEncode {
    int polls = 0;
    int sends = 0;
    bool ready = false;
    static bool Collect(void *ctx) {
        auto &job = *static_cast<PendingEncode *>(ctx);
        ++job.polls;
        if (!job.ready) return false;
        ++job.sends;
        return true;
    }
};

int main() {
    EncodeIdleWork work;
    assert(!work.Poll());
    PendingEncode job;
    work = {PendingEncode::Collect, &job};
    assert(!work.Poll() && job.polls == 1 && job.sends == 0);
    job.ready = true;
    assert(work.Poll() && job.sends == 1);
    assert(!work.Poll() && job.sends == 1 && job.polls == 2);
    assert(work.fn == nullptr && work.ctx == nullptr);
    work = {PendingEncode::Collect, &job};
    work = {};
    assert(!work.Poll() && job.sends == 1);
    puts("Idle encode polling: pending, ready, once-only, and cancellation OK");
}
