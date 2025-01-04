#include <algorithm>
#include <array>
#include <map>
#include <sstream>

#include <d3d9.h>

#include <Common/Log.h>
#include <D3dDdi/ShaderAssembler.h>

namespace
{
	const UINT BEGIN_BLOCK = 1;
	const UINT END_BLOCK = 2;
	const UINT32 PARAMETER_TOKEN_RESERVED_BIT = 0x80000000;

	typedef std::array<const char*, 7> Controls;

	const Controls CMP_CONTROLS = { nullptr, "gt", "eq", "ge", "lt", "ne", "le" };
	const Controls TEXLD_CONTROLS = { "", "p", "b" };

	struct CommentToken
	{
		UINT32 opcode : 16;
		UINT32 tokenCount : 15;
		UINT32 : 1;
	};

	struct Instruction
	{
		const char* name;
		UINT dstCount = 0;
		UINT srcCount = 0;
		UINT extraCount = 0;
		UINT indent = 0;
		const Controls* controls = nullptr;
	};

	struct InstructionToken
	{
		UINT32 opcode : 16;
		UINT32 control : 8;
		UINT32 tokenCount : 4;
		UINT32 isPredicated : 1;
		UINT32 : 3;
	};

	struct VersionToken
	{
		UINT32 minor : 8;
		UINT32 major : 8;
		UINT32 type : 16;
	};

	class RestorePos
	{
	public:
		RestorePos(UINT& pos) : m_pos(pos), m_origPos(pos) { }
		~RestorePos() { m_pos = m_origPos; }

	private:
		UINT& m_pos;
		UINT m_origPos;
	};

	void setRegisterType(UINT32& token, D3DSHADER_PARAM_REGISTER_TYPE registerType);

