#include "MonsiPch.h"
#include "AnimatedModel.h"

#include <cmath>

#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

namespace Monsi {

	static glm::mat4 ConvertAssimpMatrix(const aiMatrix4x4& from)
	{
		glm::mat4 to;
		to[0][0] = from.a1; to[1][0] = from.a2; to[2][0] = from.a3; to[3][0] = from.a4;
		to[0][1] = from.b1; to[1][1] = from.b2; to[2][1] = from.b3; to[3][1] = from.b4;
		to[0][2] = from.c1; to[1][2] = from.c2; to[2][2] = from.c3; to[3][2] = from.c4;
		to[0][3] = from.d1; to[1][3] = from.d2; to[2][3] = from.d3; to[3][3] = from.d4;
		return to;
	}

	template<typename KeyType>
	static uint32_t FindKey(float animationTime, const KeyType* keys, uint32_t count)
	{
		uint32_t lo = 0;
		uint32_t hi = count - 1;
		while (hi - lo > 1)
		{
			uint32_t mid = (lo + hi) / 2;
			if (animationTime < static_cast<float>(keys[mid].mTime))
				hi = mid;
			else
				lo = mid;
		}
		return lo;
	}

	static float CalcFactor(float animationTime, double t0, double t1)
	{
		double dt = t1 - t0;
		if (dt <= 0.0)
			return 1.0f;
		return glm::clamp(static_cast<float>((static_cast<double>(animationTime) - t0) / dt), 0.0f, 1.0f);
	}

	static aiVector3D CalcInterpolatedPosition(float animationTime, const aiNodeAnim* nodeAnim)
	{
		if (nodeAnim->mNumPositionKeys == 1)
			return nodeAnim->mPositionKeys[0].mValue;

		uint32_t index = FindKey(animationTime, nodeAnim->mPositionKeys, nodeAnim->mNumPositionKeys);
		uint32_t nextIndex = index + 1;
		float factor = CalcFactor(animationTime, nodeAnim->mPositionKeys[index].mTime, nodeAnim->mPositionKeys[nextIndex].mTime);

		const aiVector3D& start = nodeAnim->mPositionKeys[index].mValue;
		const aiVector3D& end = nodeAnim->mPositionKeys[nextIndex].mValue;
		return start + factor * (end - start);
	}

	static aiQuaternion CalcInterpolatedRotation(float animationTime, const aiNodeAnim* nodeAnim)
	{
		if (nodeAnim->mNumRotationKeys == 1)
			return nodeAnim->mRotationKeys[0].mValue;

		uint32_t index = FindKey(animationTime, nodeAnim->mRotationKeys, nodeAnim->mNumRotationKeys);
		uint32_t nextIndex = index + 1;
		float factor = CalcFactor(animationTime, nodeAnim->mRotationKeys[index].mTime, nodeAnim->mRotationKeys[nextIndex].mTime);

		const aiQuaternion& start = nodeAnim->mRotationKeys[index].mValue;
		const aiQuaternion& end = nodeAnim->mRotationKeys[nextIndex].mValue;
		aiQuaternion out;
		aiQuaternion::Interpolate(out, start, end, factor);
		return out.Normalize();
	}

	static aiVector3D CalcInterpolatedScaling(float animationTime, const aiNodeAnim* nodeAnim)
	{
		if (nodeAnim->mNumScalingKeys == 1)
			return nodeAnim->mScalingKeys[0].mValue;

		uint32_t index = FindKey(animationTime, nodeAnim->mScalingKeys, nodeAnim->mNumScalingKeys);
		uint32_t nextIndex = index + 1;
		float factor = CalcFactor(animationTime, nodeAnim->mScalingKeys[index].mTime, nodeAnim->mScalingKeys[nextIndex].mTime);

		const aiVector3D& start = nodeAnim->mScalingKeys[index].mValue;
		const aiVector3D& end = nodeAnim->mScalingKeys[nextIndex].mValue;
		return start + factor * (end - start);
	}

	struct NodeTRS
	{
		glm::vec3 Position{ 0.0f };
		glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
		glm::vec3 Scale{ 1.0f };
	};

