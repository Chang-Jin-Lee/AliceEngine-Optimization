#pragma once
#include <cstdint>

class UIBase; // forward declaration


// 컴포넌트의 부모
struct IUIComponent
{
    virtual ~IUIComponent() = default;

    // 주인 포인터
    UIBase* Owner = nullptr;
    unsigned long OwnerID = 0;

    // 추가된 경우
    virtual void OnAdded() {};
    virtual void Update() {};
    virtual void Render() {};

    // ���� ��� (���� ȣȯ�� ����)
    long unsigned id{ 0 };
    long unsigned owner{ 0 };
};