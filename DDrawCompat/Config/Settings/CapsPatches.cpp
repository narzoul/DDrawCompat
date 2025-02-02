#include <algorithm>
#include <map>
#include <sstream>
#include <type_traits>

#include <Config/Parser.h>
#include <Config/Settings/CapsPatches.h>
#include <Direct3d/Visitors/CapsVisitor.h>
#include <DDraw/Visitors/CapsVisitor.h>

#define VISIT_GUID(visit) \
	visit(Data1) \
	visit(Data2) \
	visit(Data3) \
	visit(Data4)

namespace
{
	struct Field;
	typedef std::map<std::string, Field> FieldMap;

	struct Field
	{
		UINT offset;
		UINT size;
		UINT count;
		const FieldMap* fields;
	};

	template <typename Struct>
	const FieldMap g_fields;

	template <typename Struct>
	struct IsFieldMapDefined : std::false_type {};

	template <typename T, UINT count, std::enable_if_t<std::is_arithmetic_v<T>, int> = 0>
	void addField(FieldMap& fields, const std::string& name, UINT offset, const T(&)[count])
	{
		fields[name] = { offset, sizeof(T), count };
	}

	template <typename T, std::enable_if_t<std::is_arithmetic_v<T>, int> = 0>
	void addField(FieldMap& fields, const std::string& name, UINT offset, const T&)
	{
		fields[name] = { offset, sizeof(T) };
	}

	template <typename T, std::enable_if_t<std::is_class_v<T>, int> = 0>
	void addField(FieldMap& fields, const std::string& name, UINT offset, const T&)
	{
		static_assert(IsFieldMapDefined<T>::value);
		fields[name] = { offset, sizeof(T), 0, &g_fields<T> };
	}

#define ADD_FIELD(field) \
	addField(fields, #field, offsetof(decltype(caps), field), caps.field);

#define DEFINE_FIELD_MAP(Struct) \
	template <> const FieldMap g_fields<Struct> = []() \
		{ \
			Struct caps = {}; \
			FieldMap fields; \
			VISIT_##Struct(ADD_FIELD) \
			return fields; \
		}(); \
	template <> struct IsFieldMapDefined<Struct> : std::true_type {};

	DEFINE_FIELD_MAP(DDSCAPS);
	DEFINE_FIELD_MAP(DDSCAPS2);
	DEFINE_FIELD_MAP(DDCAPS);

	DEFINE_FIELD_MAP(GUID);
	DEFINE_FIELD_MAP(D3DLIGHTINGCAPS);
	DEFINE_FIELD_MAP(D3DPRIMCAPS);
	DEFINE_FIELD_MAP(D3DTRANSFORMCAPS);
	DEFINE_FIELD_MAP(D3DDEVICEDESC);
	DEFINE_FIELD_MAP(D3DDEVICEDESC7);

	const std::map<std::string, Config::Settings::CapsPatches::Caps> g_caps = {
		{"DDCAPS", Config::Settings::CapsPatches::Caps::DD},
		{"D3DDEVICEDESC", Config::Settings::CapsPatches::Caps::D3D},
		{"D3DDEVICEDESC7", Config::Settings::CapsPatches::Caps::D3D7}
	};

	const std::map<Config::Settings::CapsPatches::Caps, const FieldMap&> g_fieldMaps = {
		{Config::Settings::CapsPatches::Caps::DD, g_fields<DDCAPS>},
		{Config::Settings::CapsPatches::Caps::D3D, g_fields<D3DDEVICEDESC>},
		{Config::Settings::CapsPatches::Caps::D3D7, g_fields<D3DDEVICEDESC7>}
	};

	const std::map<std::string, Config::Settings::CapsPatches::Operation> g_operations = {
		{"=", Config::Settings::CapsPatches::Operation::SET},
		{"&=", Config::Settings::CapsPatches::Operation::AND},
		{"|=", Config::Settings::CapsPatches::Operation::OR}
	};

	void applyPatch(BYTE* field, UINT size, Config::Settings::CapsPatches::Operation op, UINT value)
	{
		if (Config::Settings::CapsPatches::Operation::SET == op)
		{
			memcpy(field, &value, size);
			return;
		}

		DWORD v = 0;
		memcpy(&v, field, size);
		if (Config::Settings::CapsPatches::Operation::AND == op)
		{
			v &= value;
		}
		else
		{
			v |= value;
		}
		memcpy(field, &v, size);
	}

	std::string getFieldIdFromOffset(const FieldMap& fields, UINT offset)
	{
		std::string fieldId;
		for (const auto& field : fields)
		{
			if (offset >= field.second.offset &&
				offset < field.second.offset + std::max(field.second.count, 1u) * field.second.size)
			{
				fieldId = field.first;
				offset -= field.second.offset;
				if (0 != field.second.count)
				{
					const UINT index = offset / field.second.size;
					fieldId += '[' + std::to_string(index) + ']';
					offset -= index * field.second.size;
				}
				if (field.second.fields)
				{
					fieldId += '.' + getFieldIdFromOffset(*field.second.fields, offset);
				}
				break;
			}
		}
		return fieldId;
	}

	std::pair<UINT, UINT> getOffsetAndSizeFromFieldId(const FieldMap& fields, const std::string& fieldId)
	{
		const auto fieldEndPos = fieldId.find('.');
		const auto fieldNameWithIndex(fieldId.substr(0, fieldEndPos));

		const auto indexPos = fieldNameWithIndex.find('[');
		const auto fieldName(fieldNameWithIndex.substr(0, indexPos));
		const auto it = std::find_if(fields.begin(), fields.end(),
			[&](const auto& v) { return fieldName == Config::Parser::tolower(v.first); });
		if (it == fields.end() ||
			(indexPos != std::string::npos) != (0 != it->second.count))
		{
			return {};
		}

		int index = -1;
		if (indexPos != std::string::npos)
		{
			auto indexStr(fieldNameWithIndex.substr(indexPos + 1));
			if (indexStr.empty() || indexStr.back() != ']')
			{
				return {};
			}
			indexStr.pop_back();

			try
			{
				index = Config::Parser::parseInt(indexStr, 0, it->second.count - 1);
			}
			catch (const Config::ParsingError&)
			{
				return {};
			}
		}

		UINT offset = it->second.offset;
		if (index > 0)
		{
			offset += index * it->second.size;
		}

		if (!it->second.fields)
		{
			return { offset, it->second.size };
		}

		if (fieldEndPos == std::string::npos)
		{
			return {};
		}

		auto [o, s] = getOffsetAndSizeFromFieldId(*it->second.fields, fieldId.substr(fieldEndPos + 1));
		if (0 == s)
		{
			return {};
		}

		return { offset + o, s };
	}

	template <typename Key, typename Value>
	auto findMapValue(const std::map<Key, Value>& map, const Value& value)
	{
		return std::find_if(map.begin(), map.end(), [&](const auto& v) { return v.second == value; });
	}

	long long parseUInt(const std::string& value)
	{
		if (value.substr(0, 2) == "0x")
		{
			const auto v(value.substr(2));
			if (v.empty() || v.find_first_not_of("0123456789abcdef") != std::string::npos)
			{
				return -1;
			}
			return strtoul(v.c_str(), nullptr, 16);
		}

		if (value.find_first_not_of("0123456789") != std::string::npos)
		{
			return -1;
		}
		return strtoul(value.c_str(), nullptr, 10);
	}
}

namespace Config
{
	namespace Settings
	{
		CapsPatches::CapsPatches()
			: ListSetting("CapsPatches", "none")
		{
		}

