#pragma once

#pragma once
#include <cassert>
#include <DirectXMath.h>
#include <d2d1_1.h>
#include <math.h>
#include <vector>
#include "IUIComponent.h"
using namespace DirectX;

constexpr float degToRad = 3.14159265f / 180.0f;

class UITransform : public IUIComponent
{
public:
	void SetTranslation(float x, float y);
	void SetRotation(float Rotation);
	void SetScale(float scaleX, float scaleY);
	void GetTransformMat(D2D1::Matrix3x2F& tmp);
	void Reset();

	void LocalMat(D2D1::Matrix3x2F& m_localTrans);
	D2D1::Matrix3x2F WorldMatrix(const D2D1::Matrix3x2F& WorldMat);
	D2D1::Matrix3x2F ConVertD2DPos(); // 유니티 -> D2D
	XMFLOAT2 m_screenSize{ 1280, 720 };

	// 컴포넌트가 추가될 때 호출되는 훅
	void OnAdded() override
	{
		// Owner가 제대로 설정되었는지 확인 (디버그 빌드에서만)
		#ifdef _DEBUG
		assert(Owner && "UITransform::OnAdded: Owner must be set");
		assert(OwnerID != 0 && "UITransform::OnAdded: OwnerID must be set");
		#endif
		// Owner->getID() 같은 접근 테스트 가능
	}
private:
	XMFLOAT2 m_totalSize{ 0,0 };
	//void CalRect();

public:
	D2D1::Matrix3x2F m_invWorldTrans;
	D2D1::Matrix3x2F m_worldTrans; // 월드 transfom
public:

	XMFLOAT2 m_translation{ 0,0 };
	float m_rotation{ 0 };
	XMFLOAT2 m_scale{ 1,1 };
	XMFLOAT2 m_size{ 100,100 };
	XMFLOAT2 m_pivot{ 0.5f,0.5f };
};

inline XMFLOAT2 operator/(const XMFLOAT2& v, float s)
{
	return { v.x / s, v.y / s };
}

inline XMFLOAT2 operator+(const XMFLOAT2& v1, const XMFLOAT2& v2)
{
	return { v1.x + v2.x, v1.y + v2.y };
}

inline XMFLOAT2 operator-(const XMFLOAT2& v1, const XMFLOAT2& v2)
{
	return { v1.x - v2.x, v1.y - v2.y };
}
