// Created by block on 11/11/23.

#pragma once

#include <ethernet/core/Utils.hpp>

#include <engine/xc2/game/Data.hpp>
#include <engine/xc2/mm/mtl/RTTI.hpp>
#include <engine/xc2/mpgui/Mpgui.hpp>

#if ETHERNET_CODENAME(bfsw)

namespace fw {
	class Document;
}

namespace game {

	class Manager {
	   public:
		fw::Document* document;

		virtual void buildMpguiMessage(mpgui::MpguiContext* context);
		virtual mm::mtl::RTTI* getRTTI() const;
		virtual char* getName();
		virtual void initialize();
		virtual void finalize();
		virtual void update(const fw::UpdateInfo& updateInfo);
		virtual void postUpdate(const fw::UpdateInfo& updateInfo);
		virtual void postCameraUpdate(const fw::UpdateInfo& updateInfo);
		virtual void setInvalidTypeIndex();
	};

	class DataManager : public Manager {
	   public:
		INSERT_PADDING_BYTES(0x44);
		DataMenuSys dataMenuSys;
		INSERT_PADDING_BYTES(0x8);
		DataGame dataGame;

		// unfinished
		DataBdat dataBdat;
		DataParam dataParam;
		DataAI dataAI;
		DataSevFace dataSevFace;
		DataSpEff dataSpEff;
	};

} // namespace game
#endif