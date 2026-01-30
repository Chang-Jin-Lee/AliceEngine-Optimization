#include "UITransform.h"

// 크기 세팅 함수
void UITransform::SetTranslation(float x, float y)
{
	m_translation.x = x; m_translation.y = y;
}

void UITransform::SetRotation(float InRotation)
{
	m_rotation = InRotation;
}

void UITransform::SetScale(float scaleX, float scaleY)
{
	m_scale.x = scaleX; m_scale.y = scaleY;
}


void UITransform::Reset()
{
	m_scale = { 1.0f, 1.0f };
	m_rotation = { 0.0f };
	m_translation = { 0.0f, 0.0f };
}


void UITransform::LocalMat(D2D1::Matrix3x2F& m_localTrans)
{

	m_localTrans =
		D2D1::Matrix3x2F::Scale(m_scale.x, m_scale.y, D2D1::Point2F(m_pivot.x, m_pivot.y)) *
		D2D1::Matrix3x2F::Rotation(m_rotation, D2D1::Point2F(m_pivot.x, m_pivot.y)) *
		D2D1::Matrix3x2F::Translation(m_translation.x, m_translation.y);
}


D2D1::Matrix3x2F UITransform::WorldMatrix(const D2D1::Matrix3x2F& WorldMat)
{

	D2D1::Matrix3x2F LocalMatrix;
	LocalMat(LocalMatrix);
	m_worldTrans = LocalMatrix * WorldMat;

	m_invWorldTrans = m_worldTrans;

	m_invWorldTrans.Invert();

	return m_worldTrans;
}


// 유니티 -> D2D  : Render용 좌표 계산
D2D1::Matrix3x2F UITransform::ConVertD2DPos() {
	// 현재 좌표  + 화면 크리 /2

	D2D1::Matrix3x2F tmpMat =
		D2D1::Matrix3x2F::Scale(1.0f, -1.0f) *
		D2D1::Matrix3x2F::Translation(
			m_screenSize.x * 0.5f,
			m_screenSize.y * 0.5f
		);


	return  D2D1::Matrix3x2F::Scale(1.0f, -1.0f) * (m_worldTrans)*tmpMat;
}


void UITransform::GetTransformMat(D2D1::Matrix3x2F& tmp)
{
	tmp = m_worldTrans;
}