	std::map<UINT16, Instruction> g_instructionMap = {
		{ D3DSIO_NOP, { "nop" } },
		{ D3DSIO_MOV, { "mov", 1, 1 } },
		{ D3DSIO_ADD, { "add", 1, 2 } },
		{ D3DSIO_SUB, { "sub", 1, 2 } },
		{ D3DSIO_MAD, { "mad", 1, 3 } },
		{ D3DSIO_MUL, { "mul", 1, 2 } },
		{ D3DSIO_RCP, { "rcp", 1, 1 } },
		{ D3DSIO_RSQ, { "rsq", 1, 1 } },
		{ D3DSIO_DP3, { "dp3", 1, 2 } },
		{ D3DSIO_DP4, { "dp4", 1, 2 } },
		{ D3DSIO_MIN, { "min", 1, 2 } },
		{ D3DSIO_MAX, { "max", 1, 2 } },
		{ D3DSIO_SLT, { "slt", 1, 2 } },
		{ D3DSIO_SGE, { "sge", 1, 2 } },
		{ D3DSIO_EXP, { "exp", 1, 1 } },
		{ D3DSIO_LOG, { "log", 1, 1 } },
		{ D3DSIO_LIT, { "lit", 1, 1 } },
		{ D3DSIO_DST, { "dst", 1, 2 } },
		{ D3DSIO_LRP, { "lrp", 1, 3 } },
		{ D3DSIO_FRC, { "frc", 1, 1 } },
		{ D3DSIO_M4x4, { "m4x4", 1, 2 } },
		{ D3DSIO_M4x3, { "m4x3", 1, 2 } },
		{ D3DSIO_M3x4, { "m3x4", 1, 2 } },
		{ D3DSIO_M3x3, { "m3x3", 1, 2 } },
		{ D3DSIO_M3x2, { "m3x2", 1, 2 } },
		{ D3DSIO_CALL, { "call", 0, 1 } },
		{ D3DSIO_CALLNZ, { "callnz", 0, 2 } },
		{ D3DSIO_LOOP, { "loop", 0, 1, 0, BEGIN_BLOCK } },
		{ D3DSIO_RET, { "ret" } },
		{ D3DSIO_ENDLOOP, { "endloop", 0, 0, 0, END_BLOCK } },
		{ D3DSIO_LABEL, { "label", 0, 1 } },
		{ D3DSIO_DCL, { "dcl", 1, 0, 1 } },
		{ D3DSIO_POW, { "pow", 1, 2 } },
		{ D3DSIO_CRS, { "crs", 1, 2 } },
		{ D3DSIO_SGN, { "sgn", 1, 3 } },
		{ D3DSIO_ABS, { "abs", 1, 1 } },
		{ D3DSIO_NRM, { "nrm", 1, 1 } },
		{ D3DSIO_SINCOS, { "sincos", 1, 3 } },
		{ D3DSIO_REP, { "rep", 0, 1, 0, BEGIN_BLOCK } },
		{ D3DSIO_ENDREP, { "endrep", 0, 0, 0, END_BLOCK } },
		{ D3DSIO_IF, { "if", 0, 1, 0, BEGIN_BLOCK } },
		{ D3DSIO_IFC, { "if_", 0, 2, 0, BEGIN_BLOCK, &CMP_CONTROLS } },
		{ D3DSIO_ELSE, { "else", 0, 0, 0, BEGIN_BLOCK | END_BLOCK } },
		{ D3DSIO_ENDIF, { "endif", 0, 0, 0, END_BLOCK } },
		{ D3DSIO_BREAK, { "break" } },
		{ D3DSIO_BREAKC, { "break_", 0, 2, 0, 0, &CMP_CONTROLS } },
		{ D3DSIO_MOVA, { "mova", 1, 1 } },
		{ D3DSIO_DEFB, { "defb", 1, 0, 1 } },
		{ D3DSIO_DEFI, { "defi", 1, 0, 4 } },
		{ D3DSIO_TEXKILL, { "texkill", 1 } },
		{ D3DSIO_TEX, { "texld", 1, 2, 0, 0, &TEXLD_CONTROLS } },
		{ D3DSIO_TEXBEM, { "texbem", 1, 1 } },
		{ D3DSIO_TEXBEML, { "texbeml", 1, 1 } },
		{ D3DSIO_TEXREG2AR, { "texreg2ar", 1, 1 } },
		{ D3DSIO_TEXM3x2PAD, { "texm3x2pad", 1, 1 } },
		{ D3DSIO_TEXM3x2TEX, { "texm3x2tex", 1, 1 } },
		{ D3DSIO_TEXM3x3PAD, { "texm3x3pad", 1, 1 } },
		{ D3DSIO_TEXM3x3TEX, { "texm3x3tex", 1, 1 } },
		{ D3DSIO_TEXM3x3SPEC, { "texm3x3spec", 1, 2 } },
		{ D3DSIO_TEXM3x3VSPEC, { "texm3x3vspec", 1, 1 } },
		{ D3DSIO_EXPP, { "expp", 1, 1 } },
		{ D3DSIO_LOGP, { "logp", 1, 1 } },
		{ D3DSIO_CND, { "cnd", 1, 3 } },
		{ D3DSIO_DEF, { "def", 1, 0, 4 } },
		{ D3DSIO_TEXREG2RGB, { "texreg2rgb", 1, 1 } },
		{ D3DSIO_TEXDP3TEX, { "texdp3tex", 1, 1 } },
		{ D3DSIO_TEXM3x2DEPTH, { "texm3x2depth", 1, 1 } },
		{ D3DSIO_TEXDP3, { "texdp3", 1, 1 } },
		{ D3DSIO_TEXM3x3, { "texm3x3", 1, 1 } },
		{ D3DSIO_TEXDEPTH, { "texdepth", 1 } },
		{ D3DSIO_CMP, { "cmp", 1, 3 } },
		{ D3DSIO_BEM, { "bem", 1, 2 } },
		{ D3DSIO_DP2ADD, { "dp2add", 1, 3 } },
		{ D3DSIO_DSX, { "dsx", 1, 1 } },
		{ D3DSIO_DSY, { "dsy", 1, 1 } },
		{ D3DSIO_TEXLDD, { "texldd", 1, 2 } },
		{ D3DSIO_SETP, { "setp_", 1, 2, 0, 0, &CMP_CONTROLS } },
		{ D3DSIO_TEXLDL, { "texldl", 1, 2 } },
		{ D3DSIO_BREAKP, { "break pred", 0, 1 } },
		{ D3DSIO_PHASE, { "phase" } },
		{ D3DSIO_COMMENT, { "// " } },
		{ D3DSIO_END, { "end" } }
	};

