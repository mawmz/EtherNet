// Created by block on 5/25/23.

#pragma once

#include <engine/xc2/ml/Scene.hpp>
#include <engine/xc2/fw/Document.hpp>

namespace fw {

#if ETHERNET_NEW_ENGINE
	class SceneManager {
	   public:
		void* vtable;
		ml::Scn* scene;

		void initialize(fw::Document& doc);
	};
#endif

	class EffectManager {
	   public:
		static void debugOutputString(const char* str);
		static void warnOutputString(const char* str);
	};

}