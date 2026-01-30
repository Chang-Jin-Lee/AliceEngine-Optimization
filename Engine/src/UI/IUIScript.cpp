#include "IUIScript.h"
#include "UIBase.h"
#include "UI_ImageComponent.h"
#include "Core/Logger.h"
#include <DirectXMath.h>

using namespace DirectX;

//UI_ImageComponent* IUIScript::GetImageComponent()
//{
//	if (!Owner) return nullptr;
//	return Owner->TryGetComponent<UI_ImageComponent>();
//}
//
//bool IUIScript::SetImagePath(const std::wstring& path)
//{
//	if (!Owner)
//	{
//		ALICE_LOG_WARN("[IUIScript::SetImagePath] Owner is null!");
//		return false;
//	}
//
//	// ImageComponent가 없으면 자동으로 추가
//	//auto* imageComp = GetImageComponent();
//	//if (!imageComp)
//	//{
//	//	ALICE_LOG_INFO("[IUIScript::SetImagePath] ImageComponent not found, adding it automatically...");
//	//	imageComp = Owner->AddComponent<UI_ImageComponent>();
//	//	if (!imageComp)
//	//	{
//	//		ALICE_LOG_WARN("[IUIScript::SetImagePath] Failed to add UI_ImageComponent!");
//	//		return false;
//	//	}
//	//	ALICE_LOG_INFO("[IUIScript::SetImagePath] UI_ImageComponent added successfully");
//	//}
//
//	ALICE_LOG_INFO("[IUIScript::SetImagePath] Calling SetImagePath with path: %ls", path.c_str());
//	UINT result = imageComp->SetImagePath(path);
//	if (result != 0)
//	{
//		// 이미지 로드 성공 시 Transform 크기도 업데이트
//		auto& transform = Owner->GetTransform();
//		transform.m_size = imageComp->m_size;
//		ALICE_LOG_INFO("[IUIScript::SetImagePath] Image loaded successfully, size=(%.2f, %.2f)", 
//			imageComp->m_size.x, imageComp->m_size.y);
//		return true;
//	}
//
//	ALICE_LOG_WARN("[IUIScript::SetImagePath] SetImagePath returned 0 (failed)");
//	return false;
//}