	static NodeTRS SampleNode(const aiNode* node, const aiNodeAnim* ch, float t)
	{
		NodeTRS out;

		const bool hasS = ch && ch->mNumScalingKeys > 0;
		const bool hasR = ch && ch->mNumRotationKeys > 0;
		const bool hasP = ch && ch->mNumPositionKeys > 0;

		if (!(hasS && hasR && hasP))
		{
			aiVector3D s, p;
			aiQuaternion r;
			node->mTransformation.Decompose(s, r, p);
			out.Position = { p.x, p.y, p.z };
			out.Rotation = glm::quat(r.w, r.x, r.y, r.z);
			out.Scale = { s.x, s.y, s.z };
		}

		if (hasS) { aiVector3D s = CalcInterpolatedScaling(t, ch);  out.Scale = { s.x, s.y, s.z }; }
		if (hasR) { aiQuaternion r = CalcInterpolatedRotation(t, ch); out.Rotation = glm::quat(r.w, r.x, r.y, r.z); }
		if (hasP) { aiVector3D p = CalcInterpolatedPosition(t, ch); out.Position = { p.x, p.y, p.z }; }

		return out;
	}

	static NodeTRS BlendTRS(const NodeTRS& a, const NodeTRS& b, float f)
	{
		NodeTRS out;
		out.Position = glm::mix(a.Position, b.Position, f);
		out.Scale = glm::mix(a.Scale, b.Scale, f);
		out.Rotation = glm::normalize(glm::slerp(a.Rotation, b.Rotation, f));
		return out;
	}

	static glm::mat4 ComposeTRS(const NodeTRS& x)
	{
		return glm::translate(glm::mat4(1.0f), x.Position)
			* glm::toMat4(x.Rotation)
			* glm::scale(glm::mat4(1.0f), x.Scale);
	}

	AnimatedModel::AnimatedModel(const std::string& filepath)
	{
		LoadModel(filepath);
	}

	AnimatedModel::AnimatedModel(const std::string& filepath, const ModelImportSettings& settings)
	{
		LoadModel(filepath, settings);
	}

	void AnimatedModel::LoadModel(const std::string& filepath)
	{
		LoadModel(filepath, ModelImportSettings{});
	}

	void AnimatedModel::LoadModel(const std::string& filepath, const ModelImportSettings& settings)
	{
		m_ModelSettings = settings;

		m_Meshes.clear();
		m_TextureCache.clear();
		m_BoneInfoMap.clear();
		m_FinalBoneTransforms.clear();
		m_ChannelMaps.clear();
		m_Scene = nullptr;

		m_CurrentAnimIndex = 0;
		m_CurrentAnimTime = 0.0f;
		m_AnimationSpeed = 1.0f;
		m_IsPaused = false;

		m_BlendMode = BlendMode::None;
		m_BlendFromIndex = 0;
		m_BlendFromTime = 0.0f;
		m_BlendFactor = 0.0f;
		m_BlendDuration = 0.0f;
		m_BlendElapsed = 0.0f;

		unsigned int flags =
			aiProcess_Triangulate |
			aiProcess_LimitBoneWeights |
			aiProcess_PopulateArmatureData |
			aiProcess_SortByPType |
			aiProcess_GenUVCoords |
			aiProcess_GenBoundingBoxes;

		if (settings.GenSmoothNormals) flags |= aiProcess_GenSmoothNormals;
		if (settings.CalcTangentSpace) flags |= aiProcess_CalcTangentSpace;
		if (settings.FlipUVs)          flags |= aiProcess_FlipUVs;
		if (settings.MakeLeftHanded)   flags |= aiProcess_MakeLeftHanded;
		if (settings.FlipWindingOrder) flags |= aiProcess_FlipWindingOrder;

		if (settings.PreTransformVertices)
			ENGINE_LOG_WARN("AnimatedModel: PreTransformVertices is ignored (it removes bones and animations).");

		if (settings.CoordinateSystem == 2)
		{
			flags |= aiProcess_ConvertToLeftHanded;
		}

		m_Scene = m_Importer.ReadFile(filepath, flags);

		if (!m_Scene || m_Scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !m_Scene->mRootNode)
		{
			ENGINE_LOG_ERROR("ASSIMP ERROR: {0}", m_Importer.GetErrorString());
			ENGINE_ASSERT(false, "AnimatedModel load failed!");
			m_Scene = nullptr;
			return;
		}

		m_FilePath = filepath;

		size_t lastSlash = filepath.find_last_of("/\\");
		m_Directory = (lastSlash == std::string::npos) ? "." : filepath.substr(0, lastSlash);

		m_GlobalInverseTransform = glm::inverse(ConvertAssimpMatrix(m_Scene->mRootNode->mTransformation));

		m_Meshes.reserve(m_Scene->mNumMeshes);
		processNode(m_Scene->mRootNode, m_Scene);

		if (m_BoneInfoMap.size() > MaxBones)
		{
			ENGINE_LOG_WARN("AnimatedModel '{0}' has {1} bones, exceeding MaxBones ({2}). Skinning will be incorrect.",
				filepath, m_BoneInfoMap.size(), MaxBones);
		}

		m_FinalBoneTransforms.resize(m_BoneInfoMap.size(), glm::mat4(1.0f));

		buildChannelMaps();
		evaluatePose();
	}

