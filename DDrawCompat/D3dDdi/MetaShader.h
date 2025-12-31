#pragma once

#include <array>
#include <filesystem>
#include <map>
#include <memory>
#include <string>

#include <Windows.h>
#include <d3d.h>

#include <D3dDdi/DeviceState.h>
#include <D3dDdi/ResourceDeleter.h>
#include <D3dDdi/ShaderCompiler.h>
#include <D3dDdi/SurfaceRepository.h>

namespace D3dDdi
{
	class Device;
	class Resource;

	class MetaShader
	{
	public:
		enum class ShaderStatus
		{
			Init,
			Preprocessed,
			Compiling,
			Compiled,
			ParseError,
			CompileError,
			SetupError,
			TextureError,
			RenderError,
			ResolutionError,
			SurfaceError
		};

		MetaShader(Device& device);

		static void clearUnusedBitmaps();
		static const std::vector<std::filesystem::path>& getBaseDirs();
		static void loadBitmaps();

		std::vector<ShaderCompiler::Parameter> getParameters() const;
		const std::filesystem::path& getRelPath() const { return m_relPath; }
		ShaderStatus getStatus() const { return m_status; }

		void init();
		void render(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect);
		void reset();
		void updateParameters();

	private:
		enum class ScaleType
		{
			Undefined,
			Source,
			Viewport,
			Absolute
		};

		struct RegisterRange
		{
			unsigned index;
			unsigned count;
		};

		struct Bitmap
		{
			std::shared_ptr<HBITMAP__> bitmap;
			ShaderStatus status = ShaderStatus::Init;
		};

		struct CompiledShader
		{
			std::vector<BYTE> code;
			std::map<std::string, RegisterRange> samplers;
			std::map<std::string, RegisterRange> uniforms;
			int minRegisterIndex = INT_MAX;
			int maxRegisterIndex = INT_MIN;
		};

		struct Shader
		{
			CompiledShader vs;
			CompiledShader ps;
			std::vector<ShaderCompiler::Parameter> parameters;
			std::map<std::string, unsigned> texCoords;
			ShaderStatus status = ShaderStatus::Init;
		};

		struct ShaderConsts
		{
			UINT firstConst = 0;
			std::vector<DeviceState::ShaderConstF> consts;
			float* frameCountReg = nullptr;
		};

		struct Texture;

		struct Sampler
		{
			unsigned regIndex = 0;
			Texture* texture = nullptr;
			int passIndex = 0;
		};

		static std::vector<std::filesystem::path> s_baseDirs;
		static std::map<std::filesystem::path, Shader> s_shaders;
		static std::map<std::filesystem::path, Bitmap> s_bitmaps;

		struct Pass
		{
			std::map<std::filesystem::path, Shader>::iterator shader = s_shaders.end();
			DeviceState::TempShader vs;
			DeviceState::TempShader ps;
			ShaderConsts vsConsts;
			ShaderConsts psConsts;
			std::vector<Sampler> vsSamplers;
			std::vector<Sampler> psSamplers;
			SurfaceRepository::Surface rtt;
			int frameCount = 0;
			SIZE outputSize = {};
			std::string alias;
			bool filter_linear = true;
			bool float_framebuffer = false;
			int frame_count_mod = 0;
			bool mipmap_input = false;
			ScaleType scale_type = ScaleType::Undefined;
			ScaleType scale_type_x = ScaleType::Undefined;
			ScaleType scale_type_y = ScaleType::Undefined;
			float scale = NAN;
			float scale_x = NAN;
			float scale_y = NAN;
			bool srgb_framebuffer = false;
			D3DTEXTUREADDRESS wrap_mode = D3DTADDRESS_BORDER;
		};

		struct InputFrame
		{
			SurfaceRepository::Surface rtt;
			long long qpc;
		};

		struct Texture
		{
			std::map<std::filesystem::path, Bitmap>::iterator bitmap = s_bitmaps.end();
			SurfaceRepository::Surface surface;
			bool linear = true;
			bool mipmap = false;
			D3DTEXTUREADDRESS wrap_mode = D3DTADDRESS_BORDER;
		};

