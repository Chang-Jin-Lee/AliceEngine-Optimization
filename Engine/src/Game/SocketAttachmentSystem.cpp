#include "Game/SocketAttachmentSystem.h"

#include <unordered_set>
#include <DirectXMath.h>

#include "Core/World.h"
#include "Core/Logger.h"
#include "Components/TransformComponent.h"
#include "Components/IDComponent.h"
#include "Components/SocketAttachmentComponent.h"
#include "Components/AdvancedAnimationComponent.h"
#include "Components/SocketComponent.h"
#include "Components/SocketPoseOutputComponent.h"

namespace Alice
{
	namespace
	{
		EntityId ResolveOwner(World& world, SocketAttachmentComponent& att)
		{
			if (att.ownerCached != InvalidEntityId)
			{
				if (att.ownerGuid == 0)
					return att.ownerCached;

				if (const auto* idc = world.GetComponent<IDComponent>(att.ownerCached))
				{
					if (idc->guid == att.ownerGuid)
						return att.ownerCached;
				}
			}

			if (att.ownerGuid == 0)
				return InvalidEntityId;

			EntityId resolved = world.FindEntityByGuid(att.ownerGuid);
			if (resolved == InvalidEntityId)
				return InvalidEntityId;

			att.ownerCached = resolved;
			return resolved;
		}

		bool TryGetSocketWorldMatrix(World& world, EntityId owner, const std::string& socketName, DirectX::XMMATRIX& out)
		{
			if (auto* poses = world.GetComponent<SocketPoseOutputComponent>(owner))
			{
				for (const auto& p : poses->poses)
				{
					if (p.name == socketName)
					{
						out = DirectX::XMLoadFloat4x4(&p.world);
						return true;
					}
				}
			}

			// 1) Match by socket name (e.g. "Hurt_HandR", "Trace_Base")
			if (auto* adv = world.GetComponent<AdvancedAnimationComponent>(owner))
			{
				for (const auto& s : adv->sockets)
				{
					if (s.name == socketName)
					{
						out = DirectX::XMLoadFloat4x4(&s.worldMatrix);
						return true;
					}
				}
				// 2) Fallback: match by parent bone name (e.g. "??.R" -> socket that has parentBone "??.R")
				for (const auto& s : adv->sockets)
				{
					if (s.parentBone == socketName)
					{
						out = DirectX::XMLoadFloat4x4(&s.worldMatrix);
						return true;
					}
				}
			}

			if (auto* sc = world.GetComponent<SocketComponent>(owner))
			{
				for (const auto& s : sc->sockets)
				{
					if (s.name == socketName)
					{
						out = DirectX::XMLoadFloat4x4(&s.world);
						return true;
					}
				}
				for (const auto& s : sc->sockets)
				{
					if (s.parentBone == socketName)
					{
						out = DirectX::XMLoadFloat4x4(&s.world);
						return true;
					}
				}
			}

			return false;
		}
	}

	void SocketAttachmentSystem::Update(World& world)
	{
		auto&& attachments = world.GetComponents<SocketAttachmentComponent>(); // & -> &&?? ???
		if (attachments.empty())
			return;

		using namespace DirectX;
		static std::unordered_set<EntityId> s_loggedOwnerFail;
		static std::unordered_set<EntityId> s_loggedSocketFail;

		for (auto&& [eid, att] : attachments)
		{
			auto* tr = world.GetComponent<TransformComponent>(eid);
			if (!tr || !tr->enabled)
				continue;

			const EntityId owner = ResolveOwner(world, att);
			if (owner == InvalidEntityId || att.socketName.empty())
			{
				if (s_loggedOwnerFail.find(eid) == s_loggedOwnerFail.end())
				{
					s_loggedOwnerFail.insert(eid);
					ALICE_LOG_WARN("[SocketAttachment] Owner resolve failed: entity=\"%s\" id=%llu ownerGuid=%llu ownerNameDebug=\"%s\" socketName=\"%s\" (ownerGuid=0 or entity not found)",
						world.GetEntityName(eid).c_str(),
						static_cast<unsigned long long>(eid),
						static_cast<unsigned long long>(att.ownerGuid),
						att.ownerNameDebug.c_str(),
						att.socketName.c_str());
				}
				continue;
			}

			XMMATRIX socketWorld = XMMatrixIdentity();
			if (!TryGetSocketWorldMatrix(world, owner, att.socketName, socketWorld))
			{
				if (s_loggedSocketFail.find(eid) == s_loggedSocketFail.end())
				{
					s_loggedSocketFail.insert(eid);
					ALICE_LOG_WARN("[SocketAttachment] Socket not found: entity=\"%s\" id=%llu ownerGuid=%llu socketName=\"%s\" (no matching socket in owner AdvancedAnimation.sockets or SocketComponent.sockets)",
						world.GetEntityName(eid).c_str(),
						static_cast<unsigned long long>(eid),
						static_cast<unsigned long long>(att.ownerGuid),
						att.socketName.c_str());
				}
				continue;
			}

			const XMMATRIX extra =
				XMMatrixScaling(att.extraScale.x, att.extraScale.y, att.extraScale.z) *
				XMMatrixRotationRollPitchYaw(att.extraRotRad.x, att.extraRotRad.y, att.extraRotRad.z) *
				XMMatrixTranslation(att.extraPos.x, att.extraPos.y, att.extraPos.z);

			const XMMATRIX finalM = socketWorld * extra;

			XMVECTOR S, R, T;
			if (!XMMatrixDecompose(&S, &R, &T, finalM))
				continue;

			XMStoreFloat3(&tr->position, T);

			XMFLOAT4 q;
			XMStoreFloat4(&q, R);
			tr->SetRotation(q);

			if (att.followScale)
			{
				XMStoreFloat3(&tr->scale, S);
			}

			tr->parent = InvalidEntityId;
			world.MarkTransformDirty(eid);
		}
	}
}
