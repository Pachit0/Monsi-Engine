#include "MonsiPch.h"
#include "SSAOPass.h"
#include "RenderCommand.h"
#include <glad/glad.h>
#include <algorithm>

namespace Monsi {

	void SSAOPass::Init()
	{
		m_SSAOShader = Shader::Create(SHADER_PATH "SSAO.glsl");
		m_SSAOBlurShader = Shader::Create(SHADER_PATH "SSAOBlur.glsl");

		float quadVertices[] = {
			-1.0f,  1.0f,  0.0f, 1.0f,
			-1.0f, -1.0f,  0.0f, 0.0f,
			 1.0f,  1.0f,  1.0f, 1.0f,
			 1.0f, -1.0f,  1.0f, 0.0f,
		};

		m_QuadVAO = VertexArray::Create();
		m_QuadVBO = VertexBuffer::Create(quadVertices, sizeof(quadVertices));
		m_QuadVBO->SetLayout({ { ShaderDataType::Float2, "a_Position" },{ ShaderDataType::Float2, "a_TexCoord" } });
		m_QuadVAO->AddVertexBuffer(m_QuadVBO);

		m_SampleUniformNames.resize(MaxKernelSize);
		for (uint32_t i = 0; i < MaxKernelSize; ++i) {
			m_SampleUniformNames[i] = "u_Samples[" + std::to_string(i) + "]";
		}

		m_SSAOShader->Bind();
		m_SSAOShader->setInt("gPosition", PositionTextureSlot);
		m_SSAOShader->setInt("gNormal", NormalTextureSlot);
		m_SSAOShader->setInt("texNoise", NoiseTextureSlot);

		m_SSAOBlurShader->Bind();
		m_SSAOBlurShader->setInt("u_SSAOInput", SSAOTextureSlot);
	}

	void SSAOPass::Shutdown()
	{
		m_QuadVAO.reset();
		m_QuadVBO.reset();
		m_SSAOShader.reset();
		m_SSAOBlurShader.reset();
		m_SampleUniformNames.clear();
	}

	void SSAOPass::DrawSSAO(const Reference<SSAO>& ssaoInstance, const Reference<FrameBuffer>& gBuffer, const glm::mat4& projection)
	{
		if (!ssaoInstance || !gBuffer) {
			return;
		}

		m_Stats = {};

		const SSAOSettings& settings = ssaoInstance->GetSettings();

		GLint previousFramebuffer = 0;
		GLint previousViewport[4] = { 0, 0, 0, 0 };
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
		glGetIntegerv(GL_VIEWPORT, previousViewport);
		const GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
		const GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
		const GLboolean cullWasEnabled = glIsEnabled(GL_CULL_FACE);

		glDisable(GL_DEPTH_TEST);
		glDisable(GL_BLEND);
		glDisable(GL_CULL_FACE);

		ssaoInstance->BindSSAOFramebuffer();
		RenderCommand::Clear();

		const int kernelSize = std::min<int>(settings.KernelSize, (int)MaxKernelSize);

		m_SSAOShader->Bind();
		m_SSAOShader->setMat4("u_Projection", projection);
		m_SSAOShader->setFloat("u_Radius", settings.Radius);
		m_SSAOShader->setFloat("u_Bias", settings.Bias);
		m_SSAOShader->setFloat("u_Intensity", settings.Intensity);
		m_SSAOShader->setInt("u_KernelSize", kernelSize);
		m_SSAOShader->setVec2("u_NoiseScale", glm::vec2(settings.Width / 4.0f, settings.Height / 4.0f));

		const auto& kernel = ssaoInstance->GetKernel();
		const size_t sampleCount = std::min<size_t>(kernel.size(), (size_t)kernelSize);
		for (size_t i = 0; i < sampleCount; ++i) {
			m_SSAOShader->setVec3(m_SampleUniformNames[i], kernel[i]);
		}

		glBindTextureUnit(PositionTextureSlot, gBuffer->GetColorTextureID());
		glBindTextureUnit(NormalTextureSlot, gBuffer->GetNormalTextureID());
		glBindTextureUnit(NoiseTextureSlot, ssaoInstance->GetNoiseTextureID());

		RenderScreenQuad();

		ssaoInstance->BindBlurFramebuffer();
		RenderCommand::Clear();

		m_SSAOBlurShader->Bind();
		glBindTextureUnit(SSAOTextureSlot, ssaoInstance->GetSSAOTextureID());

		RenderScreenQuad();

		if (depthWasEnabled) glEnable(GL_DEPTH_TEST);
		if (blendWasEnabled) glEnable(GL_BLEND);
		if (cullWasEnabled)  glEnable(GL_CULL_FACE);

		glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previousFramebuffer);
		glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
	}

	void SSAOPass::Resize(uint32_t width, uint32_t height, const Reference<SSAO>& ssaoInstance)
	{
		ENGINE_PROFILER_FUNCTION();
		if (ssaoInstance) {
			ssaoInstance->Resize(width, height);
		}
	}

	void SSAOPass::RenderScreenQuad()
	{
		m_QuadVAO->Bind();
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

		m_Stats.DrawCalls++;
		m_Stats.Triangles += 2;
	}

}