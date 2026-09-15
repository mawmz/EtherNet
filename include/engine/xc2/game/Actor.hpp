// Created by block on 2/21/2023.

#pragma once

#include <engine/xc2/fw/Document.hpp>
#include <engine/xc2/mm/mtl/RTTI.hpp>

#if ETHERNET_NEW_ENGINE
namespace game {

	// move these out of here?

	class BehaviorComponent {
	   public:
		virtual mm::mtl::RTTI* getRTTI() const;
	};

	class BehaviorPc : BehaviorComponent {
	   public:
		static mm::mtl::RTTI m_rtti;
	};

	class ActorAccessor {
	   public:
		ActorAccessor(const fw::Document& doc, unsigned int objHandle);

		void* getPropertyComponent() const;
		BehaviorComponent* getBehaviorComponent() const;

		bool isValid() const;
	};

} // namespace game
#endif