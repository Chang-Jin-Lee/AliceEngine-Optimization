#include "UISceneManager.h"
#include <dxgi1_2.h>
#include <d2d1_1.h>
#include <dwrite.h>
#pragma comment(lib, "dwrite.lib")
#include <d2d1.h>
#pragma comment(lib, "d2d1.lib")
#include <wrl/client.h>
#include <string>
#include <stdexcept>
#include "Core/InputSystem.h"
#include "Core/Logger.h"
#include "UITransform.h"
#include "UI_ImageComponent.h"
#include "UI_ScriptComponent.h"
#include "IUIComponent.h"
#include "UIBase.h"
#include "UIScriptSystem.h"
#include "UIScriptFactory.h"
#include "UI_InputComponent.h"
#include "UIButton.h"
// ============================================================================
// UIWorld 구현 ����
// ============================================================================
void UIWorld::Initialize(UIRenderStruct* UIRst)
{
	assert(UIRst && "UIWorld::Initialize: UIRst must not be null");
	m_UIRenderStruct = UIRst;
	nowInteger = 1;

	// ��������Ʈ ���ε� (���ø� ���� ���� ����)
	m_worldDelegates.AddComponent.BindLambda(
		[this](ObjectID id, std::type_index type, std::function<void* ()> factory) -> void*
		{
			void* raw = nullptr;

			// UITextComponent 없음 (추후 추가 시 복구)
			// if (type == typeid(UITextComponent))
			// {
			// 	if (out) *out = static_cast<void*>(this->CreateTextComponent(static_cast<unsigned long>(id)));
			// 	return;
			// }

			// 1) type 별 고정 생성(월드 소유)
			if (type == typeid(UITransform))
			{
				raw = static_cast<void*>(this->CreateTransformComponent(static_cast<unsigned long>(id)));
			}
			else if (type == typeid(UI_ImageComponent))
			{
				raw = static_cast<void*>(this->CreateImageComponent(static_cast<unsigned long>(id)));
			}
			else if (type == typeid(UI_ScriptComponent))
			{
				raw = static_cast<void*>(this->CreateScriptComponent(static_cast<unsigned long>(id)));
			}
			// 2) 기타 타입: factory 생성(현재는 소유권 TODO)
			else if (factory)
			{
				raw = factory();
			}

			// 3) owner 세팅 (공통)
			if (raw)
			{
				UIBase* owner = this->Get(static_cast<unsigned long>(id)); // UIWorld::Get
				// owner가 없을 수도 있으니 체크
				if (owner)
				{
					// raw가 IUIComponent이면 세팅
					IUIComponent* comp = static_cast<IUIComponent*>(raw);
					if (comp != nullptr)
					{
						comp->Owner = owner;
						comp->OwnerID = static_cast<unsigned long>(id);
						comp->OnAdded();
					}
				}
			}

			return raw;
		}
	);

	m_worldDelegates.FindComponent.BindLambda(
		[this](ObjectID id, std::type_index type) -> void*
		{

			// UITextComponent 없음 (추후 추가 시 복구)
			// if (type == typeid(UITextComponent))
			// {
			// 	if (out) *out = static_cast<void*>(this->FindTextComponent(static_cast<unsigned long>(id)));
			// 	return;
			// }

			// UITransform 조회
			if (type == typeid(UITransform))
			{
				return static_cast<void*>(this->FindTransformComponent(static_cast<unsigned long>(id)));
			}
			// UI_ImageComponent 조회
			if (type == typeid(UI_ImageComponent))
			{
				return static_cast<void*>(this->FindImageComponent(static_cast<unsigned long>(id)));
			}
			if (type == typeid(UI_ScriptComponent))
			{
				return static_cast<void*>(this->FindScriptComponent(static_cast<unsigned long>(id)));
			}
			return nullptr;
		}
	);

	// 추후에 bool type으로 고치기!!
	m_worldDelegates.RemoveComponent.BindLambda(
		[this](ObjectID id, std::type_index type)
		{
			// UITextComponent 없음 (추후 추가 시 복구)
			// if (type == typeid(UITextComponent))
			// {
			// 	this->RemoveTextComponent(static_cast<unsigned long>(id));
			// 	return;
			// }

			// UITransform 삭제
			if (type == typeid(UITransform))
			{
				this->RemoveTransformComponent(static_cast<unsigned long>(id));
				return;
			}
			// UI_ImageComponent 삭제
			if (type == typeid(UI_ImageComponent))
			{
				this->RemoveImageComponent(static_cast<unsigned long>(id));
				return;
			}
			if (type == typeid(UI_ScriptComponent))
			{
				this->RemoveScriptComponent(static_cast<unsigned long>(id));
				return;
			}
		}
	);
}

