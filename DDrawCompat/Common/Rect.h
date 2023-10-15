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
	inline SIZE getSize(const RECT& rect)
	{
		return { rect.right - rect.left, rect.bottom - rect.top };
	}

	RectF toRectF(const RECT& rect);
	bool isEqualSize(const RECT& rect1, const RECT& rect2);
	void transform(RECT& rect, const RECT& srcView, const RECT& dstView);
	void transform(RectF& rect, const RECT& srcView, const RECT& dstView);
}
