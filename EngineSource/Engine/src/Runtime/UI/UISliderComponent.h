#pragma once

#include <string>
#include <DirectXMath.h>
#include "Runtime/UI/UICommon.h"

namespace Alice
{
	struct UISliderComponent
	{
		bool enabled{ true };
		AliceUI::UIButtonState state{ AliceUI::UIButtonState::Normal };

		// Normalized slider value [0,1].
		float value{ 0.0f };
		AliceUI::UIGaugeDirection direction{ AliceUI::UIGaugeDirection::LeftToRight };
		bool syncGauge{ true };

		// Handle visuals.
		float handleSize{ 16.0f }; // pixels
		DirectX::XMFLOAT2 backgroundSize{ 0.0f, 0.0f }; // 0 = use widget size
		DirectX::XMFLOAT4 normalTint{ 1.0f, 1.0f, 1.0f, 1.0f };
		DirectX::XMFLOAT4 hoveredTint{ 1.0f, 1.0f, 1.0f, 1.0f };
		DirectX::XMFLOAT4 pressedTint{ 0.85f, 0.85f, 0.85f, 1.0f };
		DirectX::XMFLOAT4 disabledTint{ 0.4f, 0.4f, 0.4f, 1.0f };

		std::string normalTexture;
		std::string hoveredTexture;
		std::string pressedTexture;
		std::string disabledTexture;
		std::string backgroundTexture;
		// Runtime
		bool dragging{ false };
		bool wasPressed{ false };
	};
}
