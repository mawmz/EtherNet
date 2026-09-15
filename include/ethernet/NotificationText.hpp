#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace ethernet {

// EtherNet IDs, not rows in the retail BDAT files.
enum class NotificationTextId : std::uint32_t {
    MultiplayerTitle = 0x45540001,
    PlayerRespawning = 0x45540002,
};

constexpr std::string_view NotificationText(NotificationTextId id) {
    switch (id) {
        case NotificationTextId::MultiplayerTitle: return "Multiplayer";
        case NotificationTextId::PlayerRespawning: return "{Character} is respawning...";
    }
    return {};
}

inline std::string FormatNotificationText(NotificationTextId id, std::string_view character) {
    std::string text(NotificationText(id));
    constexpr std::string_view placeholder = "{Character}";
    const auto at = text.find(placeholder);
    if (at != std::string::npos)
        text.replace(at, placeholder.size(), character.empty() ? "Player" : character);
    return text;
}

namespace notification {
// GfMenuObjPopup::OpenParam; copied by the native queue, never owns pointers.
struct Request {
    std::uint32_t kind;
    std::uint32_t text;
    std::uint32_t actorType;
    std::uint32_t actorId;
};
static_assert(sizeof(Request) == 0x10);

constexpr Request RespawnRequest(std::uint32_t actorType, std::uint32_t actorId) {
    return {10, static_cast<std::uint32_t>(NotificationTextId::PlayerRespawning), actorType, actorId};
}
constexpr bool IsRespawnRequest(const Request& request) {
    return request.kind == 10 &&
        request.text == static_cast<std::uint32_t>(NotificationTextId::PlayerRespawning);
}
// Native system notification row 22 supplies the existing layout and styles.
// This is a presentation request only: it does not unlock the Quest Log.
constexpr Request RetailPresentation{10, 22, 0, 0};
}
}
