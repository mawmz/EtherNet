#include <ethernet/core/UpdatableModule.hpp>
#include <ethernet/Notifications.hpp>
#include <ethernet/NotificationText.hpp>
#include <engine/xc2/ui/UIObjectAcc.hpp>
#include <cstddef>

namespace ethernet {
namespace {
using notification::Request;

// XC2 2.1.0 DataParamAcc owns an IDataObj. Use its native constructors and
// destructor rather than retaining an actor pointer across popup delivery.
// setupObjHandle writes the handle at +0x10 (stp at ELF 0x2d5ae8),
// even though the numeric constructor/destructor only access +0 and +8.
struct DataParam { void* vtable{}; void* data{}; gf::GF_OBJ_HANDLE* handle{}; };
static_assert(sizeof(DataParam) == 0x18 && offsetof(DataParam,handle) == 0x10);
struct Event {
    std::uint16_t id;
    std::uint16_t reserved;
    std::uint32_t flags;
    Request request;
};
static_assert(sizeof(Event) == 0x18 && offsetof(Event,request) == 8);
static_assert(sizeof(ui::UIObjectAcc) == 0x18 && sizeof(ui::UIStr) == 0x18);

void (*DataFromActor)(DataParam*,gf::GF_OBJ_HANDLE*){};
void (*DataFromId)(DataParam*,unsigned,unsigned,unsigned){};
void (*DestroyData)(DataParam*){};
bool (*ValidData)(const DataParam*){};
unsigned (*ActorType)(const DataParam*){};
unsigned (*ActorId)(const DataParam*){};
const char* (*ActorName)(const DataParam*){};
unsigned (*OpenPopup)(const Request*){};
void (*DestroyAccess)(ui::UIObjectAcc*){};
bool Installed{};

struct ScopedData : DataParam {
    explicit ScopedData(gf::GF_OBJ_HANDLE* actor) { DataFromActor(this,actor); }
    ScopedData(unsigned type, unsigned id) { DataFromId(this,type,id,0); }
    ~ScopedData() { DestroyData(this); }
    ScopedData(const ScopedData&) = delete;
    ScopedData& operator=(const ScopedData&) = delete;
};

void SetPopupText(unsigned root, const char* child, const char* text) {
    ui::UIObjectAcc access(root,child);
    if (access.uiObject) {
        // Allocated UIStr is deep-copied by native UIStr::copy in setText.
        // A non-owning reference would leave the widget pointing into a local
        // std::string after this function returns.
        ui::UIStr value(text,true);
        access.setText(value);
        value.release();
    }
    DestroyAccess(&access);
}

constexpr const char* ListenerSymbol =
    "_ZN2gf14GfMenuObjPopup12MainListener11reciveEventERKN2ui9EventDataEj";

struct PopupListener : skylaunch::hook::Trampoline<PopupListener> {
    static void Hook(void* listener, const Event& event, unsigned objectId) {
        if (event.id != 0x21 || !notification::IsRespawnRequest(event.request)) {
            Orig(listener,event,objectId);
            return;
        }

        const ScopedData actor(event.request.actorType,event.request.actorId);
        const char* name = ValidData(&actor) ? ActorName(&actor) : nullptr;
        const auto body = FormatNotificationText(NotificationTextId::PlayerRespawning,
            name ? name : "");

        // Translate only our reserved text ID, at delivery (not at enqueue).
        // Retail still consumes its queue and configures the System Update
        // layout, styles, animation, notification sound and dismissal normally.
        // Our text is set synchronously before this UI event returns to drawing.
        auto retailEvent = event;
        retailEvent.request = notification::RetailPresentation;
        Orig(listener,retailEvent,objectId);
        SetPopupText(objectId,"TXT_index",
            NotificationText(NotificationTextId::MultiplayerTitle).data());
        SetPopupText(objectId,"TXT_text@08",body.c_str());
    }
};

template<class T> bool Resolve(T& target, const char* symbol) {
    const auto address = skylaunch::hook::detail::ResolveSymbolBase(symbol);
    if (!address || address == skylaunch::hook::INVALID_FUNCTION_PTR) {
        ethernet::core::g_Logger->LogError("EtherNet notifications: missing {}",symbol);
        return false;
    }
    target = reinterpret_cast<T>(address);
    return true;
}

struct Notifications : ethernet::core::UpdatableModule {
    void Initialize() override {
        UpdatableModule::Initialize();
        if (ethernet::core::version::RuntimeGame() != ethernet::core::version::GameType::BF2) return;
        std::uintptr_t listener{};
        if (!Resolve(DataFromActor,"_ZN2gf12DataParamAccC1EPNS_13GF_OBJ_HANDLEE") ||
            !Resolve(DataFromId,"_ZN2gf12DataParamAccC1EjjNS_11GimmickTypeE") ||
            !Resolve(DestroyData,"_ZN2gf12DataParamAccD1Ev") ||
            !Resolve(ValidData,"_ZNK2gf12DataParamAcc7isValidEv") ||
            !Resolve(ActorType,"_ZNK2gf12DataParamAcc10getObjTypeEv") ||
            !Resolve(ActorId,"_ZNK2gf12DataParamAcc9getBdatIdEv") ||
            !Resolve(ActorName,"_ZNK2gf12DataParamAcc7getNameEv") ||
            !Resolve(OpenPopup,"_ZN2gf14GfMenuObjPopup4openERKNS0_9OpenParamE") ||
            !Resolve(DestroyAccess,"_ZN2ui11UIObjectAccD1Ev")) return;
        listener = skylaunch::hook::detail::ResolveSymbolBase(ListenerSymbol);
        if (!listener || listener == skylaunch::hook::INVALID_FUNCTION_PTR) return;
        PopupListener::HookAt(listener);
        Installed = PopupListener::HasApplied();
        ethernet::core::g_Logger->LogInfo("EtherNet native multiplayer notifications: {}",
            Installed ? "installed" : "failed");
    }
};
ETHERNET_REGISTER_MODULE(Notifications);
}

bool NotifyFieldRespawn(gf::GF_OBJ_HANDLE* handle) {
    if (!Installed || !handle || handle == reinterpret_cast<gf::GF_OBJ_HANDLE*>(-1)) return false;
    const ScopedData actor(handle);
    if (!ValidData(&actor)) return false;
    const auto request = notification::RespawnRequest(ActorType(&actor),ActorId(&actor));
    return OpenPopup(&request) != 0;
}
}
