// PhysicsMath.h
#pragma once

#include <directxtk/SimpleMath.h>

#include <cmath>

namespace physwrap
{
	using Vec3 = DirectX::SimpleMath::Vector3;
	using Quat = DirectX::SimpleMath::Quaternion;

	// ------------------------------------------------------------------
	// These helpers are intentionally PhysX-free.
	// Keep them usable from engine code without pulling PhysX headers.
	// ------------------------------------------------------------------

	inline bool NormalizeSafe(Vec3& v, float eps = 1e-8f)
	{
		const float len2 = v.x * v.x + v.y * v.y + v.z * v.z;
		if (len2 <= eps * eps) return false;
		const float invLen = 1.0f / std::sqrt(len2);
		v.x *= invLen;
		v.y *= invLen;
		v.z *= invLen;
		return true;
	}

	inline Quat NormalizeSafe(const Quat& q, float eps = 1e-8f)
	{
		const float len2 = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
		if (len2 <= eps * eps) return Quat::Identity;
		const float invLen = 1.0f / std::sqrt(len2);
		return Quat(q.x * invLen, q.y * invLen, q.z * invLen, q.w * invLen);
	}

	inline bool IsFinite(const Vec3& v)
	{
		return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
	}

	inline bool IsFinite(const Quat& q)
	{
		return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w);
	}
}
