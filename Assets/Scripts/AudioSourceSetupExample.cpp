#include "AudioSourceSetupExample.h"

#include "Core/ScriptFactory.h"
#include "Core/GameObject.h"
#include "Components/AudioSourceComponent.h"

namespace Alice
{
    REGISTER_SCRIPT(AudioSourceSetupExample);

    void AudioSourceSetupExample::Start()
    {
        auto go = gameObject();
        auto* src = go.GetComponent<AudioSourceComponent>();
        if (!src)
            src = &go.AddComponent<AudioSourceComponent>();

        src->soundPath = soundPath;
        src->is3D = is3D;
        src->volume = volume;
        src->minDistance = minDistance;
        src->maxDistance = maxDistance;
        src->debugDraw = debugDraw;
    }

    void AudioSourceSetupExample::Update(float dt)
    {
        // 런타임 프로퍼티 동기화
        auto go = gameObject();
        if (auto* src = go.GetComponent<AudioSourceComponent>())
        {
            src->minDistance = minDistance;
            src->maxDistance = maxDistance;
            src->debugDraw = debugDraw;
        }
    }
}
