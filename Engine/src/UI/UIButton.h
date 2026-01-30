#pragma once
#include "UIBase.h"
#include "UI_InputComponent.h"
#include "UI_ImageComponent.h"
#include <DirectXMath.h>


// 아직 프로토타입
class UIButton : public UIBase
{
public:
	UIButton();
	~UIButton();

	void Initalize(UIRenderStruct& UIRenderStruct, CompDelegates& tmpDelegate) override;
	void Update() override;
	void Render() override;

	// 이미지 경로 설정
	void SetNormalImage(const std::wstring& path);
	void SetHoverImage(const std::wstring& path);
	void SetPressedImage(const std::wstring& path);
	void SetClickedImage(const std::wstring& path);

	// InputComponent 이벤트 콜백 설정
	void SetOnHoverBegin(std::function<void()> callback);
	void SetOnHoverEnd(std::function<void()> callback);
	void SetOnPressed(std::function<void()> callback);
	void SetOnReleased(std::function<void()> callback);
	void SetOnClicked(std::function<void()> callback);

	// 상태 확인
	bool IsHovered() const { return m_input ? m_input->bIsHovered : false; }
	bool IsPressed() const { return m_input ? m_input->bIsPressed : false; }

	// InputComponent 업데이트 (UIWorld와 InputSystem 필요)
	void UpdateInput(UIWorld& world, Alice::InputSystem& input);

private:
	// InputComponent (멤버로 직접 관리)
	UI_InputComponent* m_input = nullptr;

	// ImageComponent 4개 (멤버로 직접 관리, 벡터 아님)
	UI_ImageComponent* m_imageNormal = nullptr;    // 일반 상태
	UI_ImageComponent* m_imageHover = nullptr;     // 호버 상태
	UI_ImageComponent* m_imagePressed = nullptr;   // 눌림 상태
	UI_ImageComponent* m_imageClicked = nullptr;    // 클릭 상태

	// 현재 렌더링할 이미지 결정
	UI_ImageComponent* GetCurrentImage() const;
};
