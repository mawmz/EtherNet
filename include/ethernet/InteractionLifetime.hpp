#pragma once

namespace ethernet {
// StateFieldTalk entry can precede the native sequence's player pause. A
// resumed field is a release only after this interaction actually stopped it.
struct InteractionLifetime {
    bool observedStop{};

    bool HasFinished(bool controlFree) {
        if (!controlFree) observedStop = true;
        return controlFree && observedStop;
    }
};
}
