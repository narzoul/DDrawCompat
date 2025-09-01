#include <algorithm>
#include <cmath>
#include <regex>
#include <sstream>

#include <Windows.h>
#include <mmsystem.h>
#include <winnt.h>

#include <Common/Comparison.h>
#include <Common/Log.h>
#include <Common/Path.h>
#include <Common/Rect.h>
#include <Common/Time.h>
#include <Config/Settings/DisplayFilter.h>
#include <Config/Parser.h>
#include <D3dDdi/Adapter.h>
#include <D3dDdi/Device.h>
#include <D3dDdi/MetaShader.h>
#include <D3dDdi/Resource.h>
#include <D3dDdi/ScopedCriticalSection.h>
#include <D3dDdi/ShaderBlitter.h>
#include <DDraw/RealPrimarySurface.h>
#include <Dll/Dll.h>
#include <Gdi/GuiThread.h>
#include <Overlay/ConfigWindow.h>

namespace
{
	const int FRAME_INTERVAL = 20;

	UINT g_runningThreads = 0;
	UINT g_maxThreads = 0;

	std::string_view getNextCommentLine(const std::string& assembly, std::string_view prev);

	std::string_view getFirstRegisterCommentFromAssembly(const std::string& assembly)
	{
		auto comment = getNextCommentLine(assembly, {});
		const char REGISTERS[] = "Registers:";
		while (comment.data() && REGISTERS != comment.substr(0, sizeof(REGISTERS) - 1))
		{
			comment = getNextCommentLine(assembly, comment);
		}
		if (!comment.data())
		{
			return {};
		}

		comment = getNextCommentLine(assembly, comment);
		if (!comment.empty())
		{
			return {};
		}

		const char NAME[] = "Name";
		comment = getNextCommentLine(assembly, comment);
		if (NAME != comment.substr(0, sizeof(NAME) - 1))
		{
			return {};
		}

		comment = getNextCommentLine(assembly, comment);
		if (comment.empty() || '-' != comment[0])
		{
			return {};
		}

		comment = getNextCommentLine(assembly, comment);
		if (comment.empty())
		{
			return {};
		}

		return comment;
	}

	std::string_view getNextCommentLine(const std::string& assembly, std::string_view prev)
	{
		auto offset = prev.data() ? prev.data() - assembly.data() + prev.size() + 1 : 0;
		const auto endPos = assembly.find('\n', offset);
		if (std::string::npos == endPos ||
			endPos - offset < 2 ||
			'/' != assembly[offset] ||
			'/' != assembly[offset + 1])
		{
			return {};
		}

		offset += 2;
		while (offset < endPos && isspace(assembly[offset]))
		{
			++offset;
		}

		return { assembly.data() + offset, endPos - offset };
	}

	int getNextPow2(int value)
	{
		int pow = 1;
		while (value > pow)
		{
			pow *= 2;
		}
		return pow;
	}

	bool parseBool(const std::string& value)
	{
		if ("true" == value || "1" == value)
		{
			return true;
		}
		if ("false" == value || "0" == value)
		{
			return false;
		}
		throw D3dDdi::MetaShader::ShaderStatus::ParseError;
	}

	float parseFloat(const std::string& value)
	{
		std::istringstream iss(value);
		float f = 0;
		if (iss >> f)
		{
			return f;
		}
		throw D3dDdi::MetaShader::ShaderStatus::ParseError;
	}

	std::vector<std::string> parseList(const std::string& value)
	{
		std::vector<std::string> list;
		std::size_t pos = 0;
		while (pos < value.length())
		{
			const auto endPos = value.find_first_of(';', pos);
			const auto elem(Config::Parser::trim(value.substr(pos, endPos - pos)));
			if (!elem.empty())
			{
				list.push_back(elem);
			}
			pos = std::string::npos == endPos ? endPos : (endPos + 1);
		}
		return list;
	}

	D3DTEXTUREADDRESS parseWrapMode(const std::string& value)
	{
		if ("clamp_to_border" == value)
		{
			return D3DTADDRESS_BORDER;
		}
		if ("clamp_to_edge" == value)
		{
			return D3DTADDRESS_CLAMP;
		}
		if ("repeat" == value)
		{
			return D3DTADDRESS_WRAP;
		}
		if ("mirrored_repeat" == value)
		{
			return D3DTADDRESS_MIRROR;
		}
		throw D3dDdi::MetaShader::ShaderStatus::ParseError;
	}
}

namespace D3dDdi
{
	MetaShader::MetaShader(Device& device)
		: m_device(device)
		, m_frameTimer(0)
	{
		reset();
	}

	void MetaShader::compile()
	{
		if (ShaderStatus::Compiling != m_status)
		{
			return;
		}

		if (0 == g_maxThreads)
		{
			DWORD_PTR processAffinityMask = 0;
			DWORD_PTR systemAffinityMask = 0;
			GetProcessAffinityMask(GetCurrentProcess(), &processAffinityMask, &systemAffinityMask);
			while (0 != processAffinityMask)
			{
				g_maxThreads += processAffinityMask & 1;
				processAffinityMask >>= 1;
			}
			g_maxThreads = g_maxThreads > 2 ? g_maxThreads - 2 : 1;
		}

		auto status = ShaderStatus::Compiled;
		for (int i = 0; i < m_passCount; ++i)
		{
			switch (m_passes[i].shader->second.status)
			{
			case ShaderStatus::Init:
			{
				ShaderCompiler compiler(m_passes[i].shader->first);
				auto parameters = compiler.getParameters();
				if (!parameters)
				{
					m_passes[i].shader->second.status = ShaderStatus::CompileError;
					throw ShaderStatus::CompileError;
				}
				m_passes[i].shader->second.parameters = *parameters;
				m_passes[i].shader->second.status = ShaderStatus::Preprocessed;
			}
			[[fallthrough]];

			case ShaderStatus::Preprocessed:
				if (g_runningThreads < g_maxThreads)
				{
					HANDLE thread = Dll::createThread(&compileThreadProc, nullptr, THREAD_PRIORITY_TIME_CRITICAL, 0,
						const_cast<void*>(reinterpret_cast<const void*>(&*m_passes[i].shader)));
					if (thread)
					{
						m_passes[i].shader->second.status = ShaderStatus::Compiling;
						++g_runningThreads;
						CloseHandle(thread);
					}
					else
					{
						m_passes[i].shader->second.status = ShaderStatus::CompileError;
						throw ShaderStatus::CompileError;
					}
				}
				status = ShaderStatus::Compiling;
				break;

			case ShaderStatus::Compiling:
				status = ShaderStatus::Compiling;
				break;

			case ShaderStatus::CompileError:
				throw ShaderStatus::CompileError;
			}
		}

		bool startBitmapLoad = false;
		for (const auto& texture : m_textures)
		{
			switch (texture.second.bitmap->second.status)
			{
			case ShaderStatus::Init:
				texture.second.bitmap->second.status = ShaderStatus::Compiling;
				startBitmapLoad = true;
				break;

			case ShaderStatus::Compiling:
				status = ShaderStatus::Compiling;
				break;

			case ShaderStatus::CompileError:
				throw ShaderStatus::CompileError;
			};
		}

		if (startBitmapLoad)
		{
			status = ShaderStatus::Compiling;
			Gdi::GuiThread::executeAsyncFunc([]()
				{
					D3dDdi::ScopedCriticalSection lock;
					loadBitmaps();
				});
		}

		if (ShaderStatus::Compiled == status)
		{
			setup();
			setStatus(ShaderStatus::Compiled);
		}
	}

