//
// Created by block on 7/21/2022.
//

#pragma once

#include <engine/xc2/gf/SaveGame.hpp>

namespace tl {

	class TitleMain {
	   public:
		unsigned int getChapterIdFromSaveData();
		void* getSaveBuffer();
		static void returnTitle(gf::SAVESLOT slot);
		void playTitleEvent(uint event_id);

		bool isValidAocVersion() const;
	};

} // namespace tl
