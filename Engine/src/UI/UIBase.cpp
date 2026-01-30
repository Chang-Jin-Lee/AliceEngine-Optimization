#include "UIBase.h"
#include "UITransform.h"
#include "../Core/Delegate.h"
#include "UICompDelegate.h"

void UIBase::Initalize(UIRenderStruct& UIRenderStruct, CompDelegates& worldDelegates)
{
	m_UIRenderStruct = &UIRenderStruct;

	// World 쪽 CompDelegates 포인터 저장
	m_world = &worldDelegates;

	// Transform 필수 생성 + 캐시 (UIBase가 책임)
	if (!Transform)
	{
		Transform = AddComponent<UITransform>();
		if (Transform)
		{
			Transform->m_screenSize = { (float)m_UIRenderStruct->m_width, (float)m_UIRenderStruct->m_height };
		}
	}
	assert(Transform && "UIBase::Initalize: Transform must be set");
}



bool UIBase::IsMouseOverUIAABB(DirectX::XMFLOAT2& tmpPoint, std::vector<long unsigned>& IDStorage)
{
	auto& m_transform = this->GetTransform();
	if (m_transform.m_rotation == 0.0f)
	{
		// ���� ����
		float scaledW = m_transform.m_size.x * m_transform.m_scale.x;
		float scaledH = m_transform.m_size.y * m_transform.m_scale.y;

		// pivot
		float pivotPx = m_transform.m_size.x * m_transform.m_pivot.x * m_transform.m_scale.x;
		float pivotPy = m_transform.m_size.y * m_transform.m_pivot.y * m_transform.m_scale.y;

		const float minX = m_transform.m_translation.x - pivotPx;
		const float minY = m_transform.m_translation.y - pivotPy;
		const float maxX = minX + scaledW;
		const float maxY = minY + scaledH;


		if (tmpPoint.x >= minX && tmpPoint.x <= maxX && tmpPoint.y >= minY && tmpPoint.y <= maxY)
		{
			IDStorage.push_back(ID);
			return true;
		}

		else
			return false;
	}

	IDStorage.push_back(ID);



	return false; 
}


bool UIBase::IsMouseOverUIRot(DirectX::XMFLOAT2& tmpPoint)
{
	auto& tr = this->GetTransform();
	// ���콺 ��ġ�� UI�� ���÷� �ǵ���
	// pos * invMatrix
	float inversePosX =
		tmpPoint.x * tr.m_invWorldTrans._11 +
		tmpPoint.y * tr.m_invWorldTrans._21 +
		tr.m_invWorldTrans._31;

	float inversePosY =
		tmpPoint.x * tr.m_invWorldTrans._12 +
		tmpPoint.y * tr.m_invWorldTrans._22 +
		tr.m_invWorldTrans._32;

	float px = tr.m_size.x * tr.m_pivot.x;
	float py = tr.m_size.y * tr.m_pivot.y;

	// ����
	return (inversePosX >= -px && inversePosX <= tr.m_size.x - px &&
		inversePosY >= -py && inversePosY <= tr.m_size.y - py);
}