	unsigned WINAPI MetaShader::compileThreadProc(LPVOID lpParameter)
	{
		auto& shader = *reinterpret_cast<decltype(&*s_shaders.begin())>(lpParameter);
		D3dDdi::ShaderCompiler compiler(shader.first);
		compiler.compile();

		shader.second.vs = getCompiledShader(compiler.getVs());
		if (shader.second.vs.code.empty())
		{
			shader.second.status = ShaderStatus::CompileError;
		}
		else
		{
			shader.second.ps = getCompiledShader(compiler.getPs());
			if (shader.second.ps.code.empty())
			{
				shader.second.vs = {};
				shader.second.status = ShaderStatus::CompileError;
			}
			else
			{
				shader.second.texCoords = compiler.getTexCoords();
				shader.second.status = ShaderStatus::Compiled;
			}
		}

		D3dDdi::ScopedCriticalSection lock;
		--g_runningThreads;

		for (auto& device : D3dDdi::Device::getDevices())
		{
			auto& metaShader = device.second.getShaderBlitter().getMetaShader();
			if (ShaderStatus::Compiling == metaShader.m_status)
			{
				try
				{
					metaShader.compile();
				}
				catch (ShaderStatus status)
				{
					metaShader.setStatus(status);
				}
			}
		}

		return 0;
	}

	void MetaShader::createShaders(Pass& pass)
	{
		D3DDDIARG_CREATEVERTEXSHADERFUNC vs = {};
		vs.Size = pass.shader->second.vs.code.size();
		if (FAILED(m_device.getOrigVtable().pfnCreateVertexShaderFunc(
			m_device, &vs, reinterpret_cast<UINT*>(pass.shader->second.vs.code.data()))))
		{
			LOG_DEBUG << "Failed to create vertex shader: " << pass.shader->first.u8string();
			throw ShaderStatus::SetupError;
		}
		pass.vs = { vs.ShaderHandle, ResourceDeleter(m_device, m_device.getOrigVtable().pfnDeleteVertexShaderFunc) };

		D3DDDIARG_CREATEPIXELSHADER ps = {};
		ps.CodeSize = pass.shader->second.ps.code.size();
		if (FAILED(m_device.getOrigVtable().pfnCreatePixelShader(
			m_device, &ps, reinterpret_cast<UINT*>(pass.shader->second.ps.code.data()))))
		{
			LOG_DEBUG << "Failed to create pixel shader: " << pass.shader->first.u8string();
			throw ShaderStatus::SetupError;
		}
		pass.ps = { ps.ShaderHandle, ResourceDeleter(m_device, m_device.getOrigVtable().pfnDeletePixelShader) };
	}

	void CALLBACK MetaShader::frameTimerCallback(
		UINT /*uTimerID*/, UINT /*uMsg*/, DWORD_PTR dwUser, DWORD_PTR /*dw1*/, DWORD_PTR /*dw2*/)
	{
		D3dDdi::ScopedCriticalSection lock;
		const auto self = reinterpret_cast<MetaShader*>(dwUser);
		if (ShaderStatus::Compiled == self->getStatus())
		{
			self->m_frameCount++;
			for (int i = 0; i < self->m_passCount; ++i)
			{
				auto& pass = self->m_passes[i];
				if (self->m_passes[i].vsConsts.frameCountReg ||
					self->m_passes[i].psConsts.frameCountReg)
				{
					pass.frameCount = 0 == pass.frame_count_mod
						? self->m_frameCount : (self->m_frameCount % pass.frame_count_mod);
					if (self->m_passes[i].vsConsts.frameCountReg)
					{
						*self->m_passes[i].vsConsts.frameCountReg = static_cast<float>(pass.frameCount);
					}
					if (self->m_passes[i].psConsts.frameCountReg)
					{
						*self->m_passes[i].psConsts.frameCountReg = static_cast<float>(pass.frameCount);
					}
				}
			}
			DDraw::RealPrimarySurface::scheduleOverlayUpdate();
		}
	}

	void MetaShader::generateMipSubLevels(const Resource& resource)
	{
		for (unsigned i = 1; i < resource.getFixedDesc().SurfCount; ++i)
		{
			m_device.getShaderBlitter().textureBlt(resource, i, resource.getRect(i),
				resource, i - 1, resource.getRect(i - 1), D3DTEXF_LINEAR | D3DTEXF_SRGB);
		}
	}

	const std::vector<std::filesystem::path>& MetaShader::getBaseDirs()
	{
		if (s_baseDirs.empty())
		{
			const std::filesystem::path common("common-shaders-master");
			s_baseDirs.push_back(Compat::getModulePath(nullptr).remove_filename() / common);
			s_baseDirs.push_back(Compat::getEnvPath("LOCALAPPDATA") / "DDrawCompat" / common);
			s_baseDirs.push_back(Compat::getEnvPath("PROGRAMDATA") / "DDrawCompat" / common);
		}
		return s_baseDirs;
	}

	MetaShader::CompiledShader MetaShader::getCompiledShader(const ShaderCompiler::Shader& shader)
	{
		if (shader.code.empty())
		{
			return {};
		}

		auto comment = getFirstRegisterCommentFromAssembly(shader.assembly);
		if (!comment.data())
		{
			LOG_DEBUG << "Failed to find constant table in shader assembly";
		}

		CompiledShader compiledShader;
		while (!comment.empty())
		{
			if (!parseRegisterComment(std::string(comment), compiledShader))
			{
				return {};
			}
			comment = getNextCommentLine(shader.assembly, comment);
		}

		compiledShader.code = shader.code;
		return compiledShader;
	}

	D3DDDIFORMAT MetaShader::getFormat(bool float_framebuffer)
	{
		if (float_framebuffer)
		{
			const auto& formatOps = m_device.getAdapter().getInfo().formatOps;
			auto it = formatOps.find(D3DDDIFMT_A32B32G32R32F);
			if (it != formatOps.end() &&
				(it->second.Operations & FORMATOP_TEXTURE) &&
				(it->second.Operations & FORMATOP_OFFSCREEN_RENDERTARGET))
			{
				return D3DDDIFMT_A32B32G32R32F;
			}

			it = formatOps.find(D3DDDIFMT_A16B16G16R16F);
			if (it != formatOps.end() &&
				(it->second.Operations & FORMATOP_TEXTURE) &&
				(it->second.Operations & FORMATOP_OFFSCREEN_RENDERTARGET))
			{
				return D3DDDIFMT_A16B16G16R16F;
			}
		}

		return m_srcPass.rtt.format;
	}