	std::map<D3DSHADER_PARAM_REGISTER_TYPE, const char*> g_registerMap = {
		{ D3DSPR_TEMP, "r" },
		{ D3DSPR_INPUT, "v" },
		{ D3DSPR_CONST, "c" },
		{ D3DSPR_ATTROUT, "oD"},
		{ D3DSPR_TEXCRDOUT, "oT" },
		{ D3DSPR_CONSTINT, "i" },
		{ D3DSPR_COLOROUT, "oD" },
		{ D3DSPR_DEPTHOUT, "oDepth" },
		{ D3DSPR_SAMPLER, "s" },
		{ D3DSPR_CONSTBOOL, "b" },
		{ D3DSPR_LABEL, "l" },
		{ D3DSPR_PREDICATE, "p" }
	};

	std::map<UINT, const char*> g_sourceModifierPrefixMap = {
		{ D3DSPSM_NEG, "-" },
		{ D3DSPSM_BIASNEG, "-" },
		{ D3DSPSM_SIGNNEG, "-" },
		{ D3DSPSM_COMP, "1-" },
		{ D3DSPSM_X2NEG, "-" },
		{ D3DSPSM_ABSNEG, "-" },
		{ D3DSPSM_NOT, "!" }
	};

	std::map<UINT, const char*> g_sourceModifierSuffixMap = {
		{ D3DSPSM_BIAS, "bias" },
		{ D3DSPSM_SIGN, "bx2" },
		{ D3DSPSM_X2, "x2" },
		{ D3DSPSM_DZ, "dz" },
		{ D3DSPSM_DW, "dw" },
		{ D3DSPSM_ABS, "abs" }
	};

	std::map<UINT, const char*> g_textureTypeMap = {
		{ D3DSTT_UNKNOWN, nullptr },
		{ D3DSTT_2D, "2d" },
		{ D3DSTT_CUBE, "cube" },
		{ D3DSTT_VOLUME, "volume" }
	};

	std::map<D3DDECLUSAGE, const char*> g_usageMap = {
		{ D3DDECLUSAGE_POSITION, "position" },
		{ D3DDECLUSAGE_BLENDWEIGHT, "blendweight" },
		{ D3DDECLUSAGE_BLENDINDICES, "blendindices" },
		{ D3DDECLUSAGE_NORMAL, "normal" },
		{ D3DDECLUSAGE_PSIZE, "psize" },
		{ D3DDECLUSAGE_TEXCOORD, "texcoord" },
		{ D3DDECLUSAGE_TANGENT, "tangent" },
		{ D3DDECLUSAGE_BINORMAL, "binormal" },
		{ D3DDECLUSAGE_TESSFACTOR, "tessfactor" },
		{ D3DDECLUSAGE_POSITIONT, "positiont" },
		{ D3DDECLUSAGE_COLOR, "color" },
		{ D3DDECLUSAGE_FOG, "fog" },
		{ D3DDECLUSAGE_DEPTH, "depth" },
		{ D3DDECLUSAGE_SAMPLE, "sample" }
	};

	UINT getFreeRegisterNumber(const std::set<UINT>& usedRegisterNumbers)
	{
		UINT prev = UINT_MAX;
		for (UINT num : usedRegisterNumbers)
		{
			if (num > prev + 1)
			{
				return prev + 1;
			}
		}
		return usedRegisterNumbers.empty() ? 0 : (*usedRegisterNumbers.rbegin() + 1);
	}

	D3DSHADER_PARAM_REGISTER_TYPE getRegisterType(UINT32 token)
	{
		return static_cast<D3DSHADER_PARAM_REGISTER_TYPE>(
			((token & D3DSP_REGTYPE_MASK) >> D3DSP_REGTYPE_SHIFT) |
			((token & D3DSP_REGTYPE_MASK2) >> D3DSP_REGTYPE_SHIFT2));
	}

