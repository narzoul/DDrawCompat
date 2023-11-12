#pragma once

#include <array>

#include <intrin.h>

template <int min, int max>
class BitSet
{
public:
	BitSet() : m_bits{}
	{
	}

	template <typename Func>
	void forEach(Func func)
	{
		int value = min;
		unsigned long index = 0;
		for (unsigned bits : m_bits)
		{
			while (0 != bits)
			{
				_BitScanForward(&index, bits);
				func(value + index);
				bits &= ~(1 << index);
			}
			value += 32;
		}
	}

	int getMax() const { return max; }
	int getMin() const { return min; }

	void reset()
	{
		m_bits.fill(0);
	}

	void reset(int value)
	{
		const unsigned index = value - min;
		m_bits[index / 32] &= ~(1 << (index % 32));
	}

	void set(int value)
	{
		const unsigned index = value - min;
		m_bits[index / 32] |= 1 << (index % 32);
	}

	bool test(int value)
	{
		const unsigned index = value - min;
		return m_bits[index / 32] & (1 << (index % 32));
	}

private:
	std::array<unsigned, (max - min + 32) / 32> m_bits;
};