	int MetaShader::getOutputSize(int inputSize, int vpSize, ScaleType scale_type, float scale) const
	{
		switch (scale_type)
		{
		case ScaleType::Viewport:
			return static_cast<int>(vpSize * scale);
		case ScaleType::Absolute:
			return static_cast<int>(scale);
		default:
			return static_cast<int>(inputSize * scale);
		}
	}

	int MetaShader::getPassIndexFromStructName(const std::string& structName, int passIndex) const
	{
		if ("ORIG" == structName)
		{
			return 0;
		}

		std::smatch match;
		if (std::regex_match(structName, match, std::regex("PASS([1-9]|1[0-9]+)")))
		{
			const int index = std::stoi(match.str(1));
			if (index > passIndex)
			{
				LOG_DEBUG << "PASS index out of range: " << structName;
				throw ShaderStatus::SetupError;
			}
			return index;
		}

		if (std::regex_match(structName, match, std::regex("PASSPREV([1-9]|1[0-9]+)?")))
		{
			const int index = match.str(1).empty() ? 1 : std::stoi(match.str(1));
			const int prevPassIndex = passIndex - index + 1;
			if (prevPassIndex < 0)
			{
				LOG_DEBUG << "PASSPREV index out of range: " << structName;
				throw ShaderStatus::SetupError;
			}
			return prevPassIndex;
		}

		if (std::regex_match(structName, match, std::regex("PREV([1-9]|1[0-9]+)?")))
		{
			const unsigned index = match.str(1).empty() ? 0 : std::stoi(match.str(1));
			if (index >= m_prevInputFrames.max_size())
			{
				LOG_DEBUG << "PREV index out of range: " << structName;
				throw ShaderStatus::SetupError;
			}
			return -1 - index;
		}

		for (int i = 0; i < m_passCount; ++i)
		{
			if (m_passes[i].alias == structName)
			{
				if (i + 1 > passIndex)
				{
					LOG_DEBUG << "Alias refers to future pass: " << structName;
					throw ShaderStatus::SetupError;
				}
				return i + 1;
			}
		}

		return MAXINT;
	}

	SurfaceRepository::Surface& MetaShader::getPassRtt(int passIndex)
	{
		if (passIndex >= -1 || 0 == m_prevInputFrameCount)
		{
			return getPass(passIndex).rtt;
		}
		return getPrevInputFrame(-2 - passIndex).rtt;
	}

	MetaShader::Pass& MetaShader::getPass(int passIndex)
	{
		return passIndex < 0 ? m_srcPass : (passIndex >= m_passCount ? m_dstPass : m_passes[passIndex]);
	}

	MetaShader::InputFrame& MetaShader::getPrevInputFrame(int prevIndex)
	{
		prevIndex = std::min(prevIndex, m_prevInputFrameCount - 1);
		return m_prevInputFrames[(m_prevInputFrameIndex + prevIndex) % m_maxPrevInputFrames];
	}

	void MetaShader::getSurface(SurfaceRepository::Surface& surface, D3DDDIFORMAT format, SIZE size, DWORD caps)
	{
		if (caps & DDSCAPS_MIPMAP)
		{
			size = { getNextPow2(size.cx), getNextPow2(size.cy) };
		}

		const auto& d3dCaps = m_device.getAdapter().getInfo().d3dExtendedCaps;
		if (size.cx > static_cast<LONG>(d3dCaps.dwMaxTextureWidth) ||
			size.cy > static_cast<LONG>(d3dCaps.dwMaxTextureHeight))
		{
			throw ShaderStatus::ResolutionError;
		}

		m_device.getRepo().getSurface(surface, size.cx, size.cy, format, caps, (caps & DDSCAPS_MIPMAP) ? 0 : 1);
		if (!surface.resource)
		{
			throw ShaderStatus::SurfaceError;
		}
	}

	MetaShader::Vertex& MetaShader::getVertex(int index)
	{
		return reinterpret_cast<Vertex&>(m_vertices[index * m_vertexSize]);
	}

	void MetaShader::loadBitmaps()
	{
		LOG_FUNC("MetaShader::loadBitmaps");
		ShaderStatus status = ShaderStatus::Compiled;
		for (auto& bitmap : s_bitmaps)
		{
			if (ShaderStatus::Compiling == bitmap.second.status)
			{
				bitmap.second.bitmap = { Gdi::GuiThread::wicLoadImage(bitmap.first), &DeleteObject };
				if (!bitmap.second.bitmap)
				{
					status = ShaderStatus::CompileError;
					break;
				}
			}
			else if (ShaderStatus::CompileError == bitmap.second.status)
			{
				status = ShaderStatus::CompileError;
				break;
			}
		}

		for (auto& bitmap : s_bitmaps)
		{
			bitmap.second.status = status;
			if (ShaderStatus::CompileError == status)
			{
				bitmap.second.bitmap.reset();
			}
		}

		for (auto& device : D3dDdi::Device::getDevices())
		{
			device.second.getShaderBlitter().getMetaShader().compile();
		}
	}

	void MetaShader::loadCgp(const std::filesystem::path& relPath)
	{
		if (relPath == m_relPath)
		{
			return;
		}

		reset();
		m_relPath = relPath;

		for (const auto& baseDir : getBaseDirs())
		{
			if (parseCgp(baseDir))
			{
				break;
			}
		}

		if (0 == m_passCount)
		{
			LOG_DEBUG << "Number of shaders was not defined";
			setStatus(ShaderStatus::ParseError);
			return;
		}

		for (int i = 0; i < m_passCount; ++i)
		{
			validatePass(i);
		}
	}

	void MetaShader::loadTexture(Texture& texture)
	{
		if (texture.surface.surface && SUCCEEDED(texture.surface.surface->IsLost(texture.surface.surface)))
		{
			return;
		}

		BITMAP bm = {};
		GetObject(texture.bitmap->second.bitmap.get(), sizeof(bm), &bm);
		getSurface(texture.surface, D3DDDIFMT_A8R8G8B8, { bm.bmWidth, bm.bmHeight },
			DDSCAPS_TEXTURE | DDSCAPS_3DDEVICE | DDSCAPS_VIDEOMEMORY | (texture.mipmap ? DDSCAPS_MIPMAP : 0));

		const bool needsScaling = bm.bmWidth != static_cast<LONG>(texture.surface.width) ||
			bm.bmHeight != static_cast<LONG>(texture.surface.height);
		const SurfaceRepository::Surface& dst = needsScaling
			? m_device.getRepo().getTempTexture(bm.bmWidth, bm.bmHeight, D3DDDIFMT_A8R8G8B8)
			: texture.surface;
		if (!dst.surface)
		{
			throw ShaderStatus::TextureError;
		}

		HDC dc = nullptr;
		if (FAILED(dst.surface->GetDC(dst.surface, &dc)))
		{
			throw ShaderStatus::TextureError;
		}

		{
			std::shared_ptr<HDC__> dstDc(dc, [&](HDC) { dst.surface->ReleaseDC(dst.surface, dc); });
			std::shared_ptr<HDC__> srcDc(CreateCompatibleDC(nullptr), DeleteDC);
			if (!srcDc)
			{
				throw ShaderStatus::TextureError;
			}

			SelectObject(srcDc.get(), texture.bitmap->second.bitmap.get());
			if (!CALL_ORIG_FUNC(BitBlt)(dstDc.get(), 0, 0, bm.bmWidth, bm.bmHeight, srcDc.get(), 0, 0, SRCCOPY))
			{
				throw ShaderStatus::TextureError;
			}
		}

		if (texture.mipmap)
		{
			if (needsScaling)
			{
				m_device.getShaderBlitter().textureBlt(*texture.surface.resource, 0, texture.surface.resource->getRect(0),
					*dst.resource, 0, dst.resource->getRect(0), D3DTEXF_LINEAR | D3DTEXF_SRGB);
			}
			generateMipSubLevels(*texture.surface.resource);
		}
	}