	UINT32 makeConstToken(FLOAT value)
	{
		return *reinterpret_cast<UINT32*>(&value);
	}

	UINT32 makeDestinationParameterToken(D3DSHADER_PARAM_REGISTER_TYPE registerType, UINT32 registerNumber,
		UINT32 writeMask, UINT32 modifiers)
	{
		UINT32 token = PARAMETER_TOKEN_RESERVED_BIT | registerNumber | writeMask | modifiers;
		setRegisterType(token, registerType);
		return token;
	}

	UINT32 makeInstructionToken(D3DSHADER_INSTRUCTION_OPCODE_TYPE opcode)
	{
		auto& inst = g_instructionMap.at(static_cast<UINT16>(opcode));
		auto tokenCount = inst.dstCount + inst.srcCount + inst.extraCount;
		return opcode | (tokenCount << D3DSI_INSTLENGTH_SHIFT);
	}

	UINT32 makeSourceParameterToken(D3DSHADER_PARAM_REGISTER_TYPE registerType, UINT32 registerNumber,
		UINT32 swizzle, D3DSHADER_PARAM_SRCMOD_TYPE modifier)
	{
		UINT32 token = PARAMETER_TOKEN_RESERVED_BIT | registerNumber | swizzle | modifier;
		setRegisterType(token, registerType);
		return token;
	}

	UINT reserveRegisterNumber(std::set<UINT>& usedRegisterNumbers)
	{
		auto num = getFreeRegisterNumber(usedRegisterNumbers);
		usedRegisterNumbers.insert(num);
		return num;
	}

	void setRegisterType(UINT32& token, D3DSHADER_PARAM_REGISTER_TYPE registerType)
	{
		token &= ~(D3DSP_REGTYPE_MASK | D3DSP_REGTYPE_MASK2);
		token |= ((registerType << D3DSP_REGTYPE_SHIFT) & D3DSP_REGTYPE_MASK) |
			((registerType << D3DSP_REGTYPE_SHIFT2) & D3DSP_REGTYPE_MASK2);
	}
}

namespace D3dDdi
{
	ShaderAssembler::ShaderAssembler(const UINT* code, DWORD size)
		: m_tokens(code, code + size)
		, m_pos(0)
	{
	}

	bool ShaderAssembler::addAlphaTest(UINT alphaRef)
	{
		LOG_FUNC("ShaderAssembler::addAlphaTest", alphaRef);
		LOG_DEBUG << "Original bytecode: " << Compat::hexDump(m_tokens.data(), m_tokens.size() * 4);
		LOG_DEBUG << disassemble();

		RestorePos restorePos(m_pos);
		m_pos = 0;
		UINT constRegNum = UINT_MAX;
		UINT tempRegNum = UINT_MAX;

		while (nextInstruction())
		{
			auto instruction = getToken<InstructionToken>();
			if (D3DSIO_TEX != instruction.opcode)
			{
				continue;
			}

			const auto dst = getToken<UINT32>(1);
			const auto src = getToken<UINT32>(3);
			const auto samplerRegNum = src & D3DSP_REGNUM_MASK;

			if (UINT_MAX == constRegNum)
			{
				auto usedConstRegNums = getUsedRegisterNumbers(D3DSPR_CONST);
				constRegNum = reserveRegisterNumber(usedConstRegNums);
				if (constRegNum >= 32)
				{
					LOG_ONCE("ERROR: no free PS const register found");
					return false;
				}

				auto usedTempRegNums = getUsedRegisterNumbers(D3DSPR_TEMP);
				tempRegNum = reserveRegisterNumber(usedTempRegNums);
				if (tempRegNum >= 32)
				{
					LOG_ONCE("ERROR: no free PS temp register found");
					return false;
				}
			}

			nextInstruction();

			insertToken(makeInstructionToken(D3DSIO_IF));
			insertToken(makeSourceParameterToken(D3DSPR_CONSTBOOL, samplerRegNum, D3DSP_NOSWIZZLE, D3DSPSM_NONE));

			insertToken(makeInstructionToken(D3DSIO_SUB));
			insertToken(makeDestinationParameterToken(D3DSPR_TEMP, tempRegNum, D3DSP_WRITEMASK_ALL, D3DSPDM_NONE));
			insertToken(makeSourceParameterToken(D3DSPR_TEMP, dst & D3DSP_REGNUM_MASK, D3DSP_REPLICATEALPHA, D3DSPSM_NONE));
			insertToken(makeSourceParameterToken(D3DSPR_CONST, constRegNum, D3DSP_REPLICATEALPHA, D3DSPSM_NONE));

			insertToken(makeInstructionToken(D3DSIO_TEXKILL));
			insertToken(makeDestinationParameterToken(D3DSPR_TEMP, tempRegNum, D3DSP_WRITEMASK_ALL, D3DSPDM_NONE));

			insertToken(makeInstructionToken(D3DSIO_ENDIF));
			--m_pos;
		}

		if (UINT_MAX == constRegNum)
		{
			LOG_DEBUG << "No modifications needed";
			return false;
		}

		m_pos = 0;
		nextInstruction();
		insertToken(makeInstructionToken(D3DSIO_DEF));
		insertToken(makeDestinationParameterToken(D3DSPR_CONST, constRegNum, D3DSP_WRITEMASK_ALL, D3DSPSM_NONE));
		for (UINT i = 0; i < 3; ++i)
		{
			insertToken(makeConstToken(0));
		}
		insertToken(makeConstToken(alphaRef / 255.0f));

		LOG_DEBUG << "Modified bytecode: " << Compat::hexDump(m_tokens.data(), m_tokens.size() * 4);
		LOG_DEBUG << disassemble();
		return true;
	}

