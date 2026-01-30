#include "UIButton.h"
#include "UISceneManager.h"
#include "Core/InputSystem.h"
#include "Core/Logger.h"
#include <cassert>

UIButton::UIButton()
{
	// 멤버 컴포넌트들을 직접 생성 (AddComponent 사용 안 함)
	m_input = new UI_InputComponent();
	m_imageNormal = new UI_ImageComponent();
	m_imageHover = new UI_ImageComponent();
	m_imagePressed = new UI_ImageComponent();
	m_imageClicked = new UI_ImageComponent();
}

UIButton::~UIButton()
{
	// 멤버 컴포넌트들을 직접 삭제
	if (m_input) delete m_input;
	if (m_imageNormal) delete m_imageNormal;
	if (m_imageHover) delete m_imageHover;
	if (m_imagePressed) delete m_imagePressed;
	if (m_imageClicked) delete m_imageClicked;
}

void UIButton::Initalize(UIRenderStruct& UIRenderStruct, CompDelegates& tmpDelegate)
{
	// 부모 초기화 (Transform 생성)
	UIBase::Initalize(UIRenderStruct, tmpDelegate);

	// InputComponent 초기화
	if (m_input)
	{
		m_input->Owner = this;
		m_input->OwnerID = ID;
		m_input->OnAdded();
	}

	// ImageComponent 4개 초기화
	if (m_imageNormal)
	{
		m_imageNormal->Owner = this;
		m_imageNormal->OwnerID = ID;
		m_imageNormal->Initalize(UIRenderStruct);
		m_imageNormal->OnAdded();
	}

	if (m_imageHover)
	{
		m_imageHover->Owner = this;
		m_imageHover->OwnerID = ID;
		m_imageHover->Initalize(UIRenderStruct);
		m_imageHover->OnAdded();
	}

	if (m_imagePressed)
	{
		m_imagePressed->Owner = this;
		m_imagePressed->OwnerID = ID;
		m_imagePressed->Initalize(UIRenderStruct);
		m_imagePressed->OnAdded();
	}

	if (m_imageClicked)
	{
		m_imageClicked->Owner = this;
		m_imageClicked->OwnerID = ID;
		m_imageClicked->Initalize(UIRenderStruct);
		m_imageClicked->OnAdded();
	}
}

void UIButton::Update()
{
	// ImageComponent들 업데이트 (UIImageSystem은 AddComponent로 등록된 것만 처리하므로 수동 호출)
	if (m_imageNormal) m_imageNormal->Update();
	if (m_imageHover) m_imageHover->Update();
	if (m_imagePressed) m_imagePressed->Update();
	if (m_imageClicked) m_imageClicked->Update();
}

void UIButton::UpdateInput(UIWorld& world, Alice::InputSystem& input)
{
	if (m_input)
	{
		m_input->Update(world, input);
	}
}

void UIButton::Render()
{
	// 현재 상태에 따라 적절한 이미지 렌더링
	UI_ImageComponent* currentImage = GetCurrentImage();
	if (currentImage)
	{
		currentImage->Render();
	}
}

void UIButton::SetNormalImage(const std::wstring& path)
{
	if (m_imageNormal)
	{
		m_imageNormal->SetImagePath(path);
	}
}

void UIButton::SetHoverImage(const std::wstring& path)
{
	if (m_imageHover)
	{
		m_imageHover->SetImagePath(path);
	}
}

void UIButton::SetPressedImage(const std::wstring& path)
{
	if (m_imagePressed)
	{
		m_imagePressed->SetImagePath(path);
	}
}

void UIButton::SetClickedImage(const std::wstring& path)
{
	if (m_imageClicked)
	{
		m_imageClicked->SetImagePath(path);
	}
}

void UIButton::SetOnHoverBegin(std::function<void()> callback)
{
	if (m_input)
	{
		m_input->OnHoverBegin = callback;
	}
}

void UIButton::SetOnHoverEnd(std::function<void()> callback)
{
	if (m_input)
	{
		m_input->OnHoverEnd = callback;
	}
}

void UIButton::SetOnPressed(std::function<void()> callback)
{
	if (m_input)
	{
		m_input->OnPressed = callback;
	}
}

void UIButton::SetOnReleased(std::function<void()> callback)
{
	if (m_input)
	{
		m_input->OnReleased = callback;
	}
}

void UIButton::SetOnClicked(std::function<void()> callback)
{
	if (m_input)
	{
		m_input->OnClicked = callback;
	}
}

UI_ImageComponent* UIButton::GetCurrentImage() const
{
	if (!m_input)
	{
		return m_imageNormal;
	}

	// 상태에 따라 적절한 이미지 반환
	if (m_input->bIsPressed)
	{
		return m_imagePressed ? m_imagePressed : m_imageNormal;
	}
	else if (m_input->bIsHovered)
	{
		return m_imageHover ? m_imageHover : m_imageNormal;
	}
	else
	{
		return m_imageNormal;
	}
}