//UITextComponent* UIWorld::CreateTextComponent(unsigned long ownerID)
//{
//	// 이미 해당 ownerID에 컴포넌트가 있다면 그대로 반환
//	auto it = m_compStorage.find(ownerID);
//	if (it != m_compStorage.end())
//		return it->second.get();
//
//	// 새 컴포넌트 생성 후 저장
//	auto comp = std::make_unique<UITextComponent>();
//	comp->owner = ownerID;
//	UITextComponent* raw = comp.get();
//	m_compStorage.emplace(ownerID, std::move(comp));
//	return raw;
//}

UITransform* UIWorld::CreateTransformComponent(unsigned long ownerID)
{
	// �̹� �ش� ownerID�� ������Ʈ�� �ִٸ� �״�� ��ȯ
	auto it = m_transformStorage.find(ownerID);
	if (it != m_transformStorage.end())
		return it->second.get();

	// �� ������Ʈ ���� �� ������ ���� ����
	auto comp = std::make_unique<UITransform>();
	comp->owner = ownerID;
	UITransform* raw = comp.get();
	
	// Owner/OwnerID는 AddComponent 델리게이트에서 설정됨
	// 직접 호출 시에는 여기서 설정 (델리게이트를 거치지 않는 경우)
	if (UIBase* owner = this->Get(ownerID))
	{
		raw->Owner = owner;
		raw->OwnerID = ownerID;
	}
	
	m_transformStorage.emplace(ownerID, std::move(comp));
	return raw;
}

/*
UITextComponent* UIWorld::FindTextComponent(unsigned long ownerID)
{
	auto it = m_compStorage.find(ownerID);
	return (it == m_compStorage.end()) ? nullptr : it->second.get();
}
*/

UITransform* UIWorld::FindTransformComponent(unsigned long ownerID)
{
	auto it = m_transformStorage.find(ownerID);
	return (it == m_transformStorage.end()) ? nullptr : it->second.get();
}

const UITransform* UIWorld::FindTransformComponent(unsigned long ownerID) const
{
	auto it = m_transformStorage.find(ownerID);
	return (it == m_transformStorage.end()) ? nullptr : it->second.get();
}

/*
void UIWorld::RemoveTextComponent(unsigned long ownerID)
{
	m_compStorage.erase(ownerID);
}
*/

void UIWorld::RemoveTransformComponent(unsigned long ownerID)
{
	m_transformStorage.erase(ownerID);
}

UI_ImageComponent* UIWorld::CreateImageComponent(unsigned long ownerID)
{
	// 이미 해당 ownerID에 컴포넌트가 있다면 그대로 반환
	auto it = m_imageComponentStorage.find(ownerID);
	if (it != m_imageComponentStorage.end())
		return it->second.get();

	// 새 컴포넌트 생성 후 저장
	auto comp = std::make_unique<UI_ImageComponent>();
	comp->Initalize(*m_UIRenderStruct);
	UI_ImageComponent* raw = comp.get();
	
	// Owner/OwnerID는 AddComponent 델리게이트에서 설정됨
	// 직접 호출 시에는 여기서 설정 (델리게이트를 거치지 않는 경우)
	if (UIBase* owner = this->Get(ownerID))
	{
		raw->Owner = owner;
		raw->OwnerID = ownerID;
	}
	m_imageComponentStorage.emplace(ownerID, std::move(comp));
	return raw;
}

