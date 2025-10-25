#include <algorithm>
#include <array>
#include <map>
#include <regex>
#include <set>
#include <sstream>

#include <Windows.h>
#include <bcrypt.h>
#include <d3dcompiler.h>

#include <Common/CompatPtr.h>
#include <Common/Log.h>
#include <Common/Path.h>
#include <D3dDdi/ShaderCompiler.h>

#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)

namespace
{
	const auto COMPILER_FLAGS =
		D3DCOMPILE_PACK_MATRIX_ROW_MAJOR |
		D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY |
		D3DCOMPILE_OPTIMIZATION_LEVEL3;

	typedef std::string_view Token;
	typedef std::vector<Token>::const_iterator TokenIter;
	typedef std::initializer_list<D3dDdi::ShaderCompiler::Pattern> PatternSeq;

	const D3dDdi::ShaderCompiler::Regex IDENTIFIER("[A-Za-z_][A-Za-z0-9_]*");

#define THROW_EXCEPTION(...) throw std::runtime_error((std::ostringstream() << __func__ << ": " << __VA_ARGS__).str())

	const D3D_SHADER_MACRO g_shaderDefines[] = {
		{ "PARAMETER_UNIFORM", "" },
		{ "double", "float" },
		{ "fixed", "float" },
		{ "fixed2", "float2" },
		{ "fixed3", "float3" },
		{ "fixed4", "float4" },
		{ "frac", "_frac" },
		{ "fract", "frac" },
		{ "line", "_line" },
		{ "mat3x3", "float3x3" },
		{ "matrix", "_matrix" },
		{ "mix", "lerp" },
		{ "mod", "fmod" },
		{ "point", "_point" },
		{ "sampler", "sampler2D" },
		{ "texture", "_texture" },
		{ "vec2", "float2" },
		{ "vec3", "float3" },
		{ "vec4", "float4" },
		{}
	};

	struct ShaderCacheHeader
	{
		std::array<BYTE, 16> compilerMd5;
		std::array<BYTE, 16> shaderMd5;
		UINT compilerFlags;
		UINT vsSize;
		UINT psSize;
	};

	struct D3DCompilerFuncs
	{
		decltype(&D3DCompile) d3dCompile;
		decltype(&D3DDisassemble) d3dDisassemble;
		decltype(&D3DPreprocess) d3dPreprocess;
		std::vector<BYTE> md5;
	};

	std::vector<BYTE> md5sum(const void* input, DWORD inputSize)
	{
		static BCRYPT_ALG_HANDLE alg = []()
			{
				BCRYPT_ALG_HANDLE handle = nullptr;
				BCryptOpenAlgorithmProvider(&handle, BCRYPT_MD5_ALGORITHM, nullptr, 0);
				return handle;
			}();

		static ULONG hashObjectSize = []()
			{
				ULONG size = 0;
				if (alg)
				{
					ULONG result = 0;
					BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&size), sizeof(size), &result, 0);
				}
				return size;
			}();

		std::vector<BYTE> md5(16);
		if (0 != hashObjectSize)
		{
			std::vector<BYTE> hashObject(hashObjectSize);
			BCRYPT_HASH_HANDLE hash = nullptr;
			if (NT_SUCCESS(BCryptCreateHash(alg, &hash, hashObject.data(), hashObject.size(), nullptr, 0, 0)))
			{
				if (!NT_SUCCESS(BCryptHashData(hash, static_cast<PUCHAR>(const_cast<void*>(input)), inputSize, 0)) ||
					!NT_SUCCESS(BCryptFinishHash(hash, md5.data(), md5.size(), 0)))
				{
					md5.clear();
				}
				BCryptDestroyHash(hash);
			}
		}

		if (md5.empty())
		{
			LOG_ONCE("ERROR: md5sum failed");
		}
		return md5;
	}

	FARPROC loadD3dCompilerFunc(HMODULE d3dCompiler, const char* name)
	{
		auto func = GetProcAddress(d3dCompiler, name);
		if (!func)
		{
			LOG_INFO << "ERROR: Failed to load shader compiler function: " << name;
			return nullptr;
		}
		return func;
	}

	D3DCompilerFuncs loadD3DCompilerFuncs()
	{
		HMODULE d3dCompiler = LoadLibrary("d3dcompiler_47.dll");
		if (!d3dCompiler)
		{
			d3dCompiler = LoadLibrary("d3dcompiler_43.dll");
			if (!d3dCompiler)
			{
				LOG_INFO << "ERROR: Failed to load any shader compiler modules (d3dcompiler_47.dll, d3dcompiler_43.dll)";
				return {};
			}
		}

		D3DCompilerFuncs funcs = {};
		funcs.d3dCompile = reinterpret_cast<decltype(&D3DCompile)>(loadD3dCompilerFunc(d3dCompiler, "D3DCompile"));
		funcs.d3dDisassemble = reinterpret_cast<decltype(&D3DDisassemble)>(loadD3dCompilerFunc(d3dCompiler, "D3DDisassemble"));
		funcs.d3dPreprocess = reinterpret_cast<decltype(&D3DPreprocess)>(loadD3dCompilerFunc(d3dCompiler, "D3DPreprocess"));

		auto modulePath = Compat::getModulePath(d3dCompiler);
		if (!modulePath.empty())
		{
			Compat::Log log(Config::Settings::LogLevel::INFO);
			log << "Using shader compiler: " << modulePath.u8string();
			std::ifstream f(modulePath, std::ios::binary);
			if (f)
			{
				std::ostringstream oss;
				oss << f.rdbuf();
				funcs.md5 = md5sum(oss.str().data(), oss.str().size());
				if (!funcs.md5.empty())
				{
					log << " (MD5: " << Compat::HexDump(funcs.md5.data(), funcs.md5.size()) << ')';
				}
			}
		}
		return funcs;
	}

	const D3DCompilerFuncs& getD3DCompilerFuncs()
	{
		static D3DCompilerFuncs funcs = loadD3DCompilerFuncs();
		return funcs;
	}

	float parseFloat(const std::string& value)
	{
		std::istringstream iss(value);
		float f = 0;
		if (iss >> f)
		{
			return f;
		}
		throw std::runtime_error("");
	}

	std::pair<Token, unsigned> splitSemantic(Token semantic)
	{
		const auto indexPos = semantic.find_last_not_of("0123456789") + 1;
		const auto index = indexPos < semantic.length() ? std::stoul(std::string(semantic.substr(indexPos))) : 0;
		return { semantic.substr(0, indexPos), index };
	}

	Token tokenFromRange(TokenIter begin, TokenIter end)
	{
		if (begin >= end)
		{
			return {};
		}
		return Token(begin->data(), end->data() - begin->data());
	}

	Token substring(const std::string& str, std::size_t pos, std::size_t len = std::string::npos)
	{
		return Token(str.c_str() + pos, std::min(len, str.length() - pos));
	}

	char getClosingBracket(Token token)
	{
		if (1 == token.length())
		{
			switch (token[0])
			{
			case '(': return ')';
			case '[': return ']';
			case '{': return '}';
			}
		}
		return 0;
	}

	bool isOpeningBracket(Token token)
	{
		return 0 != getClosingBracket(token);
	}

	bool matchToken(Token token, D3dDdi::ShaderCompiler::Pattern pattern)
	{
		const bool match = pattern.isRegex
			? std::regex_match(token.begin(), token.end(), std::regex(pattern.pattern.begin(), pattern.pattern.end()))
			: (token == pattern.pattern);
		return match == !pattern.isNegated;
	}

	bool matchTokenSequence(TokenIter begin, TokenIter end, PatternSeq seq)
	{
		PatternSeq::iterator it;
		for (it = seq.begin(); it < seq.end() && begin < end; ++it)
		{
			if (matchToken(*begin, *it))
			{
				++begin;
			}
			else if (!it->isOptional)
			{
				return false;
			}
		}
		return it == seq.end();
	}

	TokenIter findTokenSequence(TokenIter begin, TokenIter end, PatternSeq seq)
	{
		auto last = end;
		for (const auto& s : seq)
		{
			if (!s.isOptional)
			{
				--last;
			}
		}

		for (auto it = begin; it <= last; ++it)
		{
			if (matchTokenSequence(it, end, seq))
			{
				return it;
			}
		}
		return end;
	}
}

