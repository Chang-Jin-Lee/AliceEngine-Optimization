#include "UIImage.h"
#include "UI_ImageComponent.h"

UIImage::UIImage()
{
}

UIImage::~UIImage() {
}

void UIImage::Initalize(UIRenderStruct& UIRenderStruct, CompDelegates& tmpDelegate)
{
	UIBase::Initalize(UIRenderStruct, tmpDelegate);
	m_Image = this->AddComponent<UI_ImageComponent>(); // 이미지 컴포넌트 추가!!!

}

void UIImage::Update()
{

};