	void ShaderAssembler::applyTexCoordIndexes(const std::array<UINT, 8>& texCoordIndexes)
	{
		LOG_FUNC("ShaderAssembler::applyTexCoordIndex", Compat::array(texCoordIndexes.data(), texCoordIndexes.size()));
		LOG_DEBUG << "Original bytecode: " << Compat::hexDump(m_tokens.data(), m_tokens.size() * 4);
		LOG_DEBUG << disassemble();

		RestorePos restorePos(m_pos);
		m_pos = 0;
		std::array<UINT, 8> tcIndexToRegNum = {};
		std::array<UINT, 16> regNumToTcIndex = {};
		regNumToTcIndex.fill(UINT_MAX);

		while (nextInstruction())
		{
			const auto instruction = getToken<InstructionToken>();
			if (D3DSIO_DCL == instruction.opcode)
			{
				const auto usage = getToken<UINT32>(1);
				if (D3DDECLUSAGE_TEXCOORD == (usage & D3DSP_DCL_USAGE_MASK))
				{
					const auto tcIndex = (usage & D3DSP_DCL_USAGEINDEX_MASK) >> D3DSP_DCL_USAGEINDEX_SHIFT;
					const auto dst = getToken<UINT32>(2);
					const auto regNum = dst & D3DSP_REGNUM_MASK;
					tcIndexToRegNum[tcIndex] = regNum;
					regNumToTcIndex[regNum] = tcIndex;
				}
				continue;
			}

			const auto it = g_instructionMap.find(instruction.opcode);
			if (it == g_instructionMap.end())
			{
				continue;
			}

			for (UINT i = 0; i < it->second.srcCount; ++i)
			{
				UINT& src = m_tokens[m_pos + 1 + it->second.dstCount + i];
				const auto regType = getRegisterType(src);
				if (D3DSPR_INPUT != regType)
				{
					continue;
				}

				const auto origRegNum = src & D3DSP_REGNUM_MASK;
				const auto origTcIndex = regNumToTcIndex[origRegNum];
				if (origTcIndex >= texCoordIndexes.size())
				{
					continue;
				}

				const auto mappedTcIndex = texCoordIndexes[origTcIndex] & 7;
				const auto mappedRegNum = tcIndexToRegNum[mappedTcIndex];
				src &= ~D3DSP_REGNUM_MASK;
				src |= mappedRegNum;
			}
		}

		LOG_DEBUG << "Modified bytecode: " << Compat::hexDump(m_tokens.data(), m_tokens.size() * 4);
		LOG_DEBUG << disassemble();
	}

