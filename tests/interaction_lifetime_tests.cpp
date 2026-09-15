#include <ethernet/InteractionLifetime.hpp>
#include <cassert>

int main() {
    ethernet::InteractionLifetime owner;
    // Entry precedes the native stop: do not lose the pending initiator.
    assert(!owner.HasFinished(true));
    assert(!owner.HasFinished(true));
    assert(!owner.HasFinished(false));
    assert(!owner.HasFinished(false));
    // Missing end/leave callbacks must not retain ownership after field resume.
    assert(owner.HasFinished(true));
    // A new owner starts clean, including alternating P1/P2 interactions.
    for (int i = 0; i < 4; ++i) {
        owner = {};
        assert(!owner.HasFinished(true));
        assert(!owner.HasFinished(false));
        assert(owner.HasFinished(true));
    }
}
