// Created by block on 6/18/23.

#pragma once

#include <ethernet/core/Utils.hpp>

#include <engine/xc2/game/Controllers.hpp>
#include <engine/xc2/game/Data.hpp>
#include <engine/xc2/game/Managers.hpp>
#include <engine/xc2/game/Sequence.hpp>
#include <engine/xc2/fw/Document.hpp>

#if ETHERNET_CODENAME(bfsw)
namespace game {

	class DocAccessor {
	   public:
		DataManager* getDataManager() const;
		void* getPartyManager();
		SeqManager* getSeqManager() const;

		// NOT A REAL FUNCTION
		static inline DocAccessor* GetFromEtherNetDocument() {
			return reinterpret_cast<game::DocAccessor*>(&ethernet::core::DocumentPtr);
		}
	};

}
#endif