	std::string ShaderAssembler::disassemble()
	{
		if (Compat::Log::getLogLevel() < Config::Settings::LogLevel::DEBUG)
		{
			return {};
		}

		RestorePos restorePos(m_pos);
		m_pos = 0;
		std::ostringstream os;
		os << "Disassembled shader code:" << std::endl;

		try
		{
			disassembleVersion(os);
			UINT indent = 0;
			while (0 != getRemainingTokenCount())
			{
				os << std::endl;
				if (D3DSIO_END == disassembleInstruction(os, indent))
				{
					break;
				}
			}
		}
		catch (std::exception& e)
		{
			os << e.what();
		}

		return os.str();
	}

	void ShaderAssembler::disassembleComment(std::ostream& os, UINT tokenCount)
	{
		auto begin = reinterpret_cast<const char*>(readTokens(tokenCount));
		auto end = begin + tokenCount * 4;
		end = std::find_if(begin, end, [](char c) { return !std::isprint(c); });
		os.write(begin, end - begin);
	}

	void ShaderAssembler::disassembleConstToken(std::ostream& os, UINT opcode)
	{
		switch (opcode)
		{
		case D3DSIO_DEFB:
		case D3DSIO_DEFI:
			os << readToken<INT>();
			break;

		default:
			os << readToken<FLOAT>();
			break;
		}
	}

	void ShaderAssembler::disassembleDclPs(std::ostream& os)
	{
		auto token = readToken();
		auto type = token & D3DSP_TEXTURETYPE_MASK;
		auto it = g_textureTypeMap.find(type);
		if (it == g_textureTypeMap.end())
		{
			throw std::runtime_error("Unknown dcl texture type: " + std::to_string(type >> D3DSP_TEXTURETYPE_SHIFT));
		}
		if (it->second)
		{
			os << '_' << it->second;
		}
	}

	void ShaderAssembler::disassembleDclVs(std::ostream& os)
	{
		auto token = readToken();
		auto usage = static_cast<D3DDECLUSAGE>(token & D3DSP_DCL_USAGE_MASK);
		auto it = g_usageMap.find(usage);
		if (it == g_usageMap.end())
		{
			throw std::runtime_error("Unknown dcl usage: " + std::to_string(usage));
		}
		os << '_' << it->second;

		auto usageIndex = (token & D3DSP_DCL_USAGEINDEX_MASK) >> D3DSP_DCL_USAGEINDEX_SHIFT;
		if (0 != usageIndex)
		{
			os << usageIndex;
		}
	}

	void ShaderAssembler::disassembleDestinationParameter(std::ostream& os)
	{
		auto token = readToken();
		auto dstMod = token & D3DSP_DSTMOD_MASK;
		if (dstMod & D3DSPDM_SATURATE)
		{
			os << "_sat";
		}
		if (dstMod & D3DSPDM_PARTIALPRECISION)
		{
			os << "_pp";
		}
		if (dstMod & D3DSPDM_MSAMPCENTROID)
		{
			os << "_centroid";
		}

		os << ' ';
		disassembleRegister(os, token);

		auto writeMask = token & D3DSP_WRITEMASK_ALL;
		if (D3DSP_WRITEMASK_ALL != writeMask)
		{
			os << '.';
			if (writeMask & D3DSP_WRITEMASK_0)
			{
				os << 'x';
			}
			if (writeMask & D3DSP_WRITEMASK_1)
			{
				os << 'y';
			}
			if (writeMask & D3DSP_WRITEMASK_2)
			{
				os << 'z';
			}
			if (writeMask & D3DSP_WRITEMASK_3)
			{
				os << 'w';
			}
		}
	}