	void AnimatedModel::buildChannelMaps()
	{
		m_ChannelMaps.clear();
		if (!m_Scene) return;

		m_ChannelMaps.resize(m_Scene->mNumAnimations);
		for (uint32_t a = 0; a < m_Scene->mNumAnimations; ++a)
		{
			const aiAnimation* anim = m_Scene->mAnimations[a];
			auto& map = m_ChannelMaps[a];
			map.reserve(anim->mNumChannels);
			for (uint32_t c = 0; c < anim->mNumChannels; ++c)
				map[anim->mChannels[c]->mNodeName.C_Str()] = anim->mChannels[c];
		}
	}

	void AnimatedModel::processNode(aiNode* node, const aiScene* scene)
	{
		for (uint32_t i = 0; i < node->mNumMeshes; i++)
		{
			aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
			m_Meshes.push_back(processMesh(mesh, scene));
		}

		for (uint32_t i = 0; i < node->mNumChildren; i++)
		{
			processNode(node->mChildren[i], scene);
		}
	}

	AnimatedMesh AnimatedModel::processMesh(aiMesh* mesh, const aiScene* scene)
	{
		std::vector<AnimatedVertex> vertices(mesh->mNumVertices);

		const bool hasNormals = mesh->HasNormals();
		const bool hasUVs = mesh->HasTextureCoords(0);

		for (uint32_t i = 0; i < mesh->mNumVertices; i++)
		{
			vertices[i].Position = { mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z };

			if (hasNormals)
				vertices[i].Normal = { mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z };
			else
				vertices[i].Normal = { 0.0f, 1.0f, 0.0f };

			if (hasUVs)
				vertices[i].TexCoords = { mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y };
			else
				vertices[i].TexCoords = { 0.0f, 0.0f };
		}

		extractBoneWeights(vertices, mesh);

		size_t totalIndices = 0;
		for (uint32_t i = 0; i < mesh->mNumFaces; i++)
			totalIndices += mesh->mFaces[i].mNumIndices;

		std::vector<unsigned int> indices;
		indices.reserve(totalIndices);

		for (uint32_t i = 0; i < mesh->mNumFaces; i++)
		{
			const aiFace& face = mesh->mFaces[i];
			for (uint32_t j = 0; j < face.mNumIndices; j++)
			{
				indices.push_back(face.mIndices[j]);
			}
		}

		auto meshMaterial = CreateReference<Material>();

		if (mesh->mMaterialIndex < scene->mNumMaterials)
		{
			aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];

			aiColor3D diffuseColor(1.0f, 1.0f, 1.0f);
			if (material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuseColor) == AI_SUCCESS)
				meshMaterial->DiffuseColor = glm::vec4(diffuseColor.r, diffuseColor.g, diffuseColor.b, 1.0f);

			aiColor3D specularColor(1.0f, 1.0f, 1.0f);
			if (material->Get(AI_MATKEY_COLOR_SPECULAR, specularColor) == AI_SUCCESS)
				meshMaterial->SpecularColor = glm::vec3(specularColor.r, specularColor.g, specularColor.b);

			float shininess = 32.0f;
			if (material->Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS)
				meshMaterial->Shininess = shininess;

			int twoSided = 0;
			if (material->Get(AI_MATKEY_TWOSIDED, twoSided) == AI_SUCCESS)
				meshMaterial->DoubleSided = (twoSided != 0);

			meshMaterial->DiffuseMap = loadMaterialTexture(material, aiTextureType_DIFFUSE);
			meshMaterial->SpecularMap = loadMaterialTexture(material, aiTextureType_SPECULAR);

