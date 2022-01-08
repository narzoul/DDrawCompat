#pragma once

#include <Windows.h>

struct RectF
{
	float left;
	float top;
	float right;
	float bottom;
};

namespace Rect
{
	RectF toRectF(const RECT& rect);
	void transform(RECT& rect, const RECT& srcView, const RECT& dstView);
	void transform(RectF& rect, const RECT& srcView, const RECT& dstView);
}