namespace D3dDdi
{
	class ShaderCompiler::D3DInclude : public ID3DInclude
	{
	public:
		D3DInclude(ShaderCompiler& compiler, const std::filesystem::path& sourceAbsPath)
			: m_vtbl{ Open, Close }
			, m_compiler(compiler)
		{
			lpVtbl = &m_vtbl;
			m_parentData[nullptr] = { sourceAbsPath };
		}

		static HRESULT STDMETHODCALLTYPE Open(ID3DInclude* This,
			D3D_INCLUDE_TYPE IncludeType, LPCSTR pFileName, LPCVOID pParentData, LPCVOID* ppData, UINT* pBytes)
		{
			LOG_FUNC("D3DInclude::Open", This, IncludeType, pFileName, pParentData, Compat::out(ppData), Compat::out(pBytes));
			*ppData = nullptr;
			*pBytes = 0;

			const auto self = static_cast<D3DInclude*>(This);
			const auto it = self->m_parentData.find(pParentData);
			if (it == self->m_parentData.end())
			{
				LOG_DEBUG << "Failed to find parent data: " << pParentData;
				return LOG_RESULT(E_FAIL);
			}

			const std::filesystem::path absPath((it->second.absPath.parent_path() / pFileName).lexically_normal());
			const ParentData* parent = &it->second;
			while (parent && parent->absPath != absPath)
			{
				parent = parent->parent;
			}
			if (parent)
			{
				LOG_DEBUG << "Cyclic include directives detected";
				return LOG_RESULT(E_FAIL);
			}

			std::string content(self->m_compiler.loadShaderFile(absPath));
			if (content.empty())
			{
				LOG_DEBUG << "Failed to load include file: " << absPath.u8string();
				return LOG_RESULT(E_FAIL);
			}

			*ppData = content.c_str();
			*pBytes = content.length();
			self->m_parentData.try_emplace(*ppData, ParentData{ absPath, std::move(content), &it->second });
			return LOG_RESULT(S_OK);
		}

		static HRESULT STDMETHODCALLTYPE Close(ID3DInclude* This, LPCVOID pData)
		{
			LOG_FUNC("D3DInclude::Close", This, pData);
			const auto self = static_cast<D3DInclude*>(This);
			self->m_parentData.erase(pData);
			return LOG_RESULT(S_OK);
		}

	private:
		struct ParentData
		{
			std::filesystem::path absPath;
			std::string content;
			const ParentData* parent;
		};

		ID3DIncludeVtbl m_vtbl;
		ShaderCompiler& m_compiler;
		std::map<const void*, ParentData> m_parentData;
	};

	ShaderCompiler::ShaderCompiler(const std::filesystem::path& absPath)
		: m_absPath(absPath)
	{
		LOG_FUNC("ShaderCompiler::ShaderCompiler", absPath.u8string());
		const auto& funcs = getD3DCompilerFuncs();
		if (!funcs.d3dPreprocess || !funcs.d3dCompile || !funcs.d3dDisassemble)
		{
			return;
		}
		preprocess();
	}

	void ShaderCompiler::applyTokenPatches()
	{
		std::string patchedContent;
		std::size_t contentPos = 0;
		for (const auto& patch : m_tokenPatches)
		{
			patchedContent.append(m_content.begin() + contentPos,
				m_content.begin() + (patch.first->data() - m_content.data()));
			patchedContent.append(patch.second);
			contentPos = patch.first->data() + patch.first->length() - m_content.data();
		}
		patchedContent.append(m_content.begin() + contentPos, m_content.end());

		m_content.swap(patchedContent);
		m_structs.clear();
		m_tokenPatches.clear();
		m_tokens.clear();
	}

	void ShaderCompiler::compile()
	{
		if (m_content.empty())
		{
			return;
		}

		try
		{
			postprocess();
		}
		catch (std::exception& e)
		{
			LOG_INFO << "ERROR: Failed to postprocess shader \"" << m_absPath.u8string()
				<< "\" due to the following error:\n" << e.what();
			return;
		}

		auto cachePath(m_absPath);
		cachePath += ".dcc";
		const bool cached = loadFromCache(cachePath);

		for (unsigned i = 0; i < 2; ++i)
		{
			const char* entry = 0 == i ? "main_vertex" : "main_fragment";
			const char* target = 0 == i ? "vs_3_0" : "ps_3_0";
			auto& shader = 0 == i ? m_vs : m_ps;
			std::string errors;
			compile(entry, target, shader, errors);
			if (!errors.empty() && errors.back() == '\n')
			{
				errors.pop_back();
			}

			if (shader.code.empty())
			{
				LOG_INFO << "ERROR: Failed to compile " << entry << " in shader \"" << m_absPath.u8string()
					<< "\" due to the following errors:\n" << errors;
				return;
			}
			if (shader.assembly.empty())
			{
				LOG_INFO << "ERROR: Failed to disassemble " << entry << " in shader \"" << m_absPath.u8string() << '"';
				return;
			}

			if (errors.empty())
			{
				LOG_DEBUG << "Successfully compiled " << entry << " in shader \"" << m_absPath.u8string() << '"';
			}
			else
			{
				LOG_DEBUG << "Successfully compiled " << entry << " in shader \"" << m_absPath.u8string()
					<< "\" with the following warnings:\n" << errors;
			}
		}

		if (!cached)
		{
			saveToCache(cachePath);
		}
	}

	void ShaderCompiler::compile(const char* entry, const char* target, Shader& shader, std::string& errors)
	{
		LOG_FUNC("ShaderCompiler::compile", entry, target, errors);

		const auto& funcs = getD3DCompilerFuncs();
		if (shader.code.empty())
		{
			CompatPtr<ID3DBlob> blob = nullptr;
			CompatPtr<ID3DBlob> errorMsgs = nullptr;
			HRESULT result = funcs.d3dCompile(m_content.c_str(), m_content.length(), "",
				nullptr, nullptr, entry, target, COMPILER_FLAGS, 0, &blob.getRef(), &errorMsgs.getRef());
			if (errorMsgs)
			{
				errors = static_cast<const char*>(errorMsgs.get()->lpVtbl->GetBufferPointer(errorMsgs));
			}
			if (FAILED(result))
			{
				return;
			}

			const BYTE* tokens = static_cast<BYTE*>(blob.get()->lpVtbl->GetBufferPointer(blob));
			const auto codeSize = blob.get()->lpVtbl->GetBufferSize(blob);
			shader.code.assign(tokens, tokens + codeSize);
		}

		CompatPtr<ID3DBlob> assembly;
		HRESULT result = funcs.d3dDisassemble(shader.code.data(), shader.code.size(), 0, nullptr, &assembly.getRef());
		if (SUCCEEDED(result))
		{
			shader.assembly = static_cast<char*>(assembly.get()->lpVtbl->GetBufferPointer(assembly));
			LOG_DEBUG << "Disassembled shader:\n" << shader.assembly;
		}

	}

