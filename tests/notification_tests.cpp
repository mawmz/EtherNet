#include <ethernet/NotificationText.hpp>
#include <array>
#include <cassert>
#include <cstdio>

int main() {
    using namespace ethernet;
    using namespace ethernet::notification;
    assert(NotificationText(NotificationTextId::MultiplayerTitle) == "Multiplayer");
    assert(FormatNotificationText(NotificationTextId::PlayerRespawning,"Nia") == "Nia is respawning...");
    assert(FormatNotificationText(NotificationTextId::PlayerRespawning,"Rex") == "Rex is respawning...");
    assert(FormatNotificationText(NotificationTextId::PlayerRespawning,"") == "Player is respawning...");
    assert(FormatNotificationText(static_cast<NotificationTextId>(0),"Nia").empty());

    // Mixed native queue: only the reserved EtherNet ID is translated.
    const std::array queue{RetailPresentation,RespawnRequest(3,1),
        Request{9,22,0,0},RespawnRequest(3,2)};
    assert(!IsRespawnRequest(queue[0]) && !IsRespawnRequest(queue[2]));
    assert(IsRespawnRequest(queue[1]) && IsRespawnRequest(queue[3]));
    assert(queue[1].actorId == 1 && queue[3].actorId == 2);
    auto translated = queue[1];
    translated = RetailPresentation;
    assert(translated.text == 22 && translated.actorType == 0 && translated.actorId == 0);
    assert(IsRespawnRequest(queue[1])); // Delivery translation does not mutate queued payload.
    assert(!IsRespawnRequest({9,queue[1].text,3,1}));
    std::puts("PASS: notification text IDs, character substitution, native request isolation.");
}