UI_ImageComponent* UIWorld::FindImageComponent(unsigned long ownerID)
{
	auto it = m_imageComponentStorage.find(ownerID);
	return (it == m_imageComponentStorage.end()) ? nullptr : it->second.get();
}

const UI_ImageComponent* UIWorld::FindImageComponent(unsigned long ownerID) const
{
	auto it = m_imageComponentStorage.find(ownerID);
	return (it == m_imageComponentStorage.end()) ? nullptr : it->second.get();
}

void UIWorld::RemoveImageComponent(unsigned long ownerID)
{
	m_imageComponentStorage.erase(ownerID);
}

UI_ScriptComponent* UIWorld::CreateScriptComponent(unsigned long ownerID)
{
	// 이미 해당 ownerID에 컴포넌트가 있다면 그대로 반환
	auto it = m_scriptComponentStorage.find(ownerID);
	if (it != m_scriptComponentStorage.end())
		return it->second.get();

	auto comp = std::make_unique<UI_ScriptComponent>();
	UI_ScriptComponent* raw = comp.get();
	
	// Owner/OwnerID는 AddComponent 델리게이트에서 설정됨
	// 직접 호출 시에는 여기서 설정 (델리게이트를 거치지 않는 경우)
	if (UIBase* owner = this->Get(ownerID))
	{
		raw->Owner = owner;
		raw->OwnerID = ownerID;
	}
	m_scriptComponentStorage.emplace(ownerID, std::move(comp));
	return raw;
}

UI_ScriptComponent* UIWorld::FindScriptComponent(unsigned long ownerID)
{
	auto it = m_scriptComponentStorage.find(ownerID);
	return (it == m_scriptComponentStorage.end()) ? nullptr : it->second.get();
}

const UI_ScriptComponent* UIWorld::FindScriptComponent(unsigned long ownerID) const
{
	auto it = m_scriptComponentStorage.find(ownerID);
	return (it == m_scriptComponentStorage.end()) ? nullptr : it->second.get();
}

void UIWorld::RemoveScriptComponent(unsigned long ownerID)
{
	auto it = m_scriptComponentStorage.find(ownerID);
	if (it != m_scriptComponentStorage.end())
	{
		if (it->second)
		{
			it->second->OnRemoved();
		}
		m_scriptComponentStorage.erase(it);
	}
}

// ============================================================================
// UI Script 관리 구현 (여러 스크립트 지원)
// ============================================================================
UIScriptEntry& UIWorld::AddUIScript(unsigned long ownerID, const std::string& scriptName)
{
	UIScriptEntry entry{};
	entry.scriptName = scriptName;
	
	// UIBase 참조 가져오기
	UIBase* owner = Get(ownerID);
	if (!owner)
	{
		ALICE_LOG_WARN("[UIWorld] AddUIScript: Owner UIBase not found for ID=%lu", ownerID);
		// owner가 없어도 entry는 추가 (나중에 EnsureInstance에서 처리)
	}
	
	// 스크립트 인스턴스 생성 시도
	entry.instance = UIScriptFactory::Create(scriptName.c_str());
	if (entry.instance)
	{
		entry.instance->Owner = owner;
		entry.instance->OwnerID = ownerID;
		//ALICE_LOG_INFO("[UIWorld] AddUIScript: Created script '%s' for UI ID=%lu", scriptName.c_str(), ownerID);
	}
	else
	{
		ALICE_LOG_WARN("[UIWorld] AddUIScript: Failed to create script '%s' for UI ID=%lu (will be created later)", scriptName.c_str(), ownerID);
	}
	
	m_scripts[ownerID].push_back(std::move(entry));
	return m_scripts[ownerID].back();
}

std::vector<UIScriptEntry>* UIWorld::GetUIScripts(unsigned long ownerID)
{
	auto it = m_scripts.find(ownerID);
	if (it == m_scripts.end())
		return nullptr;
	return &it->second;
}