	bool MetaShader::parseCgp(const std::filesystem::path& baseDir)
	{
		LOG_FUNC("MetaShader::parseCgp", baseDir.u8string());
		const auto absPath((baseDir / m_relPath).lexically_normal());
		std::ifstream f(absPath);
		if (f.fail())
		{
			return LOG_RESULT(false);
		}

		m_absPath = absPath;

		std::stringstream ss;
		ss << f.rdbuf();
		std::string content(ss.str());
		content = std::regex_replace(content, std::regex("(#|//).*"), "");

		std::match_results<std::string_view::const_iterator> match;
		std::string_view sv(content);
		while (std::regex_search(sv.begin(), sv.end(), match, std::regex("/\\*")))
		{
			sv = { sv.data() + match.position(), sv.size() - match.position() };
			if (!std::regex_search(sv.begin() + 2, sv.end(), match, std::regex("\\*/")))
			{
				break;
			}
			const auto pos = sv.data() - content.data();
			content.erase(pos, match.position() + 4);
			sv = { content.data() + pos, content.size() - pos };
		}
		ss.str(content);

		std::map<std::string, std::string> unknownKeyValues;
		std::string line;
		while (std::getline(ss, line))
		{
			if (std::string::npos == line.find_first_not_of(" \t"))
			{
				continue;
			}

			const auto pos = line.find_first_of('=');
			if (pos == std::string::npos)
			{
				LOG_DEBUG << "Invalid line format: " << line;
				throw ShaderStatus::ParseError;
			}

			std::string key(Config::Parser::trim(line.substr(0, pos)));
			std::string value(Config::Parser::trim(line.substr(pos + 1)));
			if (key.empty() || value.empty())
			{
				LOG_DEBUG << "Invalid line format: " << line;
				throw ShaderStatus::ParseError;
			}

			if ('"' == value[0])
			{
				if (value.length() < 2 || '"' != value.back())
				{
					LOG_DEBUG << "Unterminated string value: " << line;
					throw ShaderStatus::ParseError;
				}
				value = value.substr(1, value.length() - 2);
			}

			if (!setKey(key, value))
			{
				unknownKeyValues[key] = value;
			}
		}

		for (const auto& kv : unknownKeyValues)
		{
			if (!setKey(kv.first, kv.second))
			{
				LOG_DEBUG << "Unknown parameter: " << kv.first;
				throw ShaderStatus::ParseError;
			}
		}

		return LOG_RESULT(true);
	}

	bool MetaShader::parseRegisterComment(const std::string& comment, CompiledShader& compiledShader)
	{
		std::istringstream iss(comment);
		std::string name;
		char reg = 0;
		unsigned index = 0;
		unsigned size = 0;
		if (!(iss >> name) || !(iss >> reg) || !(iss >> index) || !(iss >> size) ||
			('c' != reg && 's' != reg) || 0 == size || size > 4)
		{
			LOG_DEBUG << "Failed to parse register comment: " << comment;
			return false;
		}

		if ('$' == name[0])
		{
			name = name.substr(1);
		}

		if ('c' == reg)
		{
			compiledShader.uniforms[name] = { index, size };
			compiledShader.minRegisterIndex = std::min<int>(compiledShader.minRegisterIndex, index);
			compiledShader.maxRegisterIndex = std::max<int>(compiledShader.maxRegisterIndex, index + size - 1);
		}
		else
		{
			compiledShader.samplers[name] = { index, size };
		}
		return true;
	}

	MetaShader::ScaleType MetaShader::parseScaleType(const std::string& value)
	{
		if ("source" == value)
		{
			return ScaleType::Source;
		}
		if ("viewport" == value)
		{
			return ScaleType::Viewport;
		}
		if ("absolute" == value)
		{
			return ScaleType::Absolute;
		}
		throw ShaderStatus::ParseError;
	}

	void MetaShader::render(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
		const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect)
	{
		LOG_FUNC("MetaShader::render", static_cast<HANDLE>(dstResource), dstSubResourceIndex, dstRect,
			static_cast<HANDLE>(srcResource), srcSubResourceIndex, srcRect);

		try
		{
			loadCgp(Config::displayFilter.getCgpPath());
			compile();
			if (ShaderStatus::Compiled == m_status)
			{
				setExternalPass(m_srcPass, srcResource, srcSubResourceIndex, srcRect);
				setExternalPass(m_dstPass, dstResource, dstSubResourceIndex, dstRect);

				if (m_passes[0].mipmap_input)
				{
					getSurface(m_srcPass.rtt, m_srcPass.rtt.format, m_srcPass.outputSize,
						DDSCAPS_TEXTURE | DDSCAPS_MIPMAP | DDSCAPS_3DDEVICE | DDSCAPS_VIDEOMEMORY);
					if (FAILED(m_srcPass.rtt.resource->copySubResourceRegion(
						0, { 0, 0, m_srcPass.outputSize.cx, m_srcPass.outputSize.cy },
						srcResource, srcSubResourceIndex, srcRect)))
					{
						throw ShaderStatus::SurfaceError;
					}
				}

				setupPrevInputFrames();

				for (int i = 0; i < m_passCount; ++i)
				{
					renderPass(dstRect, i);
				}
				m_prevInputSize = m_srcPass.outputSize;

				const auto& lastPass = m_passes[m_passCount - 1];
				dstResource.getDevice().getShaderBlitter().bilinearBlt(dstResource, dstSubResourceIndex, dstRect,
					*lastPass.rtt.resource, 0, { 0, 0, lastPass.outputSize.cx, lastPass.outputSize.cy }, 100);

				if (m_prevInputFrameCandidate.rtt.surface)
				{
					std::swap(m_prevInputFrameCandidate.rtt,
						m_passes[0].mipmap_input ? m_srcPass.rtt : m_device.getRepo().getPresentationSourceRtt());
				}
				return;
			}
		}
		catch (ShaderStatus status)
		{
			setStatus(status);
		}

		dstResource.getDevice().getShaderBlitter().bilinearBlt(dstResource, dstSubResourceIndex, dstRect,
			srcResource, srcSubResourceIndex, srcRect, 0);
	}

