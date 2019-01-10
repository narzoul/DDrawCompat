#pragma once

#include <map>
#include <string>
#include <vector>
#include <typeinfo>

template <typename Vtable>
class FuncNameVisitor
{
public:
	template <typename MemberDataPtr, MemberDataPtr ptr>
	static void visit(const char* funcName)
	{
		s_funcNames[getKey<MemberDataPtr, ptr>()] = s_vtableTypeName + "::" + funcName;
	}

	template <typename MemberDataPtr, MemberDataPtr ptr>
	static const char* getFuncName()
	{
		return s_funcNames[getKey<MemberDataPtr, ptr>()].c_str();
	}

private:
	template <typename MemberDataPtr, MemberDataPtr ptr>
	static std::vector<unsigned char> getKey()
	{
		MemberDataPtr mp = ptr;
		unsigned char* p = reinterpret_cast<unsigned char*>(&mp);
		return std::vector<unsigned char>(p, p + sizeof(mp));
	}

	static std::string getVtableTypeName()
	{
		std::string name = typeid(Vtable).name();
		if (0 == name.find("struct "))
		{
			name = name.substr(name.find(" ") + 1);
		}
		return name;
	}

	static std::string s_vtableTypeName;
	static std::map<std::vector<unsigned char>, std::string> s_funcNames;
};

template <typename Vtable>
std::string FuncNameVisitor<Vtable>::s_vtableTypeName(getVtableTypeName());

template <typename Vtable>
std::map<std::vector<unsigned char>, std::string> FuncNameVisitor<Vtable>::s_funcNames;
