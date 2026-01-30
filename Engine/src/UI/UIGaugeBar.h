#pragma once
#include "UI/UIBase.h"
#include "UI/UIImage.h"

class UIGaugeBar : public UIBase 
{
public:

    void Initalize(UIRenderStruct& UIRenderStruct, CompDelegates& worldDelegates) override ;
    void Update() override {};
    void Render()override {};


private:
    UIImage* backGroundImage{ nullptr };
    UIImage* FillImage{ nullptr };
    UIImage* lerpDownImage{ nullptr };
    UIImage* lerpUpImage{ nullptr };
};
