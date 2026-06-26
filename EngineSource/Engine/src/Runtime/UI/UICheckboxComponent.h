#pragma once
#include <DirectXMath.h>
#include <string>
#include "Runtime/UI/UICommon.h"

namespace Alice
{
	struct UICheckBoxComponent
	{
		bool enabled{ true };
		AliceUI::UIButtonState state{ AliceUI::UIButtonState::Normal };
		bool isCheck{ false };

		DirectX::XMFLOAT2 backgroundSize{ 5.0f, 5.0f }; 

		std::string normalTexture;
		std::string hoveredTexture;
		std::string pressedTexture;
		std::string disabledTexture;

		DirectX::XMFLOAT4 normalTint{ 1.0f, 1.0f, 1.0f, 1.0f };
		DirectX::XMFLOAT4 hoveredTint{ 1.0f, 1.0f, 1.0f, 1.0f };
		DirectX::XMFLOAT4 pressedTint{ 0.85f, 0.85f, 0.85f, 1.0f };
		DirectX::XMFLOAT4 disabledTint{ 0.4f, 0.4f, 0.4f, 1.0f };

		bool wasPressed{ false };
	};
}