const std::vector<UIScriptEntry>* UIWorld::GetUIScripts(unsigned long ownerID) const
{
	auto it = m_scripts.find(ownerID);
	if (it == m_scripts.end())
		return nullptr;
	return &it->second;
}

void UIWorld::RemoveUIScript(unsigned long ownerID, std::size_t index)
{
	auto it = m_scripts.find(ownerID);
	if (it == m_scripts.end())
		return;
	
	auto& list = it->second;
	if (index >= list.size())
		return;
	
	// OnRemoved 호출
	if (list[index].instance)
	{
		list[index].instance->OnRemoved();
	}
	
	list.erase(list.begin() + static_cast<std::ptrdiff_t>(index));
	if (list.empty())
		m_scripts.erase(it);
}

UIBase* UIWorld::Get(long unsigned int ID)
{
	auto it = pUIObjStorage.find(ID);
	if (it == pUIObjStorage.end())
		return nullptr;
	return it->second.get();
}

const UIBase* UIWorld::Get(long unsigned int ID) const
{
	auto it = pUIObjStorage.find(ID);
	if (it == pUIObjStorage.end())
		return nullptr;
	return it->second.get();
}

bool UIWorld::DestroyEntity(long unsigned int ID)
{
	UIBase* tmpNode = Get(ID);
	if (!tmpNode) return false;

	// 1. �θ𿡼� ��� ����
	long unsigned int parentID{ tmpNode->parentID };
	if (parentID != 0)
	{
		UIBase* parentNode = Get(parentID);
		if (parentNode != nullptr)
		{
			auto& ChildVect = parentNode->childIDStorage;
			ChildVect.erase(
				std::remove(ChildVect.begin(), ChildVect.end(), ID),
				ChildVect.end()
			);
		}
	}
	else
	{   // ��Ʈ����� ���!
		m_rootID.erase(
			std::remove(m_rootID.begin(), m_rootID.end(), ID),
			m_rootID.end()
		);
	}

	// 2. ������Ʈ�� �Բ� ����
	RemoveTransformComponent(ID);
	RemoveImageComponent(ID);
	RemoveScriptComponent(ID);

	// 3. ����Ʈ�� ��ü ���� (��������� �ڽĵ鵵 ����)
	return DeleteChildObjects(ID);
}

bool UIWorld::DeleteChildObjects(long unsigned int ID)
{
	UIBase* tmpNode = Get(ID);
	if (!tmpNode) return false;

	// �ڽĵ��� ���� ���� (���纻 ��� - ������ �����ǹǷ�)
	auto children = tmpNode->childIDStorage; // ����
	for (auto cid : children)
	{
		// �� �ڽ��� ������Ʈ�� ����
		RemoveTransformComponent(cid);
		RemoveImageComponent(cid);
		RemoveScriptComponent(cid);
		DeleteChildObjects(cid);
	}

	// ��ƼƼ ����
	pUIObjStorage.erase(ID);
	return true;
}

void UIWorld::Clear()
{
	// ��� ��ƼƼ ����
	pUIObjStorage.clear();
	m_rootID.clear();

	// ��� ������Ʈ ����
	// m_compStorage.clear(); // UITextComponent 없음
	m_transformStorage.clear();
	m_imageComponentStorage.clear();
	m_scriptComponentStorage.clear();

	// World Epoch ���� (���� �ڵ���� ��ȿȭ��)
	m_worldEpoch++;

	// nowInteger�� reset���� ���� (���� ��å ����)
}

// ============================================================================
// UILayoutSystem ����
// ============================================================================
void UILayoutSystem::UpdateTransforms(UIWorld& world)
{
	// ��Ʈ UI ����� ���� 
	for (auto rootID : world.GetRootIDs())
	{
		if (auto* root = world.Get(rootID))
			UpdateTransformChild(world, root, D2D1::Matrix3x2F::Identity());
	}
}