			meshMaterial->NormalMap = loadMaterialTexture(material, aiTextureType_NORMALS);
			if (!meshMaterial->NormalMap)
				meshMaterial->NormalMap = loadMaterialTexture(material, aiTextureType_HEIGHT);
		}

		return AnimatedMesh(vertices, indices, meshMaterial);
	}

	void AnimatedModel::extractBoneWeights(std::vector<AnimatedVertex>& vertices, aiMesh* mesh)
	{
		for (uint32_t boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex)
		{
			int boneID = -1;
			std::string boneName = mesh->mBones[boneIndex]->mName.C_Str();

			auto it = m_BoneInfoMap.find(boneName);
			if (it == m_BoneInfoMap.end())
			{
				BoneInfo newBoneInfo;
				newBoneInfo.id = static_cast<int>(m_BoneInfoMap.size());
				newBoneInfo.offsetMatrix = ConvertAssimpMatrix(mesh->mBones[boneIndex]->mOffsetMatrix);
				m_BoneInfoMap[boneName] = newBoneInfo;
				boneID = newBoneInfo.id;
			}
			else
			{
				boneID = it->second.id;
			}

			auto weights = mesh->mBones[boneIndex]->mWeights;
			uint32_t numWeights = mesh->mBones[boneIndex]->mNumWeights;

			for (uint32_t weightIndex = 0; weightIndex < numWeights; ++weightIndex)
			{
				uint32_t vertexId = weights[weightIndex].mVertexId;
				float weight = weights[weightIndex].mWeight;
				vertices[vertexId].AddBoneData(boneID, weight);
			}
		}
	}

	void AnimatedModel::advanceTime(uint32_t animIndex, float& time, float deltaTime)
	{
		const aiAnimation* anim = m_Scene->mAnimations[animIndex];
		if (anim->mDuration <= 0.0)
			return;

		float ticksPerSecond = static_cast<float>(anim->mTicksPerSecond != 0 ? anim->mTicksPerSecond : 25.0);
		float duration = static_cast<float>(anim->mDuration);

		time += deltaTime * ticksPerSecond * m_AnimationSpeed;
		time = std::fmod(time, duration);
		if (time < 0.0f)
			time += duration;
	}

	void AnimatedModel::UpdateAnimation(float deltaTime)
	{
		if (!m_Scene || !m_Scene->HasAnimations() || m_CurrentAnimIndex >= m_Scene->mNumAnimations)
			return;

		if (m_IsPaused)
			return;

		advanceTime(m_CurrentAnimIndex, m_CurrentAnimTime, deltaTime);

		switch (m_BlendMode)
		{
		case BlendMode::CrossFade:
		{
			advanceTime(m_BlendFromIndex, m_BlendFromTime, deltaTime);
			m_BlendElapsed += deltaTime * m_AnimationSpeed;

			float t = glm::clamp(m_BlendElapsed / m_BlendDuration, 0.0f, 1.0f);
			m_BlendFactor = t * t * (3.0f - 2.0f * t);

			if (t >= 1.0f)
				m_BlendMode = BlendMode::None;
			break;
		}
		case BlendMode::Manual:
		{
			const aiAnimation* a = m_Scene->mAnimations[m_CurrentAnimIndex];
			const aiAnimation* b = m_Scene->mAnimations[m_BlendFromIndex];
			if (a->mDuration > 0.0 && b->mDuration > 0.0)
				m_BlendFromTime = (m_CurrentAnimTime / static_cast<float>(a->mDuration)) * static_cast<float>(b->mDuration);
			break;
		}
		default:
			break;
		}

		evaluatePose();
	}

	void AnimatedModel::evaluatePose()
	{
		if (!m_Scene || !m_Scene->mRootNode)
			return;

		updateNodeHierarchy(m_Scene->mRootNode, glm::mat4(1.0f));
	}

	std::string AnimatedModel::GetAnimationName(uint32_t index) const
	{
		if (!m_Scene || index >= m_Scene->mNumAnimations) return "";
		return m_Scene->mAnimations[index]->mName.C_Str();
	}

	void AnimatedModel::SetAnimationSpeed(float speed)
	{
		m_AnimationSpeed = glm::clamp(speed, 0.0f, 10.0f);
	}

	void AnimatedModel::SetAnimation(uint32_t index)
	{
		if (!m_Scene || index >= m_Scene->mNumAnimations)
		{
			ENGINE_LOG_ERROR("Invalid animation index: {0}", index);
			return;
		}

		const bool wasBlending = m_BlendMode != BlendMode::None;
		m_BlendMode = BlendMode::None;

		if (index != m_CurrentAnimIndex || wasBlending)
		{
			m_CurrentAnimIndex = index;
			m_CurrentAnimTime = 0.0f;

			evaluatePose();
		}
	}

	void AnimatedModel::CrossFadeTo(uint32_t index, float duration)
	{
		if (!m_Scene || index >= m_Scene->mNumAnimations)
		{
			ENGINE_LOG_ERROR("Invalid animation index: {0}", index);
			return;
		}

		if (index == m_CurrentAnimIndex && m_BlendMode == BlendMode::None)
			return;

		if (duration <= 0.0f)
		{
			SetAnimation(index);
			return;
		}

		m_BlendFromIndex = m_CurrentAnimIndex;
		m_BlendFromTime = m_CurrentAnimTime;
		m_CurrentAnimIndex = index;
		m_CurrentAnimTime = 0.0f;

		m_BlendDuration = duration;
		m_BlendElapsed = 0.0f;
		m_BlendFactor = 0.0f;
		m_BlendMode = BlendMode::CrossFade;
	}

	void AnimatedModel::SetBlend(uint32_t fromIndex, uint32_t toIndex, float factor)
	{
		if (!m_Scene || fromIndex >= m_Scene->mNumAnimations || toIndex >= m_Scene->mNumAnimations)
			return;

		if (m_BlendMode != BlendMode::Manual || m_BlendFromIndex != fromIndex || m_CurrentAnimIndex != toIndex)
		{
			m_BlendFromIndex = fromIndex;
			m_CurrentAnimIndex = toIndex;
			m_CurrentAnimTime = 0.0f;
			m_BlendFromTime = 0.0f;
		}

		m_BlendFactor = glm::clamp(factor, 0.0f, 1.0f);
		m_BlendMode = BlendMode::Manual;
	}

	void AnimatedModel::updateNodeHierarchy(const aiNode* pNode, const glm::mat4& parentTransform)
	{
		const std::string nodeName(pNode->mName.data);
		glm::mat4 nodeTransform = ConvertAssimpMatrix(pNode->mTransformation);

		if (m_Scene->HasAnimations() && m_CurrentAnimIndex < m_Scene->mNumAnimations)
		{
			const aiNodeAnim* chTo = findNodeAnim(m_CurrentAnimIndex, nodeName);

			if (m_BlendMode == BlendMode::None)
			{
				if (chTo)
					nodeTransform = ComposeTRS(SampleNode(pNode, chTo, m_CurrentAnimTime));
			}
			else
			{
				const aiNodeAnim* chFrom = findNodeAnim(m_BlendFromIndex, nodeName);

				if (chTo || chFrom)
				{
					NodeTRS from = SampleNode(pNode, chFrom, m_BlendFromTime);
					NodeTRS to = SampleNode(pNode, chTo, m_CurrentAnimTime);
					nodeTransform = ComposeTRS(BlendTRS(from, to, m_BlendFactor));
				}
			}
		}

		glm::mat4 globalTransform = parentTransform * nodeTransform;

		auto boneIt = m_BoneInfoMap.find(nodeName);
		if (boneIt != m_BoneInfoMap.end())
		{
			const BoneInfo& info = boneIt->second;
			m_FinalBoneTransforms[info.id] = m_GlobalInverseTransform * globalTransform * info.offsetMatrix;
		}

		for (uint32_t i = 0; i < pNode->mNumChildren; ++i)
		{
			updateNodeHierarchy(pNode->mChildren[i], globalTransform);
		}
	}

	const aiNodeAnim* AnimatedModel::findNodeAnim(uint32_t animIndex, const std::string& nodeName) const
	{
		if (animIndex >= m_ChannelMaps.size())
			return nullptr;

		const auto& map = m_ChannelMaps[animIndex];
		auto it = map.find(nodeName);
		return it != map.end() ? it->second : nullptr;
	}

	Reference<Texture2D> AnimatedModel::loadMaterialTexture(aiMaterial* mat, aiTextureType type)
	{
		if (mat->GetTextureCount(type) == 0) return nullptr;

		aiString str;
		mat->GetTexture(type, 0, &str);
		std::string path = str.C_Str();

		auto it = m_TextureCache.find(path);
		if (it != m_TextureCache.end())
		{
			return it->second;
		}

		if (m_Scene && m_Scene->GetEmbeddedTexture(path.c_str()))
		{
			ENGINE_LOG_WARN("AnimatedModel: embedded texture '{0}' is not supported yet, skipping.", path);
			m_TextureCache[path] = nullptr;
			return nullptr;
		}

		std::string filename = m_Directory + "/" + path;
		Reference<Texture2D> texture = Texture2D::Create(filename);

		m_TextureCache[path] = texture;
		return texture;
	}

}