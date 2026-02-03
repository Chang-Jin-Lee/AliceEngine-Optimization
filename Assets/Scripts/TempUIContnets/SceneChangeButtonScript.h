#pragma once

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include "Runtime/UI/UIButtonComponent.h"

namespace Alice
{
    // 
    class SceneChangeButtonScript : public IScript
    {
        ALICE_BODY(SceneChangeButtonScript);

    public:
        void Start() override;
        void Update(float deltaTime) override;

        // --- ---
        
        // UI
        ALICE_PROPERTY(std::string, rootWidgetName, "UIRoot");
        
        //
        ALICE_PROPERTY(std::string, targetScenePath, "Assets/Scenes/UI/DemoiScene.scene");
        ALICE_PROPERTY(std::string, buttonWidgetName, "UI_StartButton");    
        ALICE_PROPERTY(std::string, TextWidgetName, "UI_StartText");
        ALICE_PROPERTY(std::string, UnderLineWidgetName, "UI_StartUnderLine");

    private:
        UIButtonComponent* changeSceneButton = nullptr;
    };
}