void UILayoutSystem::UpdateTransformChild(UIWorld& world, UIBase* node, const D2D1::Matrix3x2F& parentWorld)
{
	// Transform은 UIBase 캐시로 접근 (생성 직후 1회 부착 정책)
	auto& tr = node->GetTransform();
	D2D1::Matrix3x2F worldMat = tr.WorldMatrix(parentWorld);

	for (auto childID : node->childIDStorage)
	{
		if (auto* child = world.Get(childID))
			UpdateTransformChild(world, child, worldMat);
	}
}

void UILayoutSystem::UpdateUI(UIWorld& world)
{
	for (auto rootID : world.GetRootIDs())
	{
		if (auto* root = world.Get(rootID))
		{
			root->Update();
			world.Traverse(root,
				[](UIBase* node)
				{
					node->Update();
				}
			);
		}
	}
}

// ============================================================================
// UIHitTestSystem ����
// ============================================================================
long unsigned UIHitTestSystem::FindUIUnderPointer(UIWorld& world, XMFLOAT2 MousePos, UIRenderStruct* renderStruct)
{
	std::vector<long unsigned> hitMouseID;
	hitMouseID.clear();

	XMFLOAT2 unityMousePos = {
		MousePos.x - renderStruct->m_width * 0.5f,
		renderStruct->m_height * 0.5f - MousePos.y
	};

	std::vector<long unsigned> tmpStorage;
	long unsigned tmpID{ 0 };

	// AABB(ȸ�� ���� ��ü AABB)
	for (auto rootID : world.GetRootIDs())
	{
		if (auto* root = world.Get(rootID))
		{
			if (!root->IsMouseOverUIAABB(unityMousePos, tmpStorage))
				continue;

			AABBRoot(world, unityMousePos, root, tmpStorage);
		}
	}

	// ȸ���ִ� ��ü �˻�
	for (auto rootID : tmpStorage)
	{
		if (auto* root = world.Get(rootID))
		{
			if (root->IsMouseOverUIRot(unityMousePos))
			{
				hitMouseID.push_back(root->getID());
			}

			UIRotRoot(world, unityMousePos, root, hitMouseID);
		}
	}

	if (hitMouseID.size() != 0)
		tmpID = hitMouseID.back();

	return tmpID;
}

void UIHitTestSystem::AABBRoot(UIWorld& world, XMFLOAT2 MousePos, UIBase* node, std::vector<long unsigned>& IDStorage)
{
	for (auto childID : node->childIDStorage)
	{
		if (auto* child = world.Get(childID))
		{
			if (!child->IsMouseOverUIAABB(MousePos, IDStorage))
				continue;

			AABBRoot(world, MousePos, child, IDStorage);
		}
	}
}

void UIHitTestSystem::UIRotRoot(UIWorld& world, XMFLOAT2 MousePos, UIBase* node, std::vector<long unsigned>& hitMouseID)
{
	for (auto childID : node->childIDStorage)
	{
		if (auto* child = world.Get(childID))
		{
			if (!child->IsMouseOverUIRot(MousePos))
				continue;
			hitMouseID.push_back(child->getID());
			UIRotRoot(world, MousePos, child, hitMouseID);
		}
	}
}

// ============================================================================
// UIEventSystem ����
// ============================================================================
void UIEventSystem::UpdatePointer(UIWorld& world, Alice::InputSystem* inputSystem, UIRenderStruct* renderStruct)
{
	/*auto mouseState = inputSystem->m_Mouse->GetState();
	XMFLOAT2 MousePos = { (float)mouseState.x, (float)mouseState.y };

	auto nowID = UIHitTestSystem::FindUIUnderPointer(world, MousePos, renderStruct);
	if (nowID == 0) return;

	auto* nowUI = world.Get(nowID);
	if (!nowUI) return;

	auto& mouseTracker = inputSystem->m_MouseStateTracker;
	if (mouseTracker.leftButton == Mouse::ButtonStateTracker::UP)
	{
		nowUI->m_uiState = UIState::Normal;
	}

	if (mouseTracker.leftButton == Mouse::ButtonStateTracker::PRESSED)
	{
		nowUI->m_uiState = UIState::Pressed;
	}

	if (mouseTracker.leftButton == Mouse::ButtonStateTracker::RELEASED)
	{
		nowUI->m_uiState = UIState::Release;
	}

	if (mouseTracker.leftButton == Mouse::ButtonStateTracker::HELD)
	{
		nowUI->m_uiState = UIState::Hold;
	}*/
}

