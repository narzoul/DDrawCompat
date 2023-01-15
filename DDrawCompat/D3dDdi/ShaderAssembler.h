#pragma once

#include <ostream>
#include <string>
#include <vector>

#include <Windows.h>

namespace D3dDdi
{
	class ShaderAssembler
	{
	public:
		ShaderAssembler(const UINT* code, DWORD size);

		std::string disassemble();

	private:
		enum ShaderType
		{
			Vertex = 0xFFFE,
			Pixel = 0xFFFF
		};

		void disassembleComment(std::ostream& os, UINT tokenCount);
		void disassembleConstToken(std::ostream& os, UINT opcode);
		void disassembleDclPs(std::ostream& os);
		void disassembleDclVs(std::ostream& os);
		void disassembleDestinationParameter(std::ostream& os);
		UINT disassembleInstruction(std::ostream& os, UINT& indent);
		void disassembleRegister(std::ostream& os, UINT token);
		void disassembleSourceParameter(std::ostream& os);
		void disassembleSourceSwizzle(std::ostream& os, UINT token);
		void disassembleVersion(std::ostream& os);
		UINT getRemainingTokenCount() const;
		ShaderType getShaderType() const;
		UINT readToken();
		const UINT* readTokens(UINT count);

		template <typename Token>
		Token readToken();

		std::vector<UINT> m_tokens;
		UINT m_pos;
	};
}