	void MetaShader::renderPass(const RECT& dstRect, int passIndex)
	{
		auto& pass = m_passes[passIndex];
		const auto& prevPass = getPass(passIndex - 1);

		if (m_srcPass.outputSize != m_prevInputSize)
		{
			pass.outputSize.cx = getOutputSize(prevPass.outputSize.cx, m_dstPass.outputSize.cx,
				pass.scale_type_x, pass.scale_x);
			pass.outputSize.cy = getOutputSize(prevPass.outputSize.cy, m_dstPass.outputSize.cy,
				pass.scale_type_y, pass.scale_y);

			setupUniforms(pass.shader->second.vs, pass.vsConsts, passIndex);
			setupUniforms(pass.shader->second.ps, pass.psConsts, passIndex);
			setupVertices(passIndex);
		}

		if (pass.mipmap_input)
		{
			generateMipSubLevels(*prevPass.rtt.resource);
		}

		getSurface(pass.rtt, getFormat(pass.float_framebuffer), pass.outputSize,
			DDSCAPS_TEXTURE | DDSCAPS_3DDEVICE | DDSCAPS_VIDEOMEMORY |
			(getPass(passIndex + 1).mipmap_input ? DDSCAPS_MIPMAP : 0));

		auto& state = m_device.getState();
		state.setTempRenderTarget({ 0, *pass.rtt.resource, 0 });
		state.setTempDepthStencil({ nullptr });

		D3DDDIARG_VIEWPORTINFO vp = {};
		if (pass.rtt.resource == m_dstPass.rtt.resource)
		{
			vp.X = dstRect.left;
			vp.Y = dstRect.top;
			vp.Width = dstRect.right - dstRect.left;
			vp.Height = dstRect.bottom - dstRect.top;
		}
		else
		{
			vp.Width = pass.outputSize.cx;
			vp.Height = pass.outputSize.cy;
		}
		state.setTempViewport(vp);

		state.setTempZRange({ 0, 1 });
		state.setTempVertexShaderDecl(m_vertexShaderDecl.get());
		state.setTempVertexShaderFunc(pass.vs.get());
		state.setTempPixelShader(pass.ps.get());

		state.setTempRenderState({ D3DDDIRS_SCENECAPTURE, TRUE });
		state.setTempRenderState({ D3DDDIRS_ZENABLE, D3DZB_FALSE });
		state.setTempRenderState({ D3DDDIRS_FILLMODE, D3DFILL_SOLID });
		state.setTempRenderState({ D3DDDIRS_ZWRITEENABLE, FALSE });
		state.setTempRenderState({ D3DDDIRS_ALPHATESTENABLE, FALSE });
		state.setTempRenderState({ D3DDDIRS_CULLMODE, D3DCULL_NONE });
		state.setTempRenderState({ D3DDDIRS_DITHERENABLE, FALSE });
		state.setTempRenderState({ D3DDDIRS_ALPHABLENDENABLE, FALSE });
		state.setTempRenderState({ D3DDDIRS_FOGENABLE, FALSE });
		state.setTempRenderState({ D3DDDIRS_COLORKEYENABLE, FALSE });
		state.setTempRenderState({ D3DDDIRS_STENCILENABLE, FALSE });
		state.setTempRenderState({ D3DDDIRS_CLIPPING, FALSE });
		state.setTempRenderState({ D3DDDIRS_CLIPPLANEENABLE, 0 });
		state.setTempRenderState({ D3DDDIRS_MULTISAMPLEANTIALIAS, FALSE });
		state.setTempRenderState({ D3DDDIRS_COLORWRITEENABLE, 0xF });
		state.setTempRenderState({ D3DDDIRS_SCISSORTESTENABLE, FALSE });
		state.setTempRenderState({ D3DDDIRS_SRGBWRITEENABLE, pass.srgb_framebuffer });

		setTexture(0, *prevPass.rtt.resource, pass.wrap_mode, pass.filter_linear, prevPass.srgb_framebuffer);
		updateSamplers(pass.vsSamplers);
		updateSamplers(pass.psSamplers);

		state.setTempStreamSourceUm({ 0, m_vertexSize }, m_vertices.data() + 4 * passIndex * m_vertexSize);

		DeviceState::TempVertexShaderConst vsConsts(
			state, { pass.vsConsts.firstConst, pass.vsConsts.consts.size() }, pass.vsConsts.consts.data());
		DeviceState::TempPixelShaderConst psConsts(
			state, { pass.psConsts.firstConst, pass.psConsts.consts.size() }, pass.psConsts.consts.data());

		D3DDDIARG_DRAWPRIMITIVE dp = {};
		dp.PrimitiveType = D3DPT_TRIANGLESTRIP;
		dp.PrimitiveCount = 2;
		if (FAILED(m_device.getOrigVtable().pfnDrawPrimitive(m_device, &dp, nullptr)))
		{
			throw ShaderStatus::RenderError;
		}
	}

	void MetaShader::reset()
	{
		m_vertexShaderDecl.reset();
		m_vertices.clear();
		m_absPath.clear();
		m_relPath.clear();
		m_prevInputFrameCandidate = {};
		m_prevInputFrames = {};
		m_srcPass = {};
		m_passes = {};
		m_dstPass = {};
		m_parameters.clear();
		m_textures.clear();
		m_maxPrevInputFrames = 0;
		m_prevInputFrameCount = 0;
		m_prevInputFrameIndex = 0;
		m_passCount = 0;
		m_vertexSize = sizeof(Vertex::position);
		if (0 != m_frameTimer)
		{
			timeKillEvent(m_frameTimer);
			m_frameTimer = 0;
		}
		m_frameCount = 0;
		m_prevInputSize = {};
		m_status = ShaderStatus::Compiling;
	}

	void MetaShader::setExternalPass(Pass& pass, const Resource& resource, UINT subResourceIndex, const RECT& rect)
	{
		const auto resourceSize = Rect::getSize(resource.getRect(subResourceIndex));
		pass.rtt.resource = const_cast<Resource*>(&resource);
		pass.rtt.width = resourceSize.cx;
		pass.rtt.height = resourceSize.cy;
		pass.rtt.format = resource.getFixedDesc().Format;
		pass.outputSize = Rect::getSize(rect);
	}