// ============================================================================
// UIRenderSystem ����
// ============================================================================
void UIRenderSystem::Render(UIWorld& world, UIRenderStruct* renderStruct)
{
	renderStruct->m_d2DdevCon->BeginDraw();
	renderStruct->m_d2DdevCon->Clear(D2D1::ColorF(0, 0, 0, 0));

	RenderRoot(world, renderStruct);

	renderStruct->m_d2DdevCon->EndDraw();
}

void UIRenderSystem::RenderRoot(UIWorld& world, UIRenderStruct* renderStruct)
{
	// ��Ʈ UI ����� ���� 
	for (auto rootID : world.GetRootIDs())
	{
		if (auto* root = world.Get(rootID))
		{
			root->Render();
			RenderRootChild(world, root);
		}
	}
}

void UIRenderSystem::RenderRootChild(UIWorld& world, UIBase* node)
{
	for (auto childID : node->childIDStorage)
	{
		if (auto* child = world.Get(childID))
		{
			child->Render();
			RenderRootChild(world, child);
		}
	}
}

// ============================================================================
// UISceneManager ����
// ============================================================================
void UISceneManager::initalize(ID3D11Device* Dev, ID3D11DeviceContext* DevCon, UIRenderStruct* UIRst, Alice::InputSystem* tmpInput)
{
	pDev = Dev;
	pDevCon = DevCon;
	m_UIRenderStruct = UIRst;
	m_InputSystem = tmpInput;

	// UIWorld �ʱ�ȭ
	m_world.Initialize(UIRst);
}

void UISceneManager::Update()
{
	// Layout System: Transform ����
	UILayoutSystem::UpdateTransforms(m_world);

	// Layout System: UI Update
	UILayoutSystem::UpdateUI(m_world);

	// Script System: UI_ScriptComponent 업데이트
	UIScriptSystem::Tick(m_world, 0.0f); // dt가 아직 별도로 관리되지 않아 0 전달

	// Image System: UI_ImageComponent 업데이트
	UIImageSystem::Update(m_world);

	// Input System: UI_InputComponent 업데이트 (Hover/Click 판정)
	UIInputSystem::Update(m_world, *m_InputSystem);

	// UIButton의 InputComponent 업데이트 (AddComponent를 사용하지 않은 경우)
	// 모든 루트 엔티티와 자식들을 순회하면서 UIButton을 찾아 UpdateInput 호출
	for (auto rootID : m_world.GetRootIDs())
	{
		if (auto* root = m_world.Get(rootID))
		{
			UpdateButtonInputRecursive(root);
		}
	}

	// Event System: ���콺 �Է� ó��
	UIEventSystem::UpdatePointer(m_world, m_InputSystem, m_UIRenderStruct);
}

