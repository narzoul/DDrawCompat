#pragma once

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <Windows.h>
#include <d3dcompiler.h>

namespace D3dDdi
{
	class ShaderCompiler
	{
	public:
		typedef std::string_view Token;

		struct Parameter
		{
			std::string name;
			std::string description;
			float defaultValue;
			float currentValue;
			float min;
			float max;
			float step;
		};

		struct Pattern
		{
			Token pattern;
			bool isNegated = false;
			bool isOptional = false;
			bool isRegex = false;

			Pattern(Token pattern) : pattern(pattern) {}
			Pattern(const char* pattern) : pattern(pattern) {}
		};

		struct Not : Pattern
		{
			Not(const Pattern& p) : Pattern(p) { isNegated = true; }
		};

		struct Optional : Pattern
		{
			Optional(const Pattern& p) : Pattern(p) { isOptional = true; }
		};

		struct Regex : Pattern
		{
			Regex(const Pattern& p) : Pattern(p) { isRegex = true; }
		};

		struct Shader
		{
			std::vector<BYTE> code;
			std::string assembly;
		};

		typedef std::vector<Token>::const_iterator TokenIter;
		typedef std::initializer_list<Pattern> PatternSeq;

		struct TokenRange
		{
			TokenIter begin;
			TokenIter end;
		};

		ShaderCompiler(const std::filesystem::path& absPath);

		void compile();
		const std::vector<Parameter>* getParameters() const { return m_content.empty() ? nullptr : &m_parameters; }
		const std::map<std::string, unsigned>& getTexCoords() const { return m_texCoords; }
		const Shader& getPs() const { return m_ps; }
		const Shader& getVs() const { return m_vs; }

	private:
		class D3DInclude;

		struct Field
		{
			bool uniform;
			TokenIter type;
			TokenIter name;
			TokenIter semantic;
		};

		struct FunctionArg : Field
		{
			TokenIter begin;
			TokenIter end;
			unsigned dim;
			bool out;
		};

		struct Function
		{
			TokenRange mods;
			FunctionArg ret;
			TokenIter name;
			std::vector<FunctionArg> args;
			TokenRange body;
			std::map<Token, std::set<unsigned>> usedInputSemantics;
			std::map<Token, std::set<unsigned>> usedOutputSemantics;
			bool isMain;
		};

		void applyTokenPatches();
		Shader compile(const char* entry, const char* target, std::string& errors);
		bool exists(TokenIter it);
		TokenIter findBracketEnd(TokenIter begin, TokenIter end);
		TokenIter findToken(Token token);
		TokenIter findUnnestedTokenSequence(TokenIter begin, TokenIter end, PatternSeq seq);
		void gatherUsedSemantics(const FunctionArg& arg, std::map<Token, std::set<unsigned>>& usedSemantics);
		std::string getNextUnusedSemantic(Token semantic, std::map<Token, std::set<unsigned>>& usedSemantics);
		bool hasSampler(const std::vector<Field>& fields);
		std::string initStruct(std::map<Token, std::vector<Field>>::const_iterator s);
		std::string loadShaderFile(const std::filesystem::path& absPath);
		void logContent(const std::string& header);
		std::string makeUnique(Token token);
		Function parseFunction(TokenIter returnType);
		FunctionArg parseFunctionArg(TokenIter begin, TokenIter end);
		Field parseStructField(TokenIter begin, TokenIter end, TokenIter type);
		void parseStructs();
		void patchTokenSequence(TokenIter begin, TokenIter end, PatternSeq seq, const std::string& patch);
		void postprocessArrayCtors();
		void postprocessDeadBranches(TokenRange body);
		void postprocessFunctionArg(Function& func, const FunctionArg& arg);
		void postprocessFunctionRet(Function& func);
		void postprocessFunction(Function& func);
		void postprocessFunctions();
		void postprocessGlobalVariables();
		void postprocessScalarToVector();
		void postprocessStructs();
		void postprocessVariableInit(TokenIter begin, TokenIter end);
		void postprocess();
		void preprocess();
		void removeTokens(TokenIter begin, TokenIter end);
		std::string splitFunctionArg(Function& func, const FunctionArg& arg, const std::vector<Field>& fields);
		void tokenize();
		std::string toString(Token token);
		std::string toString(TokenIter it);
		std::string toString(TokenIter begin, TokenIter end);

		std::filesystem::path m_absPath;
		std::string m_content;
		std::vector<Parameter> m_parameters;
		std::map<std::string, unsigned> m_texCoords;
		std::map<Token, std::vector<Field>> m_structs;
		std::vector<Token> m_tokens;
		std::map<TokenIter, std::string> m_tokenPatches;
		Shader m_ps;
		Shader m_vs;
	};
}