		struct Vertex
		{
			std::array<float, 4> position;
			std::array<float, 2> texCoord[1];
		};

		static unsigned WINAPI compileThreadProc(LPVOID lpParameter);
		static void CALLBACK frameTimerCallback(UINT uTimerID, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dw1, DWORD_PTR dw2);
		static CompiledShader getCompiledShader(const ShaderCompiler::Shader& shader);
		static bool parseRegisterComment(const std::string& comment, CompiledShader& compiledShader);
		static ScaleType parseScaleType(const std::string& value);

		void compile();
		void createShaders(Pass& pass);
		void generateMipSubLevels(const Resource& resource);
		D3DDDIFORMAT getFormat(bool float_framebuffer);
		int getOutputSize(int inputSize, int vpSize, ScaleType scale_type, float scale) const;
		int getPassIndexFromStructName(const std::string& structName, int passIndex) const;
		SurfaceRepository::Surface& getPassRtt(int passIndex);
		Pass& getPass(int passIndex);
		InputFrame& getPrevInputFrame(int prevIndex);
		void getSurface(SurfaceRepository::Surface& surface, D3DDDIFORMAT format, SIZE size, DWORD caps);
		Vertex& getVertex(int index);
		void loadCgp(const std::filesystem::path& relPath);
		void loadTexture(Texture& texture);
		bool parseCgp(const std::filesystem::path& baseDir);
		void renderPass(const RECT& srcRect, int passIndex);
		void setExternalPass(Pass& pass, const Resource& resource, UINT subResourceIndex, const RECT& rect);
		bool setKey(const std::string& key, const std::string& value);
		void setStatus(ShaderStatus status);
		void setTexture(UINT stage, const Resource& resource, D3DTEXTUREADDRESS wrap, bool linear, bool srgb);
		void setTextureCoords(UINT stage, const RECT& rect, UINT width, UINT height, int passIndex);
		void setupPrevInputFrames();
		bool setupSamplerForPassInput(const std::string& field, unsigned regIndex,
			std::vector<Sampler>& samplers, int passIndex);
		bool setupSampler(const std::string& name, const RegisterRange& reg,
			std::vector<Sampler>& samplers, int passIndex);
		void setupSamplers(const CompiledShader& compiledShader,
			std::vector<Sampler>& samplers, int passIndex);
		bool setupUniformForPassInput(const std::string& field, unsigned regIndex,
			ShaderConsts& consts, int passIndex);
		bool setupUniform(const std::string& name, const RegisterRange& reg, ShaderConsts& consts, int passIndex);
		void setupUniforms(const CompiledShader& compiledShader, ShaderConsts& consts, int passIndex);
		void setupVertexShaderDecl();
		void setupVertices(int passIndex);
		void setup();
		void updateSamplers(const std::vector<Sampler>& samplers);
		void validateScale(int passIndex, ScaleType& scale_type_xy, float& scale_xy);
		void validatePass(int passIndex);

		Device& m_device;
		std::unique_ptr<void, ResourceDeleter> m_vertexShaderDecl;
		std::vector<BYTE> m_vertices;
		std::filesystem::path m_absPath;
		const std::filesystem::path* m_baseDir;
		std::filesystem::path m_relPath;
		InputFrame m_prevInputFrameCandidate;
		std::array<InputFrame, 7> m_prevInputFrames;
		Pass m_srcPass;
		std::array<Pass, 64> m_passes;
		Pass m_dstPass;
		std::map<std::string, float> m_parameters;
		std::map<std::string, Texture> m_textures;
		int m_maxPrevInputFrames;
		int m_prevInputFrameCount;
		int m_prevInputFrameIndex;
		int m_passCount;
		UINT m_vertexSize;
		UINT m_frameTimer;
		int m_frameCount;
		SIZE m_prevInputSize;
		ShaderStatus m_status;
	};
};
