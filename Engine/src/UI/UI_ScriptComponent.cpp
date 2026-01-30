#include "UI_ScriptComponent.h"
#include "IUIScript.h"

UI_ScriptComponent::~UI_ScriptComponent()
{
	OnRemoved();
}

void UI_ScriptComponent::OnRemoved()
{
	if (instance)
	{
		instance->OnRemoved();
		instance.reset();
	}
}