		std::string CapsPatches::addValue(const std::string& value)
		{
			if (value.find_first_of(" \t") != std::string::npos)
			{
				throw ParsingError("whitespace is not allowed in value: '" + value + "'");
			}

			Operation operation = {};
			std::size_t pos = std::string::npos;
			std::string fieldId;
			std::string valueStr;
			for (const auto& op : g_operations)
			{
				pos = value.find(op.first);
				if (std::string::npos != pos)
				{
					operation = op.second;
					fieldId = value.substr(0, pos);
					valueStr = value.substr(pos + op.first.length());
					if (2 == op.first.length())
					{
						break;
					}
				}
			}

			if (std::string::npos == pos)
			{
				throw ParsingError("operator not found in value: '" + value + "'");
			}

			if (fieldId.empty() || valueStr.empty())
			{
				throw ParsingError("missing operand: '" + value + "'");
			}

			pos = fieldId.find('.');
			auto capsIt = g_caps.find(Parser::toupper(value.substr(0, pos)));
			if (capsIt == g_caps.end())
			{
				throw ParsingError("invalid caps: '" + value.substr(0, pos) + "'");
			}

			auto [o, s] = getOffsetAndSizeFromFieldId(g_fieldMaps.at(capsIt->second),
				pos == std::string::npos ? std::string() : fieldId.substr(pos + 1));
			if (0 == s)
			{
				throw ParsingError("invalid field id: '" + fieldId + "'");
			}

			auto v = parseUInt(valueStr);
			if (v < 0)
			{
				throw ParsingError("invalid value: '" + valueStr + "'");
			}

			m_patches.push_back({ capsIt->second, o, s, operation, static_cast<UINT>(v) });

			return Parser::toupper(value.substr(0, pos)) + '.' +
				getFieldIdFromOffset(g_fieldMaps.at(capsIt->second), o) +
				std::find_if(g_operations.begin(), g_operations.end(), [=](const auto& v) { return v.second == operation; })->first +
				(valueStr.substr(0, 2) == "0x" ? ("0x" + Parser::toupper(valueStr.substr(2))) : valueStr);
		}

		void CapsPatches::applyPatches(void* desc, Caps caps)
		{
			for (const auto& patch : m_patches)
			{
				if (patch.caps == caps)
				{
					applyPatch(static_cast<BYTE*>(desc) + patch.offset, patch.size, patch.operation, patch.value);
				}
			}
		}

		void CapsPatches::clear()
		{
			m_patches.clear();
		}
	}
}
