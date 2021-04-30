#pragma once

#include <Config/Settings/CpuAffinity.h>
#include <Config/Settings/ThreadPriorityBoost.h>

namespace Config
{
	const unsigned delayedFlipModeTimeout = 200;
	const unsigned evictionTimeout = 200;
	const unsigned maxPaletteUpdatesPerMs = 5;

	extern Settings::CpuAffinity cpuAffinity;
	extern Settings::ThreadPriorityBoost threadPriorityBoost;
}
