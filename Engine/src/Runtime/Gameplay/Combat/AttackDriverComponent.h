#pragma once

#include <string>
#include <vector>
#include <cstdint>

#include "Runtime/ECS/Entity.h"

namespace Alice
{
	enum class AttackDriverNotifyType : std::uint8_t
	{
		Attack = 0,
		Dodge = 1,
		Guard = 2,
	};

	enum class AttackDriverClipSource : std::uint8_t
	{
		Explicit = 0,
		BaseA = 1,
		BaseB = 2,
		UpperA = 3,
		UpperB = 4,
		Additive = 5,
	};

	struct AttackDriverClip
	{
		AttackDriverNotifyType type = AttackDriverNotifyType::Attack;
		AttackDriverClipSource source = AttackDriverClipSource::Explicit;
		std::string clipName;
		float startTimeSec = 0.1f;
		float endTimeSec = 0.2f;
		bool enabled = true;
		bool canBeInterrupted = true;
	};

	struct AttackDriverClipHistory
	{
		std::string clipName;
		float prevTimeSec = 0.0f;
		bool valid = false;
	};

	struct AttackDriverComponent
	{
		// 공격을 제어할 트레이스 엔티티 GUID (0이면 자기 자신)
		std::uint64_t traceGuid = 0;
		EntityId traceCached = InvalidEntityId; // 런타임 캐시 (직렬화 금지)

		// AnimNotify 타이밍 목록
		std::vector<AttackDriverClip> clips;

		// 내부 상태
		std::uint64_t registeredHash = 0;
		std::uint64_t notifyTag = 0;

		// 런타임 상태 (직렬화 금지)
		bool attackActive = false;
		bool dodgeActive = false;
		bool guardActive = false;
		bool attackCancelable = true;
		bool cancelAttackRequested = false;

		// 이전 프레임 시간 캐시 (직렬화 금지)
		AttackDriverClipHistory prevBaseA;
		AttackDriverClipHistory prevBaseB;
		AttackDriverClipHistory prevUpperA;
		AttackDriverClipHistory prevUpperB;
		AttackDriverClipHistory prevAdditive;
		AttackDriverClipHistory prevSkinned;
	};
}
