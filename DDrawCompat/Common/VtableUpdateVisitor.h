#pragma once

template <typename Vtable>
class VtableUpdateVisitor
{
public:
	VtableUpdateVisitor(const Vtable& preHookOrigVtable, const Vtable& postHookOrigVtable, Vtable& vtable)
		: m_preHookOrigVtable(preHookOrigVtable)
		, m_postHookOrigVtable(postHookOrigVtable)
		, m_vtable(vtable)
	{
	}

	template <typename MemberDataPtr, MemberDataPtr ptr>
	void visit(const char* /*funcName*/)
	{
		if (m_preHookOrigVtable.*ptr == m_vtable.*ptr)
		{
			m_vtable.*ptr = m_postHookOrigVtable.*ptr;
		}
	}

private:
	const Vtable& m_preHookOrigVtable;
	const Vtable& m_postHookOrigVtable;
	Vtable& m_vtable;
};