	bool ShaderCompiler::exists(TokenIter it)
	{
		return it != m_tokens.end();
	}

	TokenIter ShaderCompiler::findBracketEnd(TokenIter begin, TokenIter end)
	{
		const char closingBracket = getClosingBracket(*begin);
		if (0 == closingBracket)
		{
			THROW_EXCEPTION("Not an opening bracket: " << toString(begin));
		}

		for (auto it = begin + 1; it < end; ++it)
		{
			if (isOpeningBracket(*it))
			{
				it = findBracketEnd(it, end);
			}
			else if (1 == it->length() && closingBracket == (*it)[0])
			{
				return it;
			}
		}

		THROW_EXCEPTION("Unmatched opening bracket: " << toString(begin));
	}

	TokenIter ShaderCompiler::findToken(Token token)
	{
		return std::lower_bound(m_tokens.begin(), m_tokens.end(), token,
			[](Token lhs, Token rhs) { return lhs.data() < rhs.data(); });
	}

	TokenIter ShaderCompiler::findUnnestedTokenSequence(TokenIter begin, TokenIter end, PatternSeq seq)
	{
		auto last = end;
		for (const auto& s : seq)
		{
			if (!s.isOptional)
			{
				--last;
			}
		}

		for (auto it = begin; it <= last; ++it)
		{
			if (isOpeningBracket(*it))
			{
				it = findBracketEnd(it, end);
			}
			else if (matchTokenSequence(it, end, seq))
			{
				return it;
			}
		}
		return end;
	}

	void ShaderCompiler::gatherUsedSemantics(const FunctionArg& arg, std::map<Token, std::set<unsigned>>& usedSemantics)
	{
		if (arg.uniform)
		{
			return;
		}

		auto [argSem, argSemInd] = exists(arg.semantic) ? splitSemantic(*arg.semantic) : std::pair<Token, unsigned>{};
		const auto s = m_structs.find(*arg.type);
		if (s == m_structs.end())
		{
			if (exists(arg.semantic))
			{
				usedSemantics[argSem].insert(argSemInd);
			}
			return;
		}

		for (const auto& field : s->second)
		{
			if (field.uniform)
			{
				continue;
			}

			if (exists(field.semantic))
			{
				auto [sem, semInd] = splitSemantic(*field.semantic);
				usedSemantics[sem].insert(semInd);
			}
			else if (exists(arg.semantic))
			{
				usedSemantics[argSem].insert(argSemInd);
				++argSemInd;
			}
		}
	}

	std::string ShaderCompiler::getNextUnusedSemantic(Token semantic, std::map<Token, std::set<unsigned>>& usedSemantics)
	{
		auto& usedIndexes = usedSemantics[semantic];
		std::size_t index = 0;
		while (usedIndexes.find(index) != usedIndexes.end())
		{
			++index;
		}
		usedIndexes.insert(index);
		return std::string(semantic) + std::to_string(index);
	}

	bool ShaderCompiler::hasSampler(const std::vector<Field>& fields)
	{
		for (const auto& f : fields)
		{
			if ("sampler2D" == *f.type)
			{
				return true;
			}
		}
		return false;
	}

	std::string ShaderCompiler::initStruct(std::map<Token, std::vector<Field>>::const_iterator s)
	{
		std::string init;
		for (const auto& f : s->second)
		{
			init += ',';
			auto it = m_structs.find(*f.name);
			if (it != m_structs.end())
			{
				init += initStruct(it);
			}
			else if (f.type->back() >= '1' && f.type->back() <= '4')
			{
				init += std::string(*f.type) + '(';
				const unsigned dim = f.type->back() - '0';
				for (unsigned i = 0; i < dim; ++i)
				{
					init += "0,";
				}
				init.back() = ')';
			}
			else
			{
				init += '0';
			}
		}
		return '{' + init.substr(1) + '}';
	}

	bool ShaderCompiler::loadFromCache(const std::filesystem::path& absPath)
	{
		LOG_FUNC("ShaderCompiler::loadFromCache", absPath.u8string());
		const auto compilerMd5 = getD3DCompilerFuncs().md5;
		if (compilerMd5.empty() || m_contentMd5.empty())
		{
			LOG_DEBUG << "Missing MD5";
			return LOG_RESULT(false);
		}

		std::ifstream f(absPath, std::ios::binary);
		if (f.fail())
		{
			LOG_DEBUG << "Failed to open file";
			return LOG_RESULT(false);
		}

		ShaderCacheHeader fileHeader = {};
		f.read(reinterpret_cast<char*>(&fileHeader), sizeof(fileHeader));
		if (f.fail())
		{
			LOG_DEBUG << "Failed to read file header";
			return LOG_RESULT(false);
		}

		if (0 != memcmp(fileHeader.compilerMd5.data(), compilerMd5.data(), fileHeader.compilerMd5.size()))
		{
			LOG_DEBUG << "Compiler MD5 mismatch";
			return LOG_RESULT(false);
		}

		if (0 != memcmp(fileHeader.shaderMd5.data(), m_contentMd5.data(), fileHeader.shaderMd5.size()))
		{
			LOG_DEBUG << "Shader MD5 mismatch";
			return LOG_RESULT(false);
		}

		if (fileHeader.compilerFlags != COMPILER_FLAGS)
		{
			LOG_DEBUG << "Compiler flags mismatch";
			return LOG_RESULT(false);
		}

		std::vector<BYTE> vs(fileHeader.vsSize);
		f.read(reinterpret_cast<char*>(vs.data()), vs.size());
		if (f.fail())
		{
			LOG_DEBUG << "Failed to read VS data";
			return LOG_RESULT(false);
		}

		std::vector<BYTE> ps(fileHeader.psSize);
		f.read(reinterpret_cast<char*>(ps.data()), ps.size());
		if (f.fail())
		{
			LOG_DEBUG << "Failed to read PS data";
			return LOG_RESULT(false);
		}

		char c = 0;
		f.read(&c, 1);
		if (!f.eof())
		{
			LOG_DEBUG << "Unexpected data at end of file";
			return LOG_RESULT(false);
		}

		m_vs.code = vs;
		m_ps.code = ps;
		return LOG_RESULT(true);
	}