void UISceneManager::Render()
{
	if (!m_UIRenderStruct || !m_UIRenderStruct->m_d2DdevCon) 
	{
		ALICE_LOG_WARN("[UISceneManager] Render skipped: invalid render struct");
		return;
	}

	m_UIRenderStruct->m_d2DdevCon->BeginDraw();
	m_UIRenderStruct->m_d2DdevCon->Clear(D2D1::ColorF(0, 0, 0, 0));
	
	UIImageSystem::Render(m_world, m_UIRenderStruct);

	HRESULT hr = m_UIRenderStruct->m_d2DdevCon->EndDraw();
	
	if (FAILED(hr))
	{
		ALICE_LOG_ERRORF("[UISceneManager] EndDraw failed: HRESULT=0x%08X", hr);
		
		if (hr == D2DERR_RECREATE_TARGET)
		{
			ALICE_LOG_WARN("[UISceneManager] D2DERR_RECREATE_TARGET - target needs recreation");
		}
		
		// 첫 프레임 재시도 (1회만)
		static bool s_retryAttempted = false;
		if (!s_retryAttempted && m_UIRenderStruct->m_d2dTargetBitmap)
		{
			s_retryAttempted = true;
			m_UIRenderStruct->m_d2DdevCon->SetTarget(m_UIRenderStruct->m_d2dTargetBitmap.Get());
			hr = m_UIRenderStruct->m_d2DdevCon->EndDraw();
			if (SUCCEEDED(hr))
			{
				//ALICE_LOG_INFO("[UISceneManager] EndDraw retry succeeded");
			}
			else
			{
				ALICE_LOG_ERRORF("[UISceneManager] EndDraw retry failed: HRESULT=0x%08X", hr);
			}
		}
	}
}

// ============================================================================
// UIImageSystem 구현
// ============================================================================
void UIImageSystem::Update(UIWorld& world)
{
	UpdateRoot(world);
}

void UIImageSystem::UpdateRoot(UIWorld& world)
{
	// 루트 UI 엔티티들 순회
	for (auto rootID : world.GetRootIDs())
	{
		if (auto* root = world.Get(rootID))
		{
			// ImageComponent 업데이트
			if (auto* imageComp = root->TryGetComponent<UI_ImageComponent>())
			{
				imageComp->Update();
			}
			// 자식들도 재귀적으로 업데이트
			UpdateRootChild(world, root);
		}
	}
}

void UIImageSystem::UpdateRootChild(UIWorld& world, UIBase* node)
{
	for (auto childID : node->childIDStorage)
	{
		if (auto* child = world.Get(childID))
		{
			// ImageComponent 업데이트
			if (auto* imageComp = child->TryGetComponent<UI_ImageComponent>())
			{
				imageComp->Update();
			}
			// 재귀적으로 자식들도 업데이트
			UpdateRootChild(world, child);
		}
	}
}

void UIImageSystem::Render(UIWorld& world, UIRenderStruct* renderStruct)
{
	// ALICE_LOG_INFO("[UIImageSystem::Render] Render called: renderStruct=%p", renderStruct);
	if (!renderStruct) 
	{
		// ALICE_LOG_WARN("[UIImageSystem::Render] Render skipped: renderStruct is null");
		return;
	}
	// ALICE_LOG_INFO("[UIImageSystem::Render] Calling RenderRoot");
	RenderRoot(world, renderStruct);
	// ALICE_LOG_INFO("[UIImageSystem::Render] RenderRoot completed");
}

void UIImageSystem::RenderRoot(UIWorld& world, UIRenderStruct* renderStruct)
{
	const auto& rootIDs = world.GetRootIDs();
	// ALICE_LOG_INFO("[UIImageSystem::RenderRoot] RenderRoot called: rootIDs.size()=%zu", rootIDs.size());
	
	// 루트 UI 엔티티들 순회
	for (size_t i = 0; i < rootIDs.size(); ++i)
	{
		auto rootID = rootIDs[i];
		// ALICE_LOG_INFO("[UIImageSystem::RenderRoot] Processing rootID[%zu]=%lu", i, rootID);
		
		if (auto* root = world.Get(rootID))
		{
			// ALICE_LOG_INFO("[UIImageSystem::RenderRoot] Found root entity: rootID=%lu", rootID);
			// ImageComponent 렌더링
			if (auto* imageComp = root->TryGetComponent<UI_ImageComponent>())
			{
				// ALICE_LOG_INFO("[UIImageSystem::RenderRoot] Found UI_ImageComponent for rootID=%lu, calling Render()", rootID);
				imageComp->Render();
				// ALICE_LOG_INFO("[UIImageSystem::RenderRoot] UI_ImageComponent::Render() completed for rootID=%lu", rootID);
			}
			else
			{
				// ALICE_LOG_WARN("[UIImageSystem::RenderRoot] No UI_ImageComponent found for rootID=%lu", rootID);
			}
			// 자식들도 재귀적으로 렌더링
			// ALICE_LOG_INFO("[UIImageSystem::RenderRoot] Calling RenderRootChild for rootID=%lu", rootID);
			RenderRootChild(world, root, renderStruct);
		}
		else
		{
			// ALICE_LOG_WARN("[UIImageSystem::RenderRoot] Root entity not found: rootID=%lu", rootID);
		}
	}
	// ALICE_LOG_INFO("[UIImageSystem::RenderRoot] RenderRoot completed");
}

