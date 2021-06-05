#pragma once

#include <tuple>
#include <type_traits>

#include <Windows.h>

template <typename T>
std::enable_if_t<std::is_class_v<T> && std::is_trivial_v<T>, bool> operator==(const T& left, const T& right)
{
	return toTuple(left) == toTuple(right);
}

template <typename T>
std::enable_if_t<std::is_class_v<T> && std::is_trivial_v<T>, bool> operator<(const T& left, const T& right)
{
	return toTuple(left) < toTuple(right);
}

inline auto toTuple(const LUID& luid)
{
	return std::make_tuple(luid.LowPart, luid.HighPart);
}