	bool MetaShader::setKey(const std::string& key, const std::string& value)
	{
		if ("shaders" == key)
		{
			m_passCount = Config::Parser::parseInt(value, 1, m_passes.max_size());
			return true;
		}

		if ("parameters" == key)
		{
			m_parameters.clear();
			for (const auto& param : parseList(value))
			{
				m_parameters[param] = NAN;
			}
			return true;
		}

		if ("textures" == key)
		{
			m_textures.clear();
			for (const auto& texture : parseList(value))
			{
				m_textures[texture] = {};
			}
			return true;
		}

		const auto parameter = m_parameters.find(key);
		if (parameter != m_parameters.end())
		{
			parameter->second = parseFloat(value);
			return true;
		}

		for (auto& texture : m_textures)
		{
			if (texture.first == key)
			{
				const auto path((m_absPath.remove_filename() / value).lexically_normal());
				texture.second.bitmap = s_bitmaps.emplace(path, Bitmap{}).first;
				return true;
			}
			if (texture.first + "_mipmap" == key)
			{
				texture.second.mipmap = parseBool(value);
				return true;
			}
			if (texture.first + "_wrap_mode" == key)
			{
				texture.second.wrap_mode = parseWrapMode(value);
				return true;
			}
		}

		const char LINEAR_SUFFIX[] = "_linear";
		if (key.length() >= sizeof(LINEAR_SUFFIX) &&
			LINEAR_SUFFIX == key.substr(key.length() - sizeof(LINEAR_SUFFIX) + 1))
		{
			const auto it = m_textures.find(key.substr(0, key.length() - sizeof(LINEAR_SUFFIX) + 1));
			if (it != m_textures.end())
			{
				it->second.linear = parseBool(value);
				return true;
			}
		}

		const auto pos = key.find_last_not_of("0123456789");
		if (pos >= key.length() - 1)
		{
			return false;
		}

		const int index = Config::Parser::parseInt(key.substr(pos + 1), 0, 15);
		const auto keyBase(key.substr(0, pos + 1));
		auto& pass = m_passes[index];

		if ("alias" == keyBase)
		{
			pass.alias = value;
		}
		else if ("filter_linear" == keyBase)
		{
			pass.filter_linear = parseBool(value);
		}
		else if ("float_framebuffer" == keyBase)
		{
			pass.float_framebuffer = parseBool(value);
		}
		else if ("frame_count_mod" == keyBase)
		{
			pass.frame_count_mod = Config::Parser::parseInt(value, 1, INT_MAX);
		}
		else if ("mipmap_input" == keyBase)
		{
			pass.mipmap_input = parseBool(value);
		}
		else if ("scale_type" == keyBase)
		{
			pass.scale_type_x = parseScaleType(value);
			pass.scale_type_y = pass.scale_type_x;
		}
		else if ("scale_type_x" == keyBase)
		{
			pass.scale_type_x = parseScaleType(value);
		}
		else if ("scale_type_y" == keyBase)
		{
			pass.scale_type_y = parseScaleType(value);
		}
		else if ("scale" == keyBase)
		{
			pass.scale = parseFloat(value);
		}
		else if ("scale_x" == keyBase)
		{
			pass.scale_x = parseFloat(value);
		}
		else if ("scale_y" == keyBase)
		{
			pass.scale_y = parseFloat(value);
		}
		else if ("shader" == keyBase)
		{
			const auto path((m_absPath.remove_filename() / value).lexically_normal());
			pass.shader = s_shaders.emplace(path, Shader{}).first;
		}
		else if ("srgb_framebuffer" == keyBase)
		{
			pass.srgb_framebuffer = parseBool(value);
		}
		else if ("texture_wrap_mode" == keyBase || "wrap_mode" == keyBase)
		{
			pass.wrap_mode = parseWrapMode(value);
		}
		else
		{
			return false;
		}

		return true;
	}

	void MetaShader::setStatus(ShaderStatus status)
	{
		if (status == m_status)
		{
			return;
		}

		if (static_cast<int>(status) > static_cast<int>(ShaderStatus::Compiled))
		{
			const auto relPath = m_relPath;
			reset();
			m_relPath = relPath;
		}

		m_status = status;

		Gdi::GuiThread::executeAsyncFunc([]()
			{
				auto configWindow = Gdi::GuiThread::getConfigWindow();
				if (configWindow)
				{
					configWindow->invalidate();
				}
				DDraw::RealPrimarySurface::scheduleOverlayUpdate();
			});
	}

	void MetaShader::setTexture(UINT stage, const Resource& resource, D3DTEXTUREADDRESS wrap, bool linear, bool srgb)
	{
		auto& state = m_device.getState();
		state.setTempTexture(stage, resource);
		state.setTempTextureStageState({ stage, D3DDDITSS_TEXCOORDINDEX, stage });
		state.setTempTextureStageState({ stage, D3DDDITSS_ADDRESSU, static_cast<UINT>(wrap) });
		state.setTempTextureStageState({ stage, D3DDDITSS_ADDRESSV, static_cast<UINT>(wrap) });
		state.setTempTextureStageState({ stage, D3DDDITSS_BORDERCOLOR, 0 });
		state.setTempTextureStageState({ stage, D3DDDITSS_MAGFILTER, linear ? D3DTEXF_LINEAR : D3DTEXF_POINT });
		state.setTempTextureStageState({ stage, D3DDDITSS_MINFILTER, linear ? D3DTEXF_LINEAR : D3DTEXF_POINT });
		state.setTempTextureStageState({ stage, D3DDDITSS_MIPFILTER, linear ? D3DTEXF_LINEAR : D3DTEXF_POINT });
		state.setTempTextureStageState({ stage, D3DDDITSS_SRGBTEXTURE, srgb });
		state.setTempRenderState({ static_cast<D3DDDIRENDERSTATETYPE>(D3DDDIRS_WRAP0 + stage), 0 });
	}

	void MetaShader::setTextureCoords(UINT stage, const RECT& rect, UINT width, UINT height, int passIndex)
	{
		const float w = static_cast<float>(width);
		const float h = static_cast<float>(height);
		getVertex(4 * passIndex + 0).texCoord[stage] = { rect.left / w, rect.top / h };
		getVertex(4 * passIndex + 1).texCoord[stage] = { rect.right / w, rect.top / h };
		getVertex(4 * passIndex + 2).texCoord[stage] = { rect.left / w, rect.bottom / h };
		getVertex(4 * passIndex + 3).texCoord[stage] = { rect.right / w, rect.bottom / h };
	}

	void MetaShader::setupPrevInputFrames()
	{
		if (0 == m_maxPrevInputFrames)
		{
			return;
		}

		if (!m_prevInputFrameCandidate.rtt.surface ||
			m_prevInputFrameCandidate.rtt.width != m_srcPass.rtt.width ||
			m_prevInputFrameCandidate.rtt.height != m_srcPass.rtt.height ||
			m_prevInputFrameCandidate.rtt.format != m_srcPass.rtt.format ||
			FAILED(m_prevInputFrameCandidate.rtt.surface->IsLost(m_prevInputFrameCandidate.rtt.surface)))
		{
			m_prevInputFrameCandidate = {};
			m_prevInputFrames = {};
			m_prevInputFrameCount = 0;
		}

		const auto qpcNow = Time::queryPerformanceCounter();
		if (m_prevInputFrameCandidate.rtt.surface)
		{
			auto qpcIdeal = m_prevInputFrameCandidate.qpc;
			if (0 != m_prevInputFrameCount)
			{
				const auto& qpcOldestPrevFrame = getPrevInputFrame(m_prevInputFrameCount - 1).qpc;
				qpcIdeal = qpcOldestPrevFrame + Time::msToQpc(m_prevInputFrameCount * FRAME_INTERVAL);
			}

			if (std::abs(m_prevInputFrameCandidate.qpc - qpcIdeal) <= std::abs(qpcNow - qpcIdeal))
			{
				if (m_prevInputFrameCount < m_maxPrevInputFrames)
				{
					++m_prevInputFrameCount;
				}
				--m_prevInputFrameIndex;
				if (m_prevInputFrameIndex < 0)
				{
					m_prevInputFrameIndex = m_maxPrevInputFrames - 1;
				}

				std::swap(m_prevInputFrameCandidate, getPrevInputFrame(0));
				getSurface(m_prevInputFrameCandidate.rtt, m_srcPass.rtt.format, m_srcPass.outputSize,
					DDSCAPS_TEXTURE | DDSCAPS_3DDEVICE | DDSCAPS_VIDEOMEMORY |
					(m_passes[0].mipmap_input ? DDSCAPS_MIPMAP : 0));
			}
		}
		else
		{
			getSurface(m_prevInputFrameCandidate.rtt, m_srcPass.rtt.format, m_srcPass.outputSize,
				DDSCAPS_TEXTURE | DDSCAPS_3DDEVICE | DDSCAPS_VIDEOMEMORY |
				(m_passes[0].mipmap_input ? DDSCAPS_MIPMAP : 0));
		}
		m_prevInputFrameCandidate.qpc = qpcNow;
	}