void UIImageSystem::RenderRootChild(UIWorld& world, UIBase* node, UIRenderStruct* /*renderStruct*/)
{
	// ALICE_LOG_INFO("[UIImageSystem::RenderRootChild] RenderRootChild called: node=%p, childCount=%zu", 
	//                node, node ? node->childIDStorage.size() : 0);
	
	for (auto childID : node->childIDStorage)
	{
		// ALICE_LOG_INFO("[UIImageSystem::RenderRootChild] Processing childID=%lu", childID);
		if (auto* child = world.Get(childID))
		{
			// ALICE_LOG_INFO("[UIImageSystem::RenderRootChild] Found child entity: childID=%lu", childID);
			// ImageComponent 렌더링
			if (auto* imageComp = child->TryGetComponent<UI_ImageComponent>())
			{
				// ALICE_LOG_INFO("[UIImageSystem::RenderRootChild] Found UI_ImageComponent for childID=%lu, calling Render()", childID);
				imageComp->Render();
				// ALICE_LOG_INFO("[UIImageSystem::RenderRootChild] UI_ImageComponent::Render() completed for childID=%lu", childID);
			}
			else
			{
				// ALICE_LOG_WARN("[UIImageSystem::RenderRootChild] No UI_ImageComponent found for childID=%lu", childID);
			}
			// 재귀적으로 자식들도 렌더링
			RenderRootChild(world, child, nullptr);
		}
		else
		{
			// ALICE_LOG_WARN("[UIImageSystem::RenderRootChild] Child entity not found: childID=%lu", childID);
		}
	}
}

// ============================================================================
// UIInputSystem 구현
// ============================================================================
void UIInputSystem::Update(UIWorld& world, Alice::InputSystem& input)
{
	UpdateRoot(world, input);
}

void UIInputSystem::UpdateRoot(UIWorld& world, Alice::InputSystem& input)
{
	// 루트 UI 엔티티들 순회
	for (auto rootID : world.GetRootIDs())
	{
		if (auto* root = world.Get(rootID))
		{
			// InputComponent 업데이트
			if (auto* inputComp = root->TryGetComponent<UI_InputComponent>())
			{
				inputComp->Update(world, input);
			}
			// 자식들도 재귀적으로 업데이트
			UpdateRootChild(world, root, input);
		}
	}
}

void UIInputSystem::UpdateRootChild(UIWorld& world, UIBase* node, Alice::InputSystem& input)
{
	for (auto childID : node->childIDStorage)
	{
		if (auto* child = world.Get(childID))
		{
			// InputComponent 업데이트
			if (auto* inputComp = child->TryGetComponent<UI_InputComponent>())
			{
				inputComp->Update(world, input);
			}
			// 재귀적으로 자식들도 업데이트
			UpdateRootChild(world, child, input);
		}
	}
}

// ============================================================================
// UIButton Input 업데이트 헬퍼 (UISceneManager 내부 함수)
// ============================================================================
void UISceneManager::UpdateButtonInputRecursive(UIBase* node)
{
	// 현재 노드가 UIButton인지 확인
	if (auto* button = dynamic_cast<UIButton*>(node))
	{
		button->UpdateInput(m_world, *m_InputSystem);
	}

	// 자식들도 재귀적으로 처리
	for (auto childID : node->childIDStorage)
	{
		if (auto* child = m_world.Get(childID))
		{
			UpdateButtonInputRecursive(child);
		}
	}
}
