//
// Created by block on 1/19/2023.
//

#pragma once

#include <engine/xc2/mm/MathTypes.hpp>
#include <nn/hid.hpp>

namespace ethernet::core {

	struct HidInput {
		struct State {
			std::uint64_t Buttons;
			glm::vec2 LAxis;
			glm::vec2 RAxis;
		};

		State stateCur {};
		State statePrev {};

		int padId {};
		bool padConnected {};

		constexpr HidInput(int id)
			: padId(id) {

		}

		void Poll();

		static HidInput* GetPlayer(int player);

		/*
		 * Gets the controller used for the debug input (like the Menu)
		 * Uses the last numbered controller if it is connected, otherwise P1.
		 */
		static HidInput* GetDebugInput();

		/**
		 * \defgroup input Input functions
		 * Standard style input functions. Can take any kind of integer to check.
		 * Strict functions ensure that only the specified combo is allowed to be held down.
		 * @{
		 */

		template<class T>
		inline bool InputHeld(T combo) {
			return bitMask(static_cast<T>(stateCur.Buttons), combo);
		}

		template<class T>
		inline bool InputUp(T combo) {
			return !bitMask(static_cast<T>(stateCur.Buttons), combo) && bitMask(static_cast<T>(statePrev.Buttons), combo);
		}

		template<class T>
		inline bool InputDown(T combo) {
			return bitMask(static_cast<T>(stateCur.Buttons), combo) && !bitMask(static_cast<T>(statePrev.Buttons), combo);
		}

		template<class T>
		inline bool InputHeldStrict(T combo) {
			return bitMaskStrict(static_cast<T>(stateCur.Buttons & 0xFFFF), combo);
		}

		template<class T>
		inline bool InputUpStrict(T combo) {
			return !bitMaskStrict(static_cast<T>(stateCur.Buttons & 0xFFFF), combo) && bitMaskStrict(static_cast<T>(statePrev.Buttons & 0xFFFF), combo);
		}

		template<class T>
		inline bool InputDownStrict(T combo) {
			return bitMaskStrict(static_cast<T>(stateCur.Buttons & 0xFFFF), combo) && !bitMaskStrict(static_cast<T>(statePrev.Buttons & 0xFFFF), combo);
		}

		/** @} */
	};

	using enum nn::hid::NpadButton;
#define NPADBIT(n) (1UL << ((std::uint64_t)n))

	enum class Keybind : std::uint64_t {
		// Menu navigation is handled by imgui-xeno; only its opener lives here.
		MENU_TOGGLE = NPADBIT(L) | NPADBIT(R) | NPADBIT(ZL) | NPADBIT(ZR),
	};

#undef NPADBIT

}; // namespace ethernet::core
