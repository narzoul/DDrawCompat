#pragma once

#include <Config/Settings/ThreadPriorityBoost.h>

namespace Config
{
	const unsigned delayedFlipModeTimeout = 200;
	const unsigned evictionTimeout = 200;
	const unsigned maxPaletteUpdatesPerMs = 5;

	extern Settings::ThreadPriorityBoost threadPriorityBoost;
}