	bool MetaShader::setupSamplerForPassInput(const std::string& field, unsigned regIndex,
		std::vector<Sampler>& samplers, int passIndex)
	{
		if ("_texture" == field)
		{
			Sampler sampler = {};
			sampler.regIndex = regIndex;
			sampler.passIndex = passIndex;
			samplers.push_back(sampler);
			return true;
		}
		return false;
	}

	bool MetaShader::setupSampler(const std::string& name, const RegisterRange& reg,
		std::vector<Sampler>& samplers, int passIndex)
	{
		if (1 != reg.count)
		{
			return false;
		}

		const auto texture = m_textures.find(name);
		if (texture != m_textures.end())
		{
			Sampler sampler = {};
			sampler.regIndex = reg.index;
			sampler.texture = &texture->second;
			samplers.push_back(sampler);
			return true;
		}

		const auto pos = name.find("__");
		if (std::string::npos == pos)
		{
			return 0 == reg.index;
		}

		const auto structName(name.substr(0, pos));
		const auto fieldName(name.substr(pos + 2));
		if ("IN" == structName)
		{
			return setupSamplerForPassInput(fieldName, reg.index, samplers, passIndex);
		}

		const auto prevPassIndex = getPassIndexFromStructName(structName, passIndex);
		if (MAXINT != prevPassIndex)
		{
			if (prevPassIndex < -1)
			{
				m_maxPrevInputFrames = std::max(m_maxPrevInputFrames, -prevPassIndex);
			}
			return setupSamplerForPassInput(fieldName, reg.index, samplers, prevPassIndex);
		}
		return 0 == reg.index;
	}

	void MetaShader::setupSamplers(const CompiledShader& compiledShader, std::vector<Sampler>& samplers, int passIndex)
	{
		for (const auto& sampler : compiledShader.samplers)
		{
			if (!setupSampler(sampler.first, sampler.second, samplers, passIndex))
			{
				LOG_DEBUG << "Unsupported sampler: " << sampler.first << " (register count: " << sampler.second.count << ')';
				throw ShaderStatus::SetupError;
			}
		}
	}

	bool MetaShader::setupUniformForPassInput(const std::string& field, unsigned regIndex,
		ShaderConsts& consts, int passIndex)
	{
		auto& prevPass = getPass(passIndex - 1);
		auto dst = &consts.consts[regIndex][0];

		if ("texture_size" == field)
		{
			dst[0] = static_cast<float>(prevPass.rtt.width);
			dst[1] = static_cast<float>(prevPass.rtt.height);
			return true;
		}

		if ("video_size" == field)
		{
			dst[0] = static_cast<float>(prevPass.outputSize.cx);
			dst[1] = static_cast<float>(prevPass.outputSize.cy);
			return true;
		}

		return false;
	}

	bool MetaShader::setupUniform(const std::string& name, const RegisterRange& reg, ShaderConsts& consts, int passIndex)
	{
		if (4 == reg.count)
		{
			if ("modelViewProj" == name)
			{
				auto dstMatrix = &consts.consts[reg.index];
				dstMatrix[0] = { 1, 0, 0, -1.0f / m_passes[passIndex].outputSize.cx };
				dstMatrix[1] = { 0, 1, 0, 1.0f / m_passes[passIndex].outputSize.cy };
				dstMatrix[2] = { 0, 0, 1, 0 };
				dstMatrix[3] = { 0, 0, 0, 1 };
				return true;
			}
			return false;
		}

		if (1 != reg.count)
		{
			return false;
		}

		auto dst = &consts.consts[reg.index][0];
		for (int i = 0; i < m_passCount; ++i)
		{
			const auto& parameters = m_passes[i].shader->second.parameters;
			const auto it = std::find_if(parameters.begin(), parameters.end(),
				[&](const ShaderCompiler::Parameter& parameter) { return name == parameter.name; });
			if (it != parameters.end())
			{
				auto value = it->default;
				const auto parameterOverride = m_parameters.find(name);
				if (parameterOverride != m_parameters.end())
				{
					value = std::max(parameterOverride->second, it->min);
					value = std::min(parameterOverride->second, it->max);
				}
				*dst = value;
				return true;
			}
		}

		const auto pos = name.find("__");
		if (std::string::npos == pos)
		{
			return false;
		}

		const auto structName(name.substr(0, pos));
		const auto fieldName(name.substr(pos + 2));

		if ("IN" == structName)
		{
			if ("frame_count" == fieldName)
			{
				consts.frameCountReg = dst;
				if (0 == m_frameTimer)
				{
					m_frameTimer = timeSetEvent(FRAME_INTERVAL, 1, &frameTimerCallback,
						reinterpret_cast<DWORD_PTR>(this), TIME_PERIODIC);
				}
				return true;
			}

			if ("frame_direction" == fieldName)
			{
				*dst = 1;
				return true;
			}

			if ("output_size" == fieldName)
			{
				dst[0] = static_cast<float>(m_passes[passIndex].outputSize.cx);
				dst[1] = static_cast<float>(m_passes[passIndex].outputSize.cy);
				return true;
			}

			if ("texture_size" == fieldName || "video_size" == fieldName)
			{
				return setupUniformForPassInput(fieldName, reg.index, consts, passIndex);
			}

			return false;
		}

		const auto prevPassIndex = getPassIndexFromStructName(structName, passIndex);
		if (MAXINT != prevPassIndex)
		{
			return setupUniformForPassInput(fieldName, reg.index, consts, prevPassIndex);
		}

		return false;
	}

	void MetaShader::setupUniforms(const CompiledShader& compiledShader, ShaderConsts& consts, int passIndex)
	{
		if (compiledShader.uniforms.empty())
		{
			return;
		}

		consts.firstConst = compiledShader.minRegisterIndex;
		consts.consts.clear();
		consts.consts.resize(compiledShader.maxRegisterIndex - compiledShader.minRegisterIndex + 1, {});

		for (const auto& uniform : compiledShader.uniforms)
		{
			if (!setupUniform(uniform.first, uniform.second, consts, passIndex))
			{
				LOG_DEBUG << "Unsupported uniform: " << uniform.first << " (register count: " << uniform.second.count << ')';
				throw ShaderStatus::SetupError;
			}
		}
	}

