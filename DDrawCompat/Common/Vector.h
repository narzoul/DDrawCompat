#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include <Windows.h>

#include <Common/Log.h>

template <typename Elem, std::size_t size>
class VectorRepresentation
{
private:
	Elem values[size];
};

template <typename Elem>
class VectorRepresentation<Elem, 2>
{
public:
	Elem x;
	Elem y;
};

template <typename Elem, std::size_t size>
class Vector : public VectorRepresentation<Elem, size>
{
public:
	template <typename... Values>
	Vector(Values... v)
	{
		asArray() = { static_cast<Elem>(v)... };
	}

	Vector(SIZE s) : Vector(s.cx, s.cy)
	{
	}

	template <typename OtherElem>
	Vector(const Vector<OtherElem, size>& other)
	{
		for (std::size_t i = 0; i < size; ++i)
		{
			(*this)[i] = static_cast<Elem>(other[i]);
		}
	}

	template <typename Value>
	Vector(Value v)
	{
		asArray().fill(static_cast<Elem>(v));
	}

	Elem& operator[](std::size_t index)
	{
		return asArray()[index];
	}

	const Elem& operator[](std::size_t index) const
	{
		return const_cast<Vector*>(this)->asArray()[index];
	}

#define DEFINE_COMPOUND_ASSIGNMENT_OPERATOR(op) \
	Vector& operator##op##=(const Vector& other) \
	{ \
		return *this = *this op other; \
	}

	DEFINE_COMPOUND_ASSIGNMENT_OPERATOR(+);
	DEFINE_COMPOUND_ASSIGNMENT_OPERATOR(-);
	DEFINE_COMPOUND_ASSIGNMENT_OPERATOR(*);
	DEFINE_COMPOUND_ASSIGNMENT_OPERATOR(/);

#undef DEFINE_COMPOUND_ASSIGNMENT_OPERATOR

private:
	std::array<Elem, size>& asArray()
	{
		return *reinterpret_cast<std::array<Elem, size>*>(this);
	}
};

template <typename Elem, std::size_t size, typename Operator>
Vector<Elem, size> binaryOperation(Operator op, const Vector<Elem, size>& lhs, const Vector<Elem, size>& rhs)
{
	Vector<Elem, size> result;
	for (std::size_t i = 0; i < size; ++i)
	{
		result[i] = op(lhs[i], rhs[i]);
	}
	return result;
}

template <typename Elem, std::size_t size, typename Operator>
Vector<Elem, size> binaryOperation(Operator op, Elem lhs, const Vector<Elem, size>& rhs)
{
	return binaryOperation(op, Vector<Elem, size>(lhs), rhs);
}

template <typename Elem, std::size_t size, typename Operator>
Vector<Elem, size> binaryOperation(Operator op, const Vector<Elem, size>& lhs, Elem rhs)
{
	return binaryOperation(op, lhs, Vector<Elem, size>(rhs));
}

template <typename Elem, std::size_t size, typename Operator>
Vector<Elem, size> unaryOperation(Operator op, const Vector<Elem, size>& vec)
{
	Vector<Elem, size> result;
	for (std::size_t i = 0; i < size; ++i)
	{
		result[i] = op(vec[i]);
	}
	return result;
}

#define DEFINE_VECTOR_BINARY_OPERATOR(name, ...) \
	template <typename Elem, std::size_t size> \
	Vector<Elem, size> name(const Vector<Elem, size>& lhs, const Vector<Elem, size>& rhs) \
	{ \
		return binaryOperation([](Elem x, Elem y) { return __VA_ARGS__; }, lhs, rhs); \
	} \
	\
	template <typename Elem, std::size_t size> \
	Vector<Elem, size> name(Elem lhs, const Vector<Elem, size>& rhs) \
	{ \
		return binaryOperation([](Elem x, Elem y) { return __VA_ARGS__; }, lhs, rhs); \
	} \
	\
	template <typename Elem, std::size_t size> \
	Vector<Elem, size> name(const Vector<Elem, size>& lhs, Elem rhs) \
	{ \
		return binaryOperation([](Elem x, Elem y) { return __VA_ARGS__; }, lhs, rhs); \
	}

#define DEFINE_VECTOR_UNARY_OPERATOR(name, ...) \
	template <typename Elem, std::size_t size> \
	Vector<Elem, size> name(const Vector<Elem, size>& vec) \
	{ \
		return unaryOperation([](Elem x) { return __VA_ARGS__; }, vec); \
	}

#define DEFINE_VECTOR_BUILTIN_BINARY_OPERATOR(op) DEFINE_VECTOR_BINARY_OPERATOR(operator ## op, x op y)
#define DEFINE_VECTOR_STD_BINARY_OPERATOR(op) DEFINE_VECTOR_BINARY_OPERATOR(op, std::op(x, y))
#define DEFINE_VECTOR_STD_UNARY_OPERATOR(op) DEFINE_VECTOR_UNARY_OPERATOR(op, std::op(x))

DEFINE_VECTOR_BUILTIN_BINARY_OPERATOR(+);
DEFINE_VECTOR_BUILTIN_BINARY_OPERATOR(-);
DEFINE_VECTOR_BUILTIN_BINARY_OPERATOR(*);
DEFINE_VECTOR_BUILTIN_BINARY_OPERATOR(/);

DEFINE_VECTOR_STD_BINARY_OPERATOR(max);
DEFINE_VECTOR_STD_BINARY_OPERATOR(min);

DEFINE_VECTOR_STD_UNARY_OPERATOR(ceil);
DEFINE_VECTOR_STD_UNARY_OPERATOR(floor);

#undef DEFINE_VECTOR_BINARY_OPERATOR
#undef DEFINE_VECTOR_UNARY_OPERATOR
#undef DEFINE_VECTOR_BUILTIN_BINARY_OPERATOR
#undef DEFINE_VECTOR_STD_BINARY_OPERATOR
#undef DEFINE_VECTOR_STD_UNARY_OPERATOR

template <typename Elem, std::size_t size>
Elem dot(const Vector<Elem, size>& lhs, const Vector<Elem, size>& rhs)
{
	Elem result = 0;
	for (std::size_t i = 0; i < size; ++i)
	{
		result += lhs[i] * rhs[i];
	}
	return result;
}

template <typename Elem, std::size_t size>
std::ostream& operator<<(std::ostream& os, const Vector<Elem, size>& vec)
{
	Compat::LogStruct log(os);
	for (std::size_t i = 0; i < size; ++i)
	{
		log << vec[i];
	}
	return os;
}

typedef Vector<float, 2> Float2;
typedef Vector<int, 2> Int2;
