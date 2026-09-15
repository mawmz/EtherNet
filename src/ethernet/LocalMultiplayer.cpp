#include <ethernet/core/UpdatableModule.hpp>

#include <cstddef>
#include <cstdint>

#include <skylaunch/hookng/Hooks.hpp>

#include <ethernet/core/Logger.hpp>
#include <engine/xc2/gf/Party.hpp>
#include <ethernet/LocalPlayers.hpp>

namespace ethernet::core {

#if ETHERNET_CODENAME(bf2)
	namespace {
		constexpr unsigned int SecondPartySlot = 1;
		constexpr unsigned int PlayerTwoPadIndex = 1;

		// XC2 2.1.0 GfComBehaviorPc layout recovered from
		// gf::GfComBehaviorPc::setup at main + 0x648130.
		constexpr std::size_t BehaviorObjectOffset = 0x08;
		constexpr std::size_t ObjectHandleOffset = 0xf8;
		constexpr std::size_t PadAgentIndexOffset = 0x08;

		struct ChangeControlModeMessage {
			std::uint64_t messageHeader = 0;
			std::uint32_t mode = 0;
		};

		using ChangeControlModeFn = void (*)(
			void* behavior,
			void* messageObject,
			ChangeControlModeMessage* message
		);

		constexpr const char* ChangeControlModeSymbol =
			"_ZN2gf15GfComBehaviorPc31procMsgPcFieldChangeControlModeEPN2fw13MessageObjectERNS_27MsgPcFieldChangeControlModeE";
		constexpr const char* PadAgentSetupSymbol =
			"_ZN2gf2pc8PadAgent5setupERNS_15GfComPropertyPcE";
		constexpr const char* BehaviorSetupSymbol =
			"_ZN2gf15GfComBehaviorPc5setupERKNS0_9SetupInfoE";

		ChangeControlModeFn ChangeControlMode = nullptr;
		bool ConfiguringPlayerTwo = false;
		gf::GF_OBJ_HANDLE* BoundHandle = nullptr;
		void* BoundObject = nullptr;
		std::uint64_t BindingGeneration = 0;

		bool IsSecondPartyMover(void* behavior) {
			if(behavior == nullptr)
				return false;

			auto* object = *reinterpret_cast<std::uint8_t**>(
				reinterpret_cast<std::uint8_t*>(behavior) + BehaviorObjectOffset
			);
			if(object == nullptr)
				return false;

			auto* objectHandle = *reinterpret_cast<gf::GF_OBJ_HANDLE**>(
				object + ObjectHandleOffset
			);
			auto* secondMover = gf::GfGameParty::getHandleMover(SecondPartySlot);
			return secondMover != nullptr
				&& secondMover != reinterpret_cast<gf::GF_OBJ_HANDLE*>(-1)
				&& objectHandle == secondMover;
		}

		struct BehaviorSetupHook : skylaunch::hook::Trampoline<BehaviorSetupHook> {
			static void Hook(void* behavior, const void* setupInfo) {
				Orig(behavior, setupInfo);

				if(!IsSecondPartyMover(behavior) || ChangeControlMode == nullptr)
					return;

				// Run XC2's complete AI -> player transition. Besides replacing
				// AIAgent, this sets pad-control movement parameters, rebuilds the
				// player-only field plugins, and resets the field HFSM state.
				const bool previousSetupState = ConfiguringPlayerTwo;
				ConfiguringPlayerTwo = true;
				ChangeControlModeMessage message {};
				ChangeControlMode(behavior, nullptr, &message);
				ConfiguringPlayerTwo = previousSetupState;

				// Observer-only binding for the shared camera; does not gate control.
				BoundHandle = gf::GfGameParty::getHandleMover(SecondPartySlot);
				BoundObject = gf::GfObjUtil::getObj(BoundHandle);
				++BindingGeneration;

				g_Logger->LogInfo(
					"XC2 multiplayer: party slot 2 now uses player movement on controller 2"
				);
			}
		};

		struct PadAgentSetupHook : skylaunch::hook::Trampoline<PadAgentSetupHook> {
			static void Hook(void* agent, void* property) {
				if(ConfiguringPlayerTwo && agent != nullptr) {
					*reinterpret_cast<unsigned int*>(
						reinterpret_cast<std::uint8_t*>(agent) + PadAgentIndexOffset
					) = PlayerTwoPadIndex;
				}
				Orig(agent, property);
			}
		};
	} // namespace
#endif

	struct LocalMultiplayer : public UpdatableModule {
		void Initialize() override {
			UpdatableModule::Initialize();
#if ETHERNET_CODENAME(bf2)
			if(version::RuntimeGame() != version::GameType::BF2) {
				g_Logger->LogWarning("EtherNet P2 requires XC2; leaving retail control.");
				return;
			}
			g_Logger->LogDebug(
				"Setting up XC2 local multiplayer (party slot 2 -> controller 2)..."
			);
			ChangeControlMode = skylaunch::hook::detail::ResolveSymbol<
				ChangeControlModeFn
			>(ChangeControlModeSymbol);
			if(!ChangeControlMode ||
				reinterpret_cast<std::uintptr_t>(ChangeControlMode)
				== skylaunch::hook::INVALID_FUNCTION_PTR
			) {
				ChangeControlMode = nullptr;
				g_Logger->LogError(
					"XC2 multiplayer: failed to resolve the native control-mode transition"
				);
				return;
			}
			for(const auto* symbol : {PadAgentSetupSymbol, BehaviorSetupSymbol}) {
				const auto address = skylaunch::hook::detail::ResolveSymbolBase(symbol);
				if(!address || address == skylaunch::hook::INVALID_FUNCTION_PTR) {
					g_Logger->LogError("XC2 multiplayer: missing native symbol {}", symbol);
					return;
				}
			}
			PadAgentSetupHook::HookAt(PadAgentSetupSymbol);
			BehaviorSetupHook::HookAt(BehaviorSetupSymbol);
			if(!PadAgentSetupHook::HasApplied() || !BehaviorSetupHook::HasApplied()) {
				g_Logger->LogError("XC2 multiplayer: native setup hook installation failed");
				return;
			}
#endif
		}
		void OnSceneTransition() override {
			// Keep weak identities for same-map warps; getObj checks replacements.
			++BindingGeneration;
		}
	};

#if ETHERNET_CODENAME(bf2)
	ETHERNET_REGISTER_MODULE(LocalMultiplayer);
#endif

} // namespace ethernet::core

namespace ethernet {
bool IsPlayerTwoBound(gf::GF_OBJ_HANDLE* handle) {
    return handle && handle == ethernet::core::BoundHandle && ethernet::core::BoundObject &&
        gf::GfObjUtil::getObj(handle) == ethernet::core::BoundObject;
}
std::uint64_t PlayerBindingGeneration() { return ethernet::core::BindingGeneration; }
}