	void MetaShader::setupVertexShaderDecl()
	{
		unsigned maxTexCoordIndex = 0;
		for (int i = 0; i < m_passCount; ++i)
		{
			auto& pass = m_passes[i];
			for (const auto& texCoord : pass.shader->second.texCoords)
			{
				maxTexCoordIndex = std::max(maxTexCoordIndex, texCoord.second);
			}
		}

		m_vertexSize = sizeof(Vertex::position) + (maxTexCoordIndex + 1) * sizeof(Vertex::texCoord[0]);
		m_vertices.resize(m_passCount * 4 * m_vertexSize);

		const UINT D3DDECLTYPE_FLOAT2 = 1;
		const UINT D3DDECLTYPE_FLOAT4 = 3;
		const UINT D3DDECLMETHOD_DEFAULT = 0;
		const UINT D3DDECLUSAGE_TEXCOORD = 5;
		const UINT D3DDECLUSAGE_POSITION = 0;

		std::vector<D3DDDIVERTEXELEMENT> vertexElements;
		vertexElements.push_back({ 0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 });
		USHORT offset = sizeof(Vertex::position);
		for (unsigned index = 0; index <= maxTexCoordIndex; ++index)
		{
			vertexElements.push_back({0, offset, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,
				static_cast<UCHAR>(index) });
			offset += sizeof(Vertex::texCoord[0]);
		}

		D3DDDIARG_CREATEVERTEXSHADERDECL data = {};
		data.NumVertexElements = vertexElements.size();
		if (FAILED(m_device.getOrigVtable().pfnCreateVertexShaderDecl(m_device, &data, vertexElements.data())))
		{
			throw ShaderStatus::SetupError;
		}
		m_vertexShaderDecl = { data.ShaderHandle,
			ResourceDeleter(m_device, m_device.getOrigVtable().pfnDeleteVertexShaderDecl) };
	}

	void MetaShader::setupVertices(int passIndex)
	{
		getVertex(4 * passIndex + 0).position = { -1, 1, 0, 1 };
		getVertex(4 * passIndex + 1).position = { 1, 1, 0, 1 };
		getVertex(4 * passIndex + 2).position = { -1, -1, 0, 1 };
		getVertex(4 * passIndex + 3).position = { 1, -1, 0, 1 };

		for (const auto& texCoord : m_passes[passIndex].shader->second.texCoords)
		{
			int prevPassIndex = MAXINT;
			const auto pos = texCoord.first.find("__");
			if (std::string::npos != pos)
			{
				const auto fieldName(texCoord.first.substr(pos + 2));
				if ("tex_coord" != fieldName)
				{
					continue;
				}
				const auto structName(texCoord.first.substr(0, pos));
				prevPassIndex = getPassIndexFromStructName(structName, passIndex);
			}

			if (MAXINT != prevPassIndex)
			{
				auto& prevPass = getPass(prevPassIndex - 1);
				setTextureCoords(texCoord.second, RECT{ 0, 0, prevPass.outputSize.cx, prevPass.outputSize.cy },
					prevPass.rtt.width, prevPass.rtt.height, passIndex);
				continue;
			}

			if (0 == texCoord.second)
			{
				auto& prevPass = getPass(passIndex - 1);
				setTextureCoords(0, RECT{ 0, 0, prevPass.outputSize.cx, prevPass.outputSize.cy },
					prevPass.rtt.width, prevPass.rtt.height, passIndex);
			}
			else if (1 == texCoord.second)
			{
				setTextureCoords(1, RECT{ 0, 0, 1, 1 }, 1, 1, passIndex);
			}
			else
			{
				LOG_DEBUG << "Unsupported texture coordinate argument: " << texCoord.first
					<< " (TEXCOORD" << texCoord.second << ')';
			}
		}
	}

	void MetaShader::setup()
	{
		setupVertexShaderDecl();

		m_passes[m_passCount - 1].srgb_framebuffer = false;

		for (int i = 0; i < m_passCount; ++i)
		{
			auto& pass = m_passes[i];
			createShaders(pass);
			setupSamplers(pass.shader->second.vs, pass.vsSamplers, i);
			setupSamplers(pass.shader->second.ps, pass.psSamplers, i);
		}

		if (0 != m_maxPrevInputFrames && 0 == m_frameTimer)
		{
			m_frameTimer = timeSetEvent(FRAME_INTERVAL, 1, &frameTimerCallback,
				reinterpret_cast<DWORD_PTR>(this), TIME_PERIODIC);
		}
	}

	void MetaShader::updateSamplers(const std::vector<Sampler>& samplers)
	{
		for (const auto& sampler : samplers)
		{
			if (sampler.texture)
			{
				loadTexture(*sampler.texture);
				setTexture(sampler.regIndex, *sampler.texture->surface.resource,
					sampler.texture->wrap_mode, sampler.texture->linear, false);
			}
			else
			{
				auto& pass = getPass(sampler.passIndex);
				auto& prevPass = getPass(sampler.passIndex - 1);
				setTexture(sampler.regIndex, *getPassRtt(sampler.passIndex - 1).resource,
					pass.wrap_mode, pass.filter_linear, prevPass.srgb_framebuffer);
			}
		}
	}

	void MetaShader::validateScale(int index, ScaleType& scale_type_xy, float& scale_xy)
	{
		if (ScaleType::Undefined == scale_type_xy || std::isnan(scale_xy))
		{
			if (ScaleType::Viewport != scale_type_xy)
			{
				scale_type_xy = (index == m_passCount - 1) ? ScaleType::Viewport : ScaleType::Source;
			}
			scale_xy = 1.0f;
		}
		else if (ScaleType::Absolute == scale_type_xy)
		{
			scale_xy = std::roundf(scale_xy);
		}
	}

	void MetaShader::validatePass(int passIndex)
	{
		Pass& pass = m_passes[passIndex];
		if (pass.shader == s_shaders.end())
		{
			LOG_DEBUG << "shader" << passIndex << " is undefined";
			throw ShaderStatus::ParseError;
		}

		if (ScaleType::Undefined != pass.scale_type)
		{
			pass.scale_type_x = pass.scale_type;
			pass.scale_type_y = pass.scale_type;
		}
		else if (pass.scale_type_x != pass.scale_type_y &&
			pass.scale_type_x != ScaleType::Undefined &&
			pass.scale_type_y != ScaleType::Undefined)
		{
			pass.scale = NAN;
		}

		if (!std::isnan(pass.scale))
		{
			pass.scale_x = pass.scale;
			pass.scale_y = pass.scale;
		}
		else if (std::isnan(pass.scale_x) != std::isnan(pass.scale_y))
		{
			if (std::isnan(pass.scale_x))
			{
				pass.scale_type_x = ScaleType::Source;
				pass.scale_x = 1.0f;
			}
			else
			{
				pass.scale_type_y = ScaleType::Source;
				pass.scale_y = 1.0f;
			}
		}

		validateScale(passIndex, pass.scale_type_x, pass.scale_x);
		validateScale(passIndex, pass.scale_type_y, pass.scale_y);
	}

	std::vector<std::filesystem::path> MetaShader::s_baseDirs;
	std::map<std::filesystem::path, MetaShader::Shader> MetaShader::s_shaders;
	std::map<std::filesystem::path, MetaShader::Bitmap> MetaShader::s_bitmaps;
}