	std::string ShaderCompiler::loadShaderFile(const std::filesystem::path& absPath)
	{
		LOG_FUNC("ShaderCompiler::loadShaderFile", absPath.u8string());
		std::ifstream f(absPath);
		if (f.fail())
		{
			LOG_DEBUG << "Failed to open file";
			return {};
		}

		std::ostringstream oss;
		oss << f.rdbuf();
		const auto content(oss.str());

		try
		{
			const std::regex regex(R"-(#pragma\s+parameter\s+([^\s]+)\s+"([^"]*)"\s+([^\s]+)\s+([^\s]+)\s+([^\s]+)\s+([^\s]+))-");
			for (auto it = std::sregex_iterator(content.begin(), content.end(), regex); it != std::sregex_iterator(); ++it)
			{
				Parameter param = {};
				param.name = it->str(1);
				param.description = it->str(2);
				param.defaultValue = parseFloat(it->str(3));
				param.currentValue = param.defaultValue;
				param.min = parseFloat(it->str(4));
				param.max = parseFloat(it->str(5));
				param.step = parseFloat(it->str(6));
				m_parameters.push_back(param);
			}
		}
		catch (const std::exception&)
		{
			LOG_DEBUG << "Failed to parse parameters";
			return {};
		}

		return content;
	}

	void ShaderCompiler::logContent(const std::string& header)
	{
		if (Config::logLevel.get() < Config::Settings::LogLevel::DEBUG)
		{
			return;
		}

		Compat::Log log(Config::Settings::LogLevel::DEBUG);
		log << header << ":\n";
		std::size_t prevPos = 0;
		std::size_t line = 1;
		for (std::size_t pos = m_content.find('\n'); pos < m_content.length(); pos = m_content.find('\n', prevPos))
		{
			log << "line " << line << ": " << std::string_view(m_content.data() + prevPos, pos - prevPos) << '\n';
			prevPos = pos + 1;
			++line;
		}
		if (prevPos < m_content.length())
		{
			log << "line " << line << ": " << std::string_view(m_content.data() + prevPos, m_content.length() - prevPos);
		}
	}

	std::string ShaderCompiler::makeUnique(Token token)
	{
		return std::string(token) + std::to_string(token.data() - m_content.data());
	}

	ShaderCompiler::Function ShaderCompiler::parseFunction(TokenIter returnType)
	{
		Function func = {};
		func.mods.begin = returnType;
		while (func.mods.begin > m_tokens.begin() && matchToken(*(func.mods.begin - 1), IDENTIFIER))
		{
			--func.mods.begin;
		}
		func.mods.end = returnType;
		func.ret.type = returnType;
		func.ret.name = m_tokens.end();
		func.ret.begin = func.mods.begin;
		func.ret.end = returnType + 1;
		func.ret.out = true;
		func.name = returnType + 1;

		const auto argsBegin = returnType + 3;
		const auto argsEnd = findBracketEnd(argsBegin - 1, m_tokens.end());
		for (auto begin = func.name + 2; begin < argsEnd;)
		{
			const auto end = findTokenSequence(begin, argsEnd, { "," });
			func.args.push_back(parseFunctionArg(begin, end));
			begin = end + 1;
		}

		func.ret.semantic = ":" == *(argsEnd + 1) ? argsEnd + 2 : m_tokens.end();
		func.body.begin = argsEnd + (exists(func.ret.semantic) ? 4 : 2);
		func.body.end = findBracketEnd(func.body.begin - 1, m_tokens.end());
		func.isMain = "main_vertex" == *func.name || "main_fragment" == *func.name;

		if (func.isMain)
		{
			if ("void" != *returnType)
			{
				gatherUsedSemantics(func.ret, func.usedOutputSemantics);
			}

			for (const auto& arg : func.args)
			{
				gatherUsedSemantics(arg, arg.out ? func.usedOutputSemantics : func.usedInputSemantics);
			}
		}

		return func;
	}

	ShaderCompiler::FunctionArg ShaderCompiler::parseFunctionArg(TokenIter begin, TokenIter end)
	{
		FunctionArg arg = {};
		arg.begin = begin;
		arg.end = end;

		auto it = begin;
		if ("static" == *it)
		{
			m_tokenPatches[it] = {};
			++it;
		}

		if ("uniform" == *it)
		{
			arg.uniform = true;
			++it;
		}
		else if ("in" == *it || "out" == *it || "inout" == *it)
		{
			arg.out = "out" == *it;
			++it;
		}
		else if ("const" == *it)
		{
			++it;
		}

		if (matchTokenSequence(it, end, { IDENTIFIER, IDENTIFIER }))
		{
			arg.type = it;
			arg.name = it + 1;
			it += 2;

			if (matchTokenSequence(it, end, { "[", Regex("[0-9]+"), "]" }))
			{
				arg.dim = std::stoul(std::string(*(it + 1)));
				it += 3;
			}

			if (it == end)
			{
				arg.semantic = m_tokens.end();
				return arg;
			}

			if (it + 2 == end && matchTokenSequence(it, end, { ":", IDENTIFIER }))
			{
				arg.semantic = it + 1;
				return arg;
			}
		}

		THROW_EXCEPTION("Failed to parse function argument: " << toString(begin, end));
	}

	ShaderCompiler::Field ShaderCompiler::parseStructField(TokenIter begin, TokenIter end, TokenIter type)
	{
		if (matchTokenSequence(begin, end, { IDENTIFIER }))
		{
			Field field = {};
			field.type = type;
			field.name = begin;

			if (begin + 1 == end)
			{
				field.semantic = m_tokens.end();
				return field;
			}

			if (3 == end - begin && matchTokenSequence(begin + 1, end, { ":", IDENTIFIER }))
			{
				field.semantic = begin + 2;
				return field;
			}
		}

		THROW_EXCEPTION("Failed to parse struct field: " << toString(begin, end));
	}

	void ShaderCompiler::parseStructs()
	{
		const PatternSeq seq = { "struct", IDENTIFIER, "{" };
		for (auto it = findTokenSequence(m_tokens.begin(), m_tokens.end(), seq); it != m_tokens.end();)
		{
			const auto& structName = *(it + 1);
			const auto bodyBegin = it + 3;
			const auto bodyEnd = findBracketEnd(it + 2, m_tokens.end());

			if (m_structs.find(structName) != m_structs.end())
			{
				removeTokens(it, bodyEnd + 2);
				it = findTokenSequence(bodyEnd + 2, m_tokens.end(), seq);
				continue;
			}

			for (auto declStart = bodyBegin; declStart < bodyEnd;)
			{
				const auto declEnd = findTokenSequence(declStart, bodyEnd, { ";" });
				const bool uniform = "uniform" == *declStart;
				if (!matchTokenSequence(declStart + uniform, declEnd, { IDENTIFIER }))
				{
					THROW_EXCEPTION("Expected a type, found: " << toString(declStart + uniform));
				}
				const auto type = declStart + uniform;

				for (auto fieldStart = declStart + uniform + 1; fieldStart < declEnd;)
				{
					const auto fieldEnd = findTokenSequence(fieldStart, declEnd, { "," });
					auto field = parseStructField(fieldStart, fieldEnd, type);
					field.uniform = uniform;
					m_structs[structName].push_back(field);
					fieldStart = fieldEnd + 1;
				}
				declStart = declEnd + 1;
			}

			if (m_structs[structName].empty())
			{
				THROW_EXCEPTION("Empty struct is not supported: " + toString(structName));
			}

			it = findTokenSequence(bodyEnd + 1, m_tokens.end(), seq);
		}
	}

	void ShaderCompiler::patchTokenSequence(TokenIter begin, TokenIter end, PatternSeq seq, const std::string& patch)
	{
		for (auto it = findTokenSequence(begin, end, seq); it < end; it = findTokenSequence(it + seq.size(), end, seq))
		{
			m_tokenPatches[it] = patch;
			removeTokens(it + 1, it + seq.size());
		}
	}

	void ShaderCompiler::postprocessArrayCtors()
	{
		const PatternSeq& seq = { Regex("float[1-4]?"), "[", "]", "(" };
		for (auto begin = findTokenSequence(m_tokens.begin(), m_tokens.end(), seq);
			begin < m_tokens.end();)
		{
			const auto end = findBracketEnd(begin + 3, m_tokens.end());
			removeTokens(begin, begin + 3);
			m_tokenPatches[begin + 3] = '{';
			m_tokenPatches[end] = '}';
			begin = findTokenSequence(end + 1, m_tokens.end(), seq);
		}
	}

	void ShaderCompiler::postprocessDeadBranches(TokenRange body)
	{
		const PatternSeq seq = { "const", Optional("static"), "bool", IDENTIFIER, "=", Regex("true|false"), ";" };
		for (auto begin = findTokenSequence(body.begin, body.end, seq);
			begin < body.end;
			begin = findTokenSequence(begin + 6, body.end, seq))
		{
			const auto end = findUnnestedTokenSequence(begin, body.end + 1, { "}" });
			const bool hasStatic = "static" == *(begin + 1);
			const auto& boolName = *(begin + 2 + hasStatic);

			const PatternSeq& ifSeq = { "if", "(", boolName, ")", "{" };
			for (auto ifBegin = findTokenSequence(begin, end, ifSeq);
				ifBegin < end;
				ifBegin = findTokenSequence(ifBegin + 5, end, ifSeq))
			{
				const auto ifEnd = findBracketEnd(ifBegin + 4, end);
				const bool boolValue = "true" == *(begin + 4 + hasStatic);
				const bool hasElse = matchTokenSequence(ifEnd + 1, end, { "else", "{" });
				if (!boolValue)
				{
					removeTokens(ifBegin + 5, ifEnd - 1);
				}
				else if (hasElse)
				{
					const auto elseEnd = findBracketEnd(ifEnd + 2, end);
					removeTokens(ifEnd + 1, elseEnd + 1);
				}
			}
		}
	}

	void ShaderCompiler::postprocessFunctionArg(Function& func, const FunctionArg& arg)
	{
		const auto s = m_structs.find(*arg.type);
		if (s == m_structs.end())
		{
			if ("sampler2D" == *arg.type)
			{
				if (exists(arg.semantic))
				{
					auto [argSem, argSemInd] = splitSemantic(*arg.semantic);
					if ("TEXUNIT" == argSem)
					{
						m_tokenPatches[arg.semantic] = "register(s" + std::to_string(argSemInd) + ')';
					}
				}
			}
			else if (func.isMain)
			{
				const auto semantic = exists(arg.semantic)
					? std::string(*arg.semantic)
					: getNextUnusedSemantic("TEXCOORD", arg.out ? func.usedOutputSemantics : func.usedInputSemantics);
				if (!exists(arg.semantic))
				{
					m_tokenPatches[arg.name] = std::string(*arg.name) + " : " + semantic;
				}

				if (!arg.out && "main_vertex" == *func.name)
				{
					const auto [sem, semInd] = splitSemantic(semantic);
					if ("TEXCOORD" == sem)
					{
						m_texCoords[std::string(*arg.name)] = semInd;
					}
				}
			}
			return;
		}

		const PatternSeq& assignmentSeq = { *arg.name, "=" };
		for (auto assignmentBegin = findTokenSequence(func.body.begin, func.body.end, assignmentSeq);
			assignmentBegin < func.body.end;)
		{
			const auto& varName = makeUnique(*arg.name);
			m_tokenPatches[assignmentBegin] = std::string(*arg.type) + ' ' + varName;
			const auto assignmentEnd = findUnnestedTokenSequence(assignmentBegin + 2, func.body.end, { ";" });
			if (assignmentEnd == func.body.end)
			{
				THROW_EXCEPTION("Failed to find end of assignment: " + toString(assignmentBegin));
			}

			auto& patch = m_tokenPatches[assignmentEnd];
			patch = ";\n";
			for (const auto& f : s->second)
			{
				patch += "    " + std::string(*arg.name) + "__" + std::string(*f.name) + " = " +
					varName + '.' + std::string(*f.name) + ";\n";
			}

			assignmentBegin = findTokenSequence(assignmentEnd + 1, func.body.end, assignmentSeq);
		}

		const PatternSeq& rhsSeq = { "=", *arg.name, ";" };
		for (auto rhsBegin = findTokenSequence(func.body.begin, func.body.end, rhsSeq);
			rhsBegin < func.body.end;
			rhsBegin = findTokenSequence(rhsBegin + 3, func.body.end, rhsSeq))
		{
			auto& patch = m_tokenPatches[rhsBegin - 1];
			for (const auto& f : s->second)
			{
				patch += std::string(*arg.name) + "__" + std::string(*f.name) + ", ";
			}
			patch = std::string(*arg.type) + ' ' + std::string(*arg.name) + " = {" +
				patch.substr(0, patch.length() - 2) + "};\n    " + std::string(*(rhsBegin - 1));
		}

		const PatternSeq& argSeq = { *arg.name, Regex("[,)]") };
		for (auto argBegin = findTokenSequence(func.body.begin, func.body.end, argSeq);
			argBegin < func.body.end;
			argBegin = findTokenSequence(argBegin + 2, func.body.end, argSeq))
		{
			auto& patch = m_tokenPatches[argBegin];
			for (const auto& f : s->second)
			{
				patch += std::string(*arg.name) + "__" + std::string(*f.name) + ", ";
			}
			patch.erase(patch.length() - 2, 2);
		}

		m_tokenPatches[arg.begin] = splitFunctionArg(func, arg, s->second);
		removeTokens(arg.begin + 1, arg.end);
	}

	void ShaderCompiler::postprocessFunctionRet(Function& func)
	{
		const auto s = m_structs.find(*func.ret.type);
		if (s == m_structs.end())
		{
			if ("main_fragment" == *func.name)
			{
				if (exists(func.ret.semantic) && "COLOR" == *func.ret.semantic && "float4" != *func.ret.type)
				{
					m_tokenPatches[func.ret.type] = "float4";
					const PatternSeq& seq = { "return" };
					for (auto begin = findTokenSequence(func.body.begin, func.body.end, seq); begin < func.body.end;)
					{
						const auto end = findUnnestedTokenSequence(begin + 1, func.body.end, { ";" });
						if (end == m_tokens.end())
						{
							THROW_EXCEPTION("Unterminated return statement: " + toString(begin));
						}
						m_tokenPatches[begin] = "return _float4(";
						m_tokenPatches[end] = ");";
						begin = findTokenSequence(end + 1, func.body.end, seq);
					}
				}
				patchTokenSequence(func.body.begin, func.body.end, { "discard" }, "discard; return 0");
			}
			return;
		}

		m_tokenPatches[func.ret.type] = "void";

		const auto argsEnd = findBracketEnd(func.name + 1, m_tokens.end());
		m_tokenPatches[argsEnd] = ((argsEnd == func.name + 2) ? "" : ", ") + splitFunctionArg(func, func.ret, s->second) + ')';

		if (exists(func.ret.semantic))
		{
			m_tokenPatches[func.ret.semantic - 1] = {};
			m_tokenPatches[func.ret.semantic] = {};
		}

		const PatternSeq& returnSeq = { "return" };
		for (auto begin = findTokenSequence(func.body.begin, func.body.end, returnSeq); begin < func.body.end;)
		{
			const auto end = findUnnestedTokenSequence(begin + 1, func.body.end, { ";" });
			if (end == m_tokens.end())
			{
				THROW_EXCEPTION("Unterminated return statement: " + toString(begin));
			}

			const auto& varName = '_' + makeUnique(*begin);
			m_tokenPatches[begin] = std::string(*func.ret.type) + ' ' + varName + " = ";

			auto& patch = m_tokenPatches[end];
			patch = ";\n";
			for (const auto& f : s->second)
			{
				patch += "    _ret__" + std::string(*f.name) + " = " +
					varName + '.' + std::string(*f.name) + ";\n";
			}

			begin = findTokenSequence(end + 1, func.body.end, returnSeq);
		}

		const PatternSeq& declSeq = { *func.ret.type, IDENTIFIER, ";" };
		for (auto begin = findTokenSequence(func.body.begin, func.body.end, declSeq);
			begin < func.body.end;
			begin = findTokenSequence(begin + 3, func.body.end, declSeq))
		{
			m_tokenPatches[begin + 2] = " = " + initStruct(s) + ';';
		}
	}

	void ShaderCompiler::postprocessFunction(Function& func)
	{
		if (!func.isMain && '_' != func.name->front() &&
			findTokenSequence(func.body.end + 1, m_tokens.end(), { *func.name }) == m_tokens.end())
		{
			removeTokens(func.mods.begin, func.body.end + 1);
			return;
		}

		for (const auto& arg : func.args)
		{
			postprocessFunctionArg(func, arg);
		}

		postprocessFunctionRet(func);

		patchTokenSequence(func.body.begin, func.body.end, { "static", "const" }, "const");
		patchTokenSequence(func.body.begin, func.body.end, { "const", "static" }, "const");
		patchTokenSequence(func.body.begin, func.body.end, { ".", "st" }, ".xy");
		patchTokenSequence(func.body.begin, func.body.end, { "_frac", "(" }, "frac(");
		postprocessDeadBranches(func.body);

		const PatternSeq& seq = { Regex("(float)[234]?"), IDENTIFIER };
		for (auto declBegin = findTokenSequence(func.body.begin, func.body.end, seq); declBegin < func.body.end;)
		{
			const auto declEnd = findTokenSequence(declBegin + 2, func.body.end, { ";" });
			if (declEnd == func.body.end)
			{
				THROW_EXCEPTION("Failed to find end of variable declaration: " << toString(declBegin + 1));
			}
			postprocessVariableInit(declBegin + 1, declEnd);
			declBegin = findTokenSequence(declEnd + 1, func.body.end, seq);
		}
	}

	void ShaderCompiler::postprocessFunctions()
	{
		const PatternSeq seq = { IDENTIFIER, IDENTIFIER, "(" };
		for (auto it = findTokenSequence(m_tokens.begin(), m_tokens.end(), seq); it < m_tokens.end();)
		{
			auto func = parseFunction(it);
			postprocessFunction(func);
			it = findTokenSequence(func.body.end + 1, m_tokens.end(), seq);
		}
	}

	void ShaderCompiler::postprocessGlobalVariables()
	{
		const PatternSeq seq = { IDENTIFIER, IDENTIFIER, Regex("[\\[=]") };
		for (auto begin = findUnnestedTokenSequence(m_tokens.begin(), m_tokens.end(), seq);
			begin < m_tokens.end();
			begin = findUnnestedTokenSequence(begin + 3, m_tokens.end(), seq))
		{
			if ("[" == *(begin + 2))
			{
				auto it = findBracketEnd(begin + 2, m_tokens.end());
				if (it + 1 >= m_tokens.end() || "=" != *(it + 1))
				{
					continue;
				}
			}

			auto it = begin;
			while (it > m_tokens.begin() && matchToken(*(it - 1), IDENTIFIER) && "static" != *(it - 1))
			{
				--it;
			}
			if (it > m_tokens.begin() && "static" != *(it - 1))
			{
				m_tokenPatches[begin] = "static " + std::string(*begin);
			}
		}
	}

	void ShaderCompiler::postprocessScalarToVector()
	{
		const PatternSeq seq = { Regex("(float|int|bool)[234]"), "(" };
		for (auto begin = findTokenSequence(m_tokens.begin(), m_tokens.end(), seq); begin < m_tokens.end();)
		{
			const auto end = findBracketEnd(begin + 1, m_tokens.end());
			if (findUnnestedTokenSequence(begin + 2, end, { "," }) == end)
			{
				m_tokenPatches[begin] = '_' + std::string(*begin);
			}
			begin = findTokenSequence(begin + 2, m_tokens.end(), seq);
		}
	}

	void ShaderCompiler::postprocessStructs()
	{
		for (auto& s : m_structs)
		{
			if (s.second.empty())
			{
				continue;
			}

			for (auto& f : s.second)
			{
				if (f.uniform)
				{
					m_tokenPatches[f.type - 1] = {};
				}

				if ("sampler2D" == *f.type)
				{
					m_tokenPatches[f.type] = {};
					const auto begin = f.name;
					const auto end = (exists(f.semantic) ? f.semantic : begin) + 2;
					removeTokens(begin, end);
				}
			}

			auto structEnd = findTokenSequence(s.second.back().name + 1, m_tokens.end(), { "}", ";" });
			if (structEnd == m_tokens.end())
			{
				THROW_EXCEPTION("Failed to find end of struct: " + toString(s.first));
			}

			const PatternSeq seq = { s.first, "(" };
			for (auto it = findTokenSequence(structEnd + 2, m_tokens.end(), seq);
				it < m_tokens.end();
				it = findTokenSequence(it + 2, m_tokens.end(), seq))
			{
				m_tokenPatches[it] = '_' + std::string(s.first);

				auto& patch = m_tokenPatches[structEnd + 1];
				if (!patch.empty())
				{
					continue;
				}

				patch = ";\n" + std::string(s.first) + " _" + std::string(s.first) + '(';
				for (auto& f : s.second)
				{
					if ("sampler2D" != *f.type)
					{
						patch += std::string(*f.type) + ' ' + std::string(*f.name) + ", ";
					}
				}

				patch.resize(patch.size() - 2);
				patch += ")\n{\n  " + std::string(s.first) + " s = { ";
				for (auto& f : s.second)
				{
					if ("sampler2D" != *f.type)
					{
						patch += std::string(*f.name) + ", ";
					}
				}
				patch.resize(patch.size() - 2);
				patch += " };\n  return s;\n}";
			}
		}
	}

	void ShaderCompiler::postprocessVariableInit(TokenIter begin, TokenIter end)
	{
		for (auto it = begin; it < end;)
		{
			if (!matchTokenSequence(it, end, { IDENTIFIER }))
			{
				THROW_EXCEPTION("Expected an identifier, found: " << toString(it));
			}

			if (it + 1 == end || "," == *(it + 1))
			{
				m_tokenPatches[it] = std::string(*it) + "=0";
				it += 2;
			}
			else
			{
				it = findUnnestedTokenSequence(it + 2, end, { "," }) + 1;
			}
		}
	}

	void ShaderCompiler::postprocess()
	{
		LOG_FUNC("ShaderCompiler::postprocess");
		tokenize();
		parseStructs();
		postprocessStructs();
		postprocessArrayCtors();
		postprocessScalarToVector();
		postprocessFunctions();
		postprocessGlobalVariables();
		applyTokenPatches();
		m_content = std::regex_replace(m_content, std::regex("#.*"), "");
		m_content = std::regex_replace(m_content, std::regex("[ \t]+\n"), "\n");
		m_content = std::regex_replace(m_content, std::regex("\n{3,}"), "\n\n");
		m_contentMd5 = md5sum(m_content.data(), m_content.size());
		LOG_DEBUG << "MD5: " << Compat::HexDump(m_contentMd5.data(), m_contentMd5.size());
		logContent("Postprocessed content");
	}

	void ShaderCompiler::preprocess()
	{
		LOG_FUNC("ShaderCompiler::preprocess");
		auto sourceFileContent = loadShaderFile(m_absPath);
		if (sourceFileContent.empty())
		{
			LOG_INFO << "Failed to open shader: " << m_absPath.u8string();
			return;
		}

		sourceFileContent = R"(
float2 _float2(float v) { return v.xx; }
float2 _float2(float2 v) { return v; }
float3 _float3(float v) { return v.xxx; }
float3 _float3(float3 v) { return v; }
float4 _float4(float v) { return v.xxxx; }
float4 _float4(float2 v) { return float4(v, 0, 0); }
float4 _float4(float3 v) { return float4(v, 0); }
float4 _float4(float4 v) { return v; }

int2 _int2(int v) { return v.xx; }
int2 _int2(int2 v) { return v; }
int3 _int3(int v) { return v.xxx; }
int3 _int3(int3 v) { return v; }
int4 _int4(int v) { return v.xxxx; }
int4 _int4(int4 v) { return v; }

bool2 _bool2(bool v) { return v.xx; }
bool2 _bool2(bool2 v) { return v; }
bool3 _bool3(bool v) { return v.xxx; }
bool3 _bool3(bool3 v) { return v; }
bool4 _bool4(bool v) { return v.xxxx; }
bool4 _bool4(bool4 v) { return v; }

float2 _normalize(float2 v) { float2 len = dot(v, v); return 0 == len ? float2(0, 1) : (rsqrt(len) * v); }
float3 _normalize(float3 v) { float3 len = dot(v, v); return 0 == len ? float3(0, 0, 1) : (rsqrt(len) * v); }

float4 _tex1D(sampler1D s, float t) { return tex1D(s, t); }
float4 _tex1D(sampler1D s, float2 t) { return tex1D(s, t.x); }
float4 _tex1D(sampler1D s, float t, int texel_off) { return tex1D(s, t); }
float4 _tex1D(sampler1D s, float2 t, int texel_off) { return tex1D(s, t.x); }
float4 _tex1D(sampler1D s, float t, float dx, float dy) { return tex1D(s, t, dx, dy); }
float4 _tex1D(sampler1D s, float2 t, float dx, float dy) { return tex1D(s, t.x, dx, dy); }
float4 _tex1D(sampler1D s, float t, float dx, float dy, int texel_off) { return tex1D(s, t, dx, dy); }
float4 _tex1D(sampler1D s, float2 t, float dx, float dy, int texel_off) { return tex1D(s, t.x, dx, dy); }
float4 _tex1Dbias(sampler1D s, float4 t) { return tex1Dbias(s, t); }
float4 _tex1Dbias(sampler1D s, float4 t, int texel_off) { return tex1Dbias(s, t); }
float4 _tex1Dfetch(sampler1D s, int4 t) { return tex1D(s, t.x); }
float4 _tex1Dfetch(sampler1D s, int4 t, int texel_off) { return tex1D(s, t.x); }
float4 _tex1Dlod(sampler1D s, float4 t) { return tex1Dlod(s, t); }
float4 _tex1Dlod(sampler1D s, float4 t, int texel_off) { return tex1Dlod(s, t); }
float4 _tex1Dproj(sampler1D s, float2 t) { return tex1Dproj(s, t.xyyy); }
float4 _tex1Dproj(sampler1D s, float3 t) { return tex1Dproj(s, t.xzzz); }
float4 _tex1Dproj(sampler1D s, float2 t, int texel_off) { return tex1Dproj(s, t.xyyy); }
float4 _tex1Dproj(sampler1D s, float3 t, int texel_off) { return tex1Dproj(s, t.xzzz); }

float4 _tex2D(sampler2D s, float2 t) { return tex2D(s, t); }
float4 _tex2D(sampler2D s, float3 t) { return tex2D(s, t.xy); }
float4 _tex2D(sampler2D s, float2 t, int texel_off) { return tex2D(s, t); }
float4 _tex2D(sampler2D s, float3 t, int texel_off) { return tex2D(s, t.xy); }
float4 _tex2D(sampler2D s, float2 t, float2 dx, float2 dy) { return tex2D(s, t, dx, dy); }
float4 _tex2D(sampler2D s, float3 t, float2 dx, float2 dy) { return tex2D(s, t.xy, dx, dy); }
float4 _tex2D(sampler2D s, float2 t, float2 dx, float2 dy, int texel_off) { return tex2D(s, t, dx, dy); }
float4 _tex2D(sampler2D s, float3 t, float2 dx, float2 dy, int texel_off) { return tex2D(s, t.xy, dx, dy); }
float4 _tex2Dbias(sampler2D s, float4 t) { return tex2Dbias(s, t); }
float4 _tex2Dbias(sampler2D s, float4 t, int texel_off) { return tex2Dbias(s, t); }
float4 _tex2Dfetch(sampler2D s, int4 t) { return tex2Dlod(s, t); }
float4 _tex2Dfetch(sampler2D s, int4 t, int texel_off) { return tex2Dlod(s, t); }
float4 _tex2Dlod(sampler2D s, float4 t) { return tex2Dlod(s, t); }
float4 _tex2Dlod(sampler2D s, float4 t, int texel_off) { return tex2Dlod(s, t); }
float4 _tex2Dproj(sampler2D s, float3 t) { return tex2Dproj(s, t.xyzz); }
float4 _tex2Dproj(sampler2D s, float4 t) { return tex2Dproj(s, t); }
float4 _tex2Dproj(sampler2D s, float3 t, int texel_off) { return tex2Dproj(s, t.xyzz); }
float4 _tex2Dproj(sampler2D s, float4 t, int texel_off) { return tex2Dproj(s, t); }

float4 _tex3D(sampler3D s, float3 t) { return tex3D(s, t); }
float4 _tex3D(sampler3D s, float3 t, int texel_off) { return tex3D(s, t); }
float4 _tex3D(sampler3D s, float3 t, float3 dx, float3 dy) { return tex3D(s, t, dx, dy); }
float4 _tex3D(sampler3D s, float3 t, float3 dx, float3 dy, int texel_off) { return tex3D(s, t, dx, dy); }
float4 _tex3Dbias(sampler3D s, float4 t) { return tex3Dbias(s, t); }
float4 _tex3Dbias(sampler3D s, float4 t, int texel_off) { return tex3Dbias(s, t); }
float4 _tex3Dfetch(sampler3D s, int4 t) { return tex3D(s, t.xyz); }
float4 _tex3Dfetch(sampler3D s, int4 t, int texel_off) { return tex3D(s, t.xyz); }
float4 _tex3Dlod(sampler3D s, float4 t) { return tex3Dlod(s, t); }
float4 _tex3Dlod(sampler3D s, float4 t, int texel_off) { return tex3Dlod(s, t); }
float4 _tex3Dproj(sampler3D s, float4 t) { return tex3Dproj(s, t); }
float4 _tex3Dproj(sampler3D s, float4 t, int texel_off) { return tex3Dproj(s, t); }

#define normalize _normalize
#define tex1D _tex1D
#define tex1Dbias _tex1Dbias
#define tex1Dfetch _tex1Dfetch
#define tex1Dlod _tex1Dlod
#define tex1Dproj _tex1Dproj
#define tex2D _tex2D
#define tex2Dbias _tex2Dbias
#define tex2Dfetch _tex2Dfetch
#define tex2Dlod _tex2Dlod
#define tex2Dproj _tex2Dproj
#define tex3D _tex3D
#define tex3Dbias _tex3Dbias
#define tex3Dfetch _tex3Dfetch
#define tex3Dlod _tex3Dlod
#define tex3Dproj _tex3Dproj
)" + sourceFileContent;

		D3DInclude d3dInclude(*this, m_absPath);
		CompatPtr<ID3DBlob> blob = nullptr;
		CompatPtr<ID3DBlob> errorMsgs = nullptr;

		const HRESULT result = getD3DCompilerFuncs().d3dPreprocess(sourceFileContent.c_str(), sourceFileContent.length(),
			"", g_shaderDefines, &d3dInclude, &blob.getRef(), &errorMsgs.getRef());

		std::string errors;
		if (errorMsgs)
		{
			errors = static_cast<const char*>(errorMsgs.get()->lpVtbl->GetBufferPointer(errorMsgs));
			if (!errors.empty() && '\n' == errors.back())
			{
				errors.pop_back();
			}
		}

		if (FAILED(result))
		{
			LOG_INFO << "ERROR: Failed to preprocess shader \"" << m_absPath.u8string()
				<< "\" due to the following errors:\n" << errors;
			return;
		}

		if (Config::logLevel.get() >= Config::Settings::LogLevel::DEBUG)
		{
			Compat::Log log(Config::Settings::LogLevel::DEBUG);
			log << "Successfully preprocessed shader \"" << m_absPath.u8string() << '"';
			if (!errors.empty())
			{
				log << " with the following warnings:\n" << errors;
			}
		}

		m_content.assign(static_cast<char*>(blob.get()->lpVtbl->GetBufferPointer(blob)));
		logContent("Preprocessed content");
	}

	std::string ShaderCompiler::splitFunctionArg(Function& func, const FunctionArg& arg, const std::vector<Field>& fields)
	{
		auto [argSem, argSemInd] = exists(arg.semantic) ? splitSemantic(*arg.semantic) : std::pair<Token, unsigned>{};
		std::string patch;
		for (const auto& f : fields)
		{
			const std::string newName = std::string(exists(arg.name) ? *arg.name : "_ret") + "__" + std::string(*f.name);
			patch += ", ";
			if (arg.uniform || f.uniform || "sampler2D" == *f.type)
			{
				patch += "uniform " + std::string(*f.type) + ' ' + newName;
			}
			else
			{
				std::string semantic;
				if (exists(f.semantic))
				{
					semantic = *f.semantic;
				}
				else if (exists(arg.semantic))
				{
					semantic = std::string(argSem) + std::to_string(argSemInd);
					++argSemInd;
				}
				else
				{
					semantic = getNextUnusedSemantic("TEXCOORD", arg.out ? func.usedOutputSemantics : func.usedInputSemantics);
				}
				patch += (arg.out ? "out " : "") + std::string(*f.type) + ' ' + newName + " : " + semantic;

				if (!arg.out && "main_vertex" == *func.name)
				{
					const auto [sem, semInd] = splitSemantic(semantic);
					if ("TEXCOORD" == sem)
					{
						m_texCoords[newName] = semInd;
					}
				}
			}

			if (exists(arg.name))
			{
				const PatternSeq seq = { *arg.name, ".", *f.name };
				patchTokenSequence(func.body.begin, func.body.end, seq, newName);
			}
		}

		return patch.empty() ? std::string() : patch.substr(2);
	}

	void ShaderCompiler::tokenize()
	{
		for (std::size_t pos = 0; pos < m_content.length(); ++pos)
		{
			auto next = m_content[pos];
			if (' ' == next || '\t' == next || '\r' == next || '\n' == next)
			{
				continue;
			}

			if ('#' == next)
			{
				pos = m_content.find_first_of('\n', pos + 1);
				continue;
			}

			if ('"' == next)
			{
				auto end = m_content.find_first_of('"', pos + 1);
				if (std::string::npos == end)
				{
					THROW_EXCEPTION("Unmatched quote: " << toString(Token(m_content.data() + pos + 1, 1)));
				}
				m_tokens.push_back(substring(m_content, pos, end - pos + 1));
				pos = end;
				continue;
			}

			const std::size_t start = pos;
			while (next >= 'A' && next <= 'Z' ||
				next >= 'a' && next <= 'z' ||
				next >= '0' && next <= '9' ||
				'_' == next)
			{
				++pos;
				next = m_content[pos];
			}

			if (pos != start)
			{
				m_tokens.push_back(substring(m_content, start, pos - start));
				--pos;
			}
			else
			{
				m_tokens.push_back(substring(m_content, pos, 1));
			}
		}
	}

	std::string ShaderCompiler::toString(Token token)
	{
		if (token.data() < m_content.data() ||
			token.data() + token.length() > m_content.data() + m_content.length())
		{
			return "'" + std::string(token) + "'";
		}

		int line = 0;
		int tokenPos = token.data() - m_content.data();
		int prevNewLinePos = -1;
		for (int pos = -1; pos < tokenPos; pos = m_content.find('\n', pos + 1))
		{
			++line;
			prevNewLinePos = pos;
		}
		int column = tokenPos - prevNewLinePos;
		return "'" + std::string(token) + "' (line " + std::to_string(line) + ", column " + std::to_string(column) + ')';
	}

	void ShaderCompiler::removeTokens(TokenIter begin, TokenIter end)
	{
		for (auto it = begin; it < end; ++it)
		{
			m_tokenPatches[it] = {};
		}
	}

	std::string ShaderCompiler::toString(TokenIter it)
	{
		return toString(*it);
	}

	std::string ShaderCompiler::toString(TokenIter begin, TokenIter end)
	{
		return toString(tokenFromRange(begin, end));
	}

	void ShaderCompiler::saveToCache(const std::filesystem::path& absPath)
	{
		LOG_FUNC("ShaderCompiler::saveToCache", absPath.u8string());
		const auto compilerMd5 = getD3DCompilerFuncs().md5;
		if (compilerMd5.empty() || m_contentMd5.empty())
		{
			LOG_DEBUG << "Missing MD5";
			return;
		}

		ShaderCacheHeader fileHeader = {};
		memcpy(fileHeader.compilerMd5.data(), compilerMd5.data(), compilerMd5.size());
		memcpy(fileHeader.shaderMd5.data(), m_contentMd5.data(), m_contentMd5.size());
		fileHeader.compilerFlags = COMPILER_FLAGS;
		fileHeader.vsSize = m_vs.code.size();
		fileHeader.psSize = m_ps.code.size();

		auto tmpPath(absPath);
		tmpPath += ".tmp";
		std::ofstream f(tmpPath, std::ios::binary);
		if (f.fail())
		{
			LOG_DEBUG << "Failed to open temporary file";
			return;
		}

		f.write(reinterpret_cast<char*>(&fileHeader), sizeof(fileHeader));
		if (f)
		{
			f.write(reinterpret_cast<char*>(m_vs.code.data()), m_vs.code.size());
		}
		if (f)
		{
			f.write(reinterpret_cast<char*>(m_ps.code.data()), m_ps.code.size());
		}

		if (f)
		{
			f.close();
			if (MoveFileExW(tmpPath.c_str(), absPath.c_str(), MOVEFILE_REPLACE_EXISTING))
			{
				return;
			}
			LOG_DEBUG << "MoveFileEx failed: " << Compat::hex(GetLastError());
		}
		else
		{
			f.close();
			LOG_DEBUG << "Failed to write into temporary file";
		}

		DeleteFileW(tmpPath.c_str());
	}
}
