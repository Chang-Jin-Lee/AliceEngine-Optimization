#include "Editor/Core/EditorCore.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/UI/UIWidgetComponent.h"
#include "Runtime/UI/UITransformComponent.h"
#include "Runtime/UI/UIImageComponent.h"
#include "Runtime/UI/UITextComponent.h"
#include "Runtime/UI/UIButtonComponent.h"
#include "Runtime/UI/UICheckboxComponent.h"
#include "Runtime/UI/UISliderComponent.h"
#include "Runtime/UI/UIGaugeComponent.h"
#include <DirectXMath.h>

namespace Alice
{
	EntityId EditorCore::CreateAliceUIRoot(World& world, std::string_view name)
	{
		EntityId e = world.CreateEntity();
		world.SetEntityName(e, std::string(name));

		UIWidgetComponent& widget = world.AddComponent<UIWidgetComponent>(e);
		widget.widgetName = std::string(name);
		widget.space = AliceUI::UISpace::Screen;

		UITransformComponent& t = world.AddComponent<UITransformComponent>(e);
		t.anchorMin = DirectX::XMFLOAT2(0.5f, 0.5f);
		t.anchorMax = DirectX::XMFLOAT2(0.5f, 0.5f);
		t.position = DirectX::XMFLOAT2(0.0f, 0.0f);
		t.size = DirectX::XMFLOAT2(200.0f, 80.0f);
		t.pivot = DirectX::XMFLOAT2(0.5f, 0.5f);

		// Always attach a 3D Transform so UI can be switched to World space later.
		world.AddComponent<TransformComponent>(e);

		return e;
	}

	EntityId EditorCore::CreateAliceUIImage(World& world)
	{
		EntityId e = CreateAliceUIRoot(world, "UI_Image");
		if (e != InvalidEntityId)
		{
			world.AddComponent<UIImageComponent>(e);
		}
		return e;
	}

	EntityId EditorCore::CreateAliceUIText(World& world)
	{
		EntityId e = CreateAliceUIRoot(world, "UI_Text");
		if (e != InvalidEntityId)
		{
			UITextComponent& text = world.AddComponent<UITextComponent>(e);
			text.text = "Text";
			text.fontPath = "Resource/Fonts/NotoSansKR-Regular.ttf";
		}
		return e;
	}

	EntityId EditorCore::CreateAliceUIButton(World& world)
	{
		EntityId e = CreateAliceUIRoot(world, "UI_Button");
		if (e != InvalidEntityId)
		{
			world.AddComponent<UIButtonComponent>(e);
			world.AddComponent<UIImageComponent>(e);
			UITextComponent& text = world.AddComponent<UITextComponent>(e);
			text.text = "Button";
			text.fontPath = "Resource/Fonts/NotoSansKR-Regular.ttf";
			UITransformComponent* t = world.GetComponent<UITransformComponent>(e);
			if (t)
				t->size = DirectX::XMFLOAT2(220.0f, 60.0f);
		}
		return e;
	}

	EntityId EditorCore::CreateAliceUIGauge(World& world)
	{
		EntityId e = CreateAliceUIRoot(world, "UI_Gauge");
		if (e != InvalidEntityId)
		{
			world.AddComponent<UIGaugeComponent>(e);
			UITransformComponent* t = world.GetComponent<UITransformComponent>(e);
			if (t)
				t->size = DirectX::XMFLOAT2(260.0f, 24.0f);
		}
		return e;
	}

	EntityId EditorCore::CreateAliceUICheckBox(World& world)
	{
		EntityId e = CreateAliceUIRoot(world, "UI_CheckBox");
		if (e != InvalidEntityId)
		{
			world.AddComponent<UICheckBoxComponent>(e);
			world.AddComponent<UIImageComponent>(e);
			UITransformComponent* t = world.GetComponent<UITransformComponent>(e);
			if (t)
				t->size = DirectX::XMFLOAT2(32.0f, 32.0f);
		}
		return e;
	}

	EntityId EditorCore::CreateAliceUISlider(World& world)
	{
		EntityId e = CreateAliceUIRoot(world, "UI_Slider");
		if (e != InvalidEntityId)
		{
			world.AddComponent<UISliderComponent>(e);
			world.AddComponent<UIGaugeComponent>(e);
			UITransformComponent* t = world.GetComponent<UITransformComponent>(e);
			if (t)
				t->size = DirectX::XMFLOAT2(260.0f, 24.0f);
		}
		return e;
	}

	EntityId EditorCore::CreateAliceUIWorldImage(World& world)
	{
		EntityId e = world.CreateEntity();
		world.SetEntityName(e, "World_UI_Image");

		auto& widget = world.AddComponent<UIWidgetComponent>(e);
		widget.widgetName = "World_UI_Image";
		widget.space = AliceUI::UISpace::World;
		widget.billboard = true;

		auto& uiTransform = world.AddComponent<UITransformComponent>(e);
		uiTransform.size = DirectX::XMFLOAT2(0.6f, 0.6f);

		world.AddComponent<UIImageComponent>(e);

		TransformComponent& t = world.AddComponent<TransformComponent>(e);
		t.position = DirectX::XMFLOAT3(0.0f, 2.0f, 0.0f);

		return e;
	}

}

