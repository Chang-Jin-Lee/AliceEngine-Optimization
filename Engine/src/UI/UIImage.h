#pragma once
#include "UIBase.h"
#include <DirectXMath.h>
#include "UI_ImageComponent.h"

class UIImage : public UIBase
{
public:
	UIImage();
	~UIImage();
public:
	XMFLOAT2 m_srcPos{ 50,50 };
	XMFLOAT2 SrcWidthHeight{ 100, 100 };
	UINT m_index{ 0 };


	void Initalize(UIRenderStruct& UIRenderStruct, CompDelegates& tmpDelegate) override;

	void Update() override;

	void Render() override
	{
		m_Image->Render();
	};

	void createImage(const std::wstring& path)
	{
		m_index = 1;
		auto m_transform = this->GetComponent<UITransform>();
		m_Image->SetImagePath(path);
	}

private:
	D2D1_RECT_F m_srcRect = D2D1::RectF(0, 0, 100, 100);
	UI_ImageComponent* m_Image{nullptr};
	// update에 넣기!!!


};