	UINT ShaderAssembler::disassembleInstruction(std::ostream& os, UINT& indent)
	{
		auto token = readToken<InstructionToken>();
		auto it = g_instructionMap.find(token.opcode);
		if (it == g_instructionMap.end())
		{
			throw std::runtime_error("Unknown opcode: " + std::to_string(token.opcode));
		}

		if ((it->second.indent & END_BLOCK) && indent > 0)
		{
			--indent;
		}

		os << std::string(2 * indent, ' ') << it->second.name;

		if (it->second.indent & BEGIN_BLOCK)
		{
			++indent;
		}

		if (D3DSIO_COMMENT == token.opcode)
		{
			disassembleComment(os, reinterpret_cast<const CommentToken*>(&token)->tokenCount);
			return token.opcode;
		}

		if (0 != token.control || it->second.controls)
		{
			auto control = (it->second.controls && token.control < it->second.controls->size())
				? it->second.controls->at(token.control)
				: nullptr;
			if (!control)
			{
				throw std::runtime_error("Unknown control: " + std::to_string(token.control) +
					", opcode: " + std::to_string(token.opcode));
			}
			os << control;
		}

		auto inst = it->second;
		auto extraCount = it->second.extraCount;
		auto tokenCount = inst.dstCount + inst.srcCount + extraCount;
		if (token.tokenCount != tokenCount)
		{
			throw std::runtime_error("Instruction length mismatch: expected " + std::to_string(token.tokenCount) +
				", got " + std::to_string(tokenCount) + ", opcode: " + std::to_string(token.opcode));
		}

		if (D3DSIO_DCL == token.opcode)
		{
			if (Pixel == getShaderType())
			{
				disassembleDclPs(os);
			}
			else
			{
				disassembleDclVs(os);
			}
			--extraCount;
		}

		const char* separator = " ";
		if (it->second.dstCount)
		{
			disassembleDestinationParameter(os);
			separator = ", ";
		}

		for (UINT i = 0; i < it->second.srcCount; ++i)
		{
			os << separator;
			disassembleSourceParameter(os);
			separator = ", ";
		}

		for (UINT i = 0; i < extraCount; ++i)
		{
			os << separator;
			disassembleConstToken(os, token.opcode);
			separator = ", ";
		}

		return token.opcode;
	}

	void ShaderAssembler::disassembleRegister(std::ostream& os, UINT token)
	{
		auto registerType = getRegisterType(token);
		auto registerNumber = token & D3DSP_REGNUM_MASK;

		auto it = g_registerMap.find(registerType);
		if (it != g_registerMap.end())
		{
			os << it->second << registerNumber;
			return;
		}

		switch (registerType)
		{
		case D3DSPR_ADDR:
			os << (Pixel == getShaderType() ? 't' : 'a') << registerNumber;
			break;

		case D3DSPR_RASTOUT:
			switch (registerNumber)
			{
			case D3DSRO_POSITION:
				os << "oPos";
				break;
			case D3DSRO_FOG:
				os << "oFog";
				break;
			case D3DSRO_POINT_SIZE:
				os << "oPts";
				break;
			default:
				throw std::runtime_error("Unknown rasterizer output register number: " + std::to_string(registerNumber));
			}
			break;

		case D3DSPR_CONST2:
		case D3DSPR_CONST3:
		case D3DSPR_CONST4:
			os << 'c' << (registerType - D3DSPR_CONST2 + 1) * 2048 + registerNumber;
			break;

		default:
			os << "reg" << registerType << '/' << registerNumber;
			break;
		}
	}

	void ShaderAssembler::disassembleSourceParameter(std::ostream& os)
	{
		auto token = readToken();
		auto modifier = token & D3DSP_SRCMOD_MASK;
		auto it = g_sourceModifierPrefixMap.find(modifier);
		if (it != g_sourceModifierPrefixMap.end())
		{
			os << it->second;
		}

		disassembleRegister(os, token);

		it = g_sourceModifierSuffixMap.find(modifier);
		if (it != g_sourceModifierSuffixMap.end())
		{
			os << '_' << it->second;
		}

		disassembleSourceSwizzle(os, token);
	}

