// Created by block on 1/11/24.

#pragma once

#include <ethernet/core/Utils.hpp>

#include <engine/xc2/gf/EventModel.hpp>
#include <engine/xc2/gf/Object.hpp>
#include <engine/xc2/mm/mtl/FixStr.hpp>

#if ETHERNET_OLD_ENGINE

namespace event {

	enum class MODEL_TYPE {
		Driver = 1,
		Blade,
		Npc,
		Enemy,
		BladeWeapon
	};

	class CharaObj { // size 0x3060 (12384)
	   public:
		INSERT_PADDING_BYTES(0xCA8);
		mm::mtl::FixStr<64> objectName;
		INSERT_PADDING_BYTES(0x8);
		mm::mtl::FixStr<64> modelPath;
		unsigned int bdatIndex;
		unsigned int unk2;
		gf::GF_OBJ_HANDLE* objHandle;
		gf::GfEvtModel* evtModel;
		INSERT_PADDING_BYTES(8984);

		void initModel();
		void initModelStream();
	};

	class ModelObj {
	   public:
		struct OPTION {
			int unk1;
			int unk2;
		};

		gf::GfEvtModel* setupModel(MODEL_TYPE modelType, unsigned int id, const OPTION&);

		gf::GF_OBJ_HANDLE* getObjHandle() const {
			const auto bytes = reinterpret_cast<const std::uint8_t*>(this);
			return *reinterpret_cast<gf::GF_OBJ_HANDLE* const*>(bytes + 0x120);
		}

		unsigned int getPendingResourceMask() const {
			const auto bytes = reinterpret_cast<const std::uint8_t*>(this);
			return *reinterpret_cast<const unsigned int*>(bytes + 0x8) & 0xC;
		}
	};

	class ModelManager {
	   public:
		ModelManager();

		ModelObj* createModel(const char* resourcePath, bool stream, unsigned int layer, int owner);
		void destroyModel(ModelObj* model, int owner);
	};

} // namespace event

#endif
