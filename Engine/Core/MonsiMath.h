#pragma once

#include <cfloat>
#include <glm/glm.hpp>

namespace Monsi::Math {

	bool DecomposeTransform(const glm::mat4& transform, glm::vec3& translation, glm::vec3& rotation, glm::vec3& scale);

	struct AABB
	{
		glm::vec3 Min{ FLT_MAX };
		glm::vec3 Max{ -FLT_MAX };

		void Expand(const glm::vec3& p) { Min = glm::min(Min, p); Max = glm::max(Max, p); }
		bool IsValid() const { return Min.x <= Max.x; }

		AABB Transformed(const glm::mat4& m) const
		{
			const glm::vec3 center = (Min + Max) * 0.5f;
			const glm::vec3 extent = (Max - Min) * 0.5f;

			const glm::vec3 newCenter = glm::vec3(m * glm::vec4(center, 1.0f));
			const glm::vec3 newExtent(glm::abs(m[0][0]) * extent.x + glm::abs(m[1][0]) * extent.y + glm::abs(m[2][0]) * extent.z,
				glm::abs(m[0][1]) * extent.x + glm::abs(m[1][1]) * extent.y + glm::abs(m[2][1]) * extent.z,
				glm::abs(m[0][2]) * extent.x + glm::abs(m[1][2]) * extent.y + glm::abs(m[2][2]) * extent.z);

			AABB result;
			result.Min = newCenter - newExtent;
			result.Max = newCenter + newExtent;
			return result;
		}
		
		AABB Inflated(float factor) const
		{
			if (!IsValid()) {
				return *this;
			}

			const glm::vec3 pad = (Max - Min) * factor;
			AABB result;
			result.Min = Min - pad;
			result.Max = Max + pad;
			return result;
		}
	};

	class Frustum
	{
	public:
		void Update(const glm::mat4& viewProj)
		{
			const glm::mat4 m = glm::transpose(viewProj);

			m_Planes[0] = m[3] + m[0];
			m_Planes[1] = m[3] - m[0];
			m_Planes[2] = m[3] + m[1];
			m_Planes[3] = m[3] - m[1];
			m_Planes[4] = m[3] + m[2];
			m_Planes[5] = m[3] - m[2];
		}

		bool IsPointInsideViewFrustum(const glm::vec3& p) const
		{
			glm::vec4 p4D(p, 1.0f);

			return glm::dot(m_Planes[0], p4D) >= 0 && glm::dot(m_Planes[1], p4D) >= 0 &&
				   glm::dot(m_Planes[2], p4D) >= 0 && glm::dot(m_Planes[3], p4D) >= 0 &&
				   glm::dot(m_Planes[4], p4D) >= 0 && glm::dot(m_Planes[5], p4D) >= 0;
		}

		bool Intersects(const AABB& box) const
		{
			if (!box.IsValid()) {
				return true;
			}

			for (const glm::vec4& p : m_Planes)
			{
				const glm::vec3 v(p.x >= 0.0f ? box.Max.x : box.Min.x,
					p.y >= 0.0f ? box.Max.y : box.Min.y,
					p.z >= 0.0f ? box.Max.z : box.Min.z);

				if (glm::dot(glm::vec3(p), v) + p.w < 0.0f) {
					return false;
				}
			}
			return true;
		}

	private:
		glm::vec4 m_Planes[6];
	};

}