	void ShaderAssembler::disassembleSourceSwizzle(std::ostream& os, UINT token)
	{
		const char component[] = { 'x', 'y', 'z', 'w' };
		UINT swizzle = token & D3DVS_SWIZZLE_MASK;

		switch (swizzle)
		{
		case D3DSP_NOSWIZZLE:
			break;

		case D3DSP_REPLICATERED:
		case D3DSP_REPLICATEGREEN:
		case D3DSP_REPLICATEBLUE:
		case D3DSP_REPLICATEALPHA:
			os << '.' << component[(swizzle >> D3DVS_SWIZZLE_SHIFT) & 0x3];
			break;

		default:
			os << '.';
			swizzle >>= D3DVS_SWIZZLE_SHIFT;
			for (UINT i = 0; i < 4; ++i)
			{
				os << component[swizzle & 0x3];
				swizzle >>= 2;
			}
			break;
		}
	}

	void ShaderAssembler::disassembleVersion(std::ostream& os)
	{
		auto version = readToken<VersionToken>();
		os << ((Pixel == version.type) ? "ps" : "vs")
			<< '_' << static_cast<UINT>(version.major)
			<< '_' << static_cast<UINT>(version.minor);
		if (2 != version.major || 0 != version.minor)
		{
			throw std::runtime_error("Unsupported shader version");
		}
	}

	UINT ShaderAssembler::getRemainingTokenCount() const
	{
		return m_tokens.size() - m_pos;
	}

	ShaderAssembler::ShaderType ShaderAssembler::getShaderType() const
	{
		return static_cast<ShaderType>(m_tokens.front() >> 16);
	}

	UINT ShaderAssembler::getTextureStageCount()
	{
		auto usedSamplers = getUsedRegisterNumbers(D3DSPR_SAMPLER);
		return usedSamplers.empty() ? 0 : (*usedSamplers.rbegin() + 1);
	}

	template <typename Token>
	Token ShaderAssembler::getToken(UINT offset) const
	{
		auto pos = m_pos + offset;
		return pos < m_tokens.size() ? *reinterpret_cast<const Token*>(&m_tokens[pos]) : Token{};
	}

	std::set<UINT> ShaderAssembler::getUsedRegisterNumbers(int registerType)
	{
		RestorePos restorePos(m_pos);
		m_pos = 0;
		std::set<UINT> usedRegisterNumbers;
		while (nextInstruction())
		{
			auto it = g_instructionMap.find(getToken<InstructionToken>().opcode);
			if (it == g_instructionMap.end())
			{
				continue;
			}

			const UINT offset = D3DSIO_DCL == it->first ? 2 : 1;
			const auto tokenCount = it->second.dstCount + it->second.srcCount;
			for (UINT i = 0; i < tokenCount; ++i)
			{
				auto token = getToken<UINT32>(offset + i);
				if (registerType == getRegisterType(token))
				{
					usedRegisterNumbers.insert(token & D3DSP_REGNUM_MASK);
				}
			}
		}
		return usedRegisterNumbers;
	}

	void ShaderAssembler::insertToken(UINT32 token)
	{
		m_tokens.insert(m_tokens.begin() + m_pos, token);
		++m_pos;
	}

	bool ShaderAssembler::nextInstruction()
	{
		if (0 == m_pos)
		{
			m_pos = 1;
		}
		else
		{
			auto token = readToken<InstructionToken>();
			readTokens(token.tokenCount);
		}

		while (D3DSIO_COMMENT == getToken<InstructionToken>().opcode)
		{
			auto token = readToken<CommentToken>();
			readTokens(token.tokenCount);
		}
		return m_pos < m_tokens.size() && D3DSIO_END != getToken<InstructionToken>().opcode;
	}

	UINT ShaderAssembler::readToken()
	{
		return *readTokens(1);
	}

	template <typename Token>
	Token ShaderAssembler::readToken()
	{
		static_assert(4 == sizeof(Token));
		return *reinterpret_cast<const Token*>(readTokens(1));
	}

	const UINT* ShaderAssembler::readTokens(UINT count)
	{
		if (count > getRemainingTokenCount())
		{
			throw std::runtime_error("Unexpected end of code");
		}
		auto tokens = m_tokens.data() + m_pos;
		m_pos += count;
		return tokens;
	}
}
