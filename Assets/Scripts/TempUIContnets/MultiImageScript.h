#pragma once

#include <string>
#include <vector>

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include "Runtime/UI/UIImageComponent.h"
#include "Runtime/UI/UIWidgetComponent.h"

namespace Alice
{
    class MultiImageScript : public IScript
    {
        ALICE_BODY(MultiImageScript);

    public:
        void Start() override;
        void Update(float deltaTime) override;
        void SetIndex(int index);

        ALICE_PROPERTY(std::string, imageWidgetName, "");
        ALICE_PROPERTY(std::string, imagePaths, "");
        ALICE_PROPERTY(float, intervalSeconds, 0.2f);
        ALICE_PROPERTY(int, startIndex, 0);
        ALICE_PROPERTY(bool, loop, true);
        ALICE_PROPERTY(bool, playOnStart, true);

        ALICE_FUNC(SetIndex);

    private:
        void ResolveTarget();
        void ParsePaths();
        void ApplyImage();
        void Advance();

        EntityId m_targetId = InvalidEntityId;
        UIImageComponent* m_targetImage = nullptr;
        std::vector<std::string> m_images;
        int m_currentIndex = 0;
        float m_elapsed = 0.0f;
        bool m_playing = false;
        bool m_initialized = false;
    };
}
