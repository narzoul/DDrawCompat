#include <Common/Rect.h>

namespace
{
	template <typename Rect>
	void transform(Rect& rect, const RECT& srcView, const RECT& dstView)
	{
		const LONG srcWidth = srcView.right - srcView.left;
		const LONG srcHeight = srcView.bottom - srcView.top;
		const LONG dstWidth = dstView.right - dstView.left;
		const LONG dstHeight = dstView.bottom - dstView.top;

		rect = {
			(rect.left - srcView.left) * dstWidth / srcWidth + dstView.left,
			(rect.top - srcView.top) * dstHeight / srcHeight + dstView.top,
			(rect.right - srcView.left) * dstWidth / srcWidth + dstView.left,
			(rect.bottom - srcView.top) * dstHeight / srcHeight + dstView.top
		};
	}
}

namespace Rect
{
	bool isEqualSize(const RECT& rect1, const RECT& rect2)
	{
		return rect1.right - rect1.left == rect2.right - rect2.left &&
			rect1.bottom - rect1.top == rect2.bottom - rect2.top;
	}

	RectF toRectF(const RECT& rect)
	{
		return {
			static_cast<float>(rect.left),
			static_cast<float>(rect.top),
			static_cast<float>(rect.right),
			static_cast<float>(rect.bottom)
		};
	}

	void transform(RECT& rect, const RECT& srcView, const RECT& dstView)
	{
		::transform(rect, srcView, dstView);
	}

	void transform(RectF& rect, const RECT& srcView, const RECT& dstView)
	{
		::transform(rect, srcView, dstView);
	}
}
