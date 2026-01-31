#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <algorithm>
#include <cmath> // atan2, asin 등을 위해 필요

#include <DirectXMath.h>

namespace Alice
{
	// ---------------------------
	// Anim Notify
	// ---------------------------
	struct AnimNotify
	{
		float timeSec;
		std::function<void()> callback;
		std::uint64_t ownerTag = 0;
	};

	// ---------------------------
	// Advanced animation data
	// ---------------------------
	struct AdvancedAnimLayer
	{
		bool enabled = true;
		bool autoAdvance = true;

		std::string clipA;
		std::string clipB;

		float timeA = 0.0f;
		float timeB = 0.0f;

		float speedA = 1.0f;
		float speedB = 1.0f;

		bool loopA = true;
		bool loopB = true;

		float blend01 = 0.0f;    // A -> B crossfade (0..1)
		float layerAlpha = 1.0f; // Upper layer weight (0..1)
	};

	struct AdvancedAnimAdditive
	{
		bool enabled = false;
		bool autoAdvance = true;

		std::string clip;
		std::string refClip; // reference pose (ex: Idle t=0)

		float time = 0.0f;
		float speed = 1.0f;
		bool loop = false;

		float alpha = 1.0f; // additive strength (0..1)
	};

	struct AdvancedAnimProcedural
	{
		float strength = 0.0f;
		std::uint32_t seed = 0u;
		float timeSec = 0.0f;
	};

	struct AdvancedAnimIK
	{
		bool enabled = false;
		std::string tipBone = "Hand_L";
		int chainLength = 3;
		DirectX::XMFLOAT3 targetMS = { 0.0f, 0.0f, 0.0f };
		float weight = 1.0f;
	};

	struct AdvancedAnimAim
	{
		bool enabled = false;
		float yawRad = 0.0f;
		float weight = 1.0f;
	};

	struct AdvancedAnimSocket
	{
		std::string name;
		std::string parentBone;
		DirectX::XMFLOAT3 pos = { 0.0f, 0.0f, 0.0f };
		DirectX::XMFLOAT3 rotDeg = { 0.0f, 0.0f, 0.0f };
		DirectX::XMFLOAT3 scale = { 1.0f, 1.0f, 1.0f };

		// Runtime output (world space)
		DirectX::XMFLOAT4X4 worldMatrix{ 1,0,0,0,
										 0,1,0,0,
										 0,0,1,0,
										 0,0,0,1 };
	};

	struct AdvancedAnimationComponent
	{
		bool enabled = true;
		bool playing = true;

		AdvancedAnimLayer base;
		AdvancedAnimLayer upper;
		AdvancedAnimAdditive additive;
		AdvancedAnimProcedural procedural;

		// 단일 IK -> 다중 IK 리스트 (발 IK 등 여러 개 동시 지원)
		// 예: 0: 왼발, 1: 오른발, 2: 왼손...
		std::vector<AdvancedAnimIK> ikChains;

		// 기존 코드를 위해 단일 IK 접근 유지 (ikChains[0]과 동기화)
		AdvancedAnimIK ik;

		AdvancedAnimAim aim;

		std::vector<AdvancedAnimSocket> sockets;

		// CPU palette for rendering (auto-filled by AdvancedAnimSystem)
		// 스키닝용 행렬: GlobalInverse * Global * InvBindPose
		std::vector<DirectX::XMFLOAT4X4> palette;

		// --------------------------------------------------------
		// 본 정보 캐싱 (스키닝 행렬에서 순수 Transform 복원용)
		// --------------------------------------------------------
		// 본 이름 -> 본 인덱스(=boneNames 기준) 맵
		std::unordered_map<std::string, int> boneToIndex;
		// 본 인덱스 -> 부모 인덱스 (계층 구조, -1이면 루트)
		std::vector<int> parentIndices;
		// 초기 포즈의 역행렬(InvBind) - Row-Major로 캐싱
		std::vector<DirectX::XMFLOAT4X4> inverseBindMatrices;
		// 모델 글로벌 역행렬(GlobalInverse) - Row-Major로 캐싱
		DirectX::XMFLOAT4X4 globalInverseRow{ 1,0,0,0,
											 0,1,0,0,
											 0,0,1,0,
											 0,0,0,1 };
		// 본 글로벌(Model Space) 행렬 캐시 - Row-Major
		std::vector<DirectX::XMFLOAT4X4> boneGlobals;
		// 캐시 유효성 체크용 메쉬 키 (런타임 전용)
		std::string boneCacheMeshKey;
		// 캐시 유효성 체크용 모델 포인터 (런타임 전용)
		const void* boneCacheModelPtr = nullptr;

		// --------------------------------------------------------
		// Anim Montage & Notify System (언리얼 엔진 스타일)
		// --------------------------------------------------------
		using NotifyMap = std::unordered_map<std::string, std::vector<AnimNotify>>;
		NotifyMap notifies;

		// 노티파이 등록 (어떤 클립의, 몇 초에, 무슨 함수를 실행할지)
		void AddNotify(const std::string& clipName, float time, std::function<void()> func, std::uint64_t ownerTag = 0)
		{
			notifies[clipName].push_back({ time, func, ownerTag });
		}

		// 시스템에서 호출: 시간 범위 내의 노티파이 실행
		void CheckAndFireNotifies(const std::string& clipName, float prevTime, float currTime)
		{
			if (clipName.empty()) return;
			auto it = notifies.find(clipName);
			if (it == notifies.end()) return;

			for (const auto& notify : it->second)
			{
				// 시간 구간 사이에 노티파이가 있는지 확인
				// (일반 재생: prev < notify <= curr)
				// (역재생: prev > notify >= curr)
				bool forwardPass = (prevTime < notify.timeSec && currTime >= notify.timeSec);
				bool backwardPass = (prevTime > notify.timeSec && currTime <= notify.timeSec);

				if (forwardPass || backwardPass)
				{
					if (notify.callback) notify.callback();
				}
			}
		}

		// 시스템에서 호출: 클립 길이를 고려해 노티파이 실행 (시간 clamp)
		void CheckAndFireNotifiesClamped(const std::string& clipName, float prevTime, float currTime, float durationSec)
		{
			if (clipName.empty()) return;
			auto it = notifies.find(clipName);
			if (it == notifies.end()) return;

			const float minTime = 0.0f;
			const float maxTime = (durationSec > 0.0f) ? durationSec : 0.0f;

			for (const auto& notify : it->second)
			{
				float t = notify.timeSec;
				if (durationSec > 0.0f)
					t = (std::clamp)(t, minTime, maxTime);

				bool forwardPass = (prevTime < t && currTime >= t);
				bool backwardPass = (prevTime > t && currTime <= t);

				if (forwardPass || backwardPass)
				{
					if (notify.callback) notify.callback();
				}
			}
		}

		void RemoveNotifiesByTag(std::uint64_t ownerTag)
		{
			if (ownerTag == 0)
				return;

			for (auto it = notifies.begin(); it != notifies.end(); )
			{
				auto& list = it->second;
				list.erase(std::remove_if(list.begin(), list.end(), [ownerTag](const AnimNotify& n) {
					return n.ownerTag == ownerTag;
					}), list.end());

				if (list.empty())
					it = notifies.erase(it);
				else
					++it;
			}
		}


		// Helper: add/update a socket definition
		void SetSocketSRT(const std::string& name,
			const std::string& parentBone,
			DirectX::XMFLOAT3 pos,
			DirectX::XMFLOAT3 rotDeg,
			DirectX::XMFLOAT3 scale)
		{
			for (auto& s : sockets)
			{
				if (s.name == name)
				{
					s.parentBone = parentBone;
					s.pos = pos;
					s.rotDeg = rotDeg;
					s.scale = scale;
					return;
				}
			}

			AdvancedAnimSocket s{};
			s.name = name;
			s.parentBone = parentBone;
			s.pos = pos;
			s.rotDeg = rotDeg;
			s.scale = scale;
			sockets.push_back(std::move(s));
		}

		// Helper: get socket world matrix (identity if missing)
		DirectX::XMMATRIX GetSocketWorldMatrix(const std::string& name) const
		{
			for (const auto& s : sockets)
			{
				if (s.name == name)
					return DirectX::XMLoadFloat4x4(&s.worldMatrix);
			}
			return DirectX::XMMatrixIdentity();
		}

		// 소켓의 월드 Transform(위치, 회전)을 추출하는 헬퍼 함수
		bool GetSocketWorldTransform(const std::string& name, DirectX::XMFLOAT3& outPos, DirectX::XMFLOAT3& outRotDeg) const
		{
			for (const auto& s : sockets)
			{
				if (s.name == name)
				{
					DirectX::XMMATRIX m = DirectX::XMLoadFloat4x4(&s.worldMatrix);

					DirectX::XMVECTOR scale, rotQuat, trans;
					if (!DirectX::XMMatrixDecompose(&scale, &rotQuat, &trans, m))
						return false;

					// Position 저장
					DirectX::XMStoreFloat3(&outPos, trans);

					// Quaternion -> Euler Angles (Degrees) 변환
					DirectX::XMFLOAT4 q;
					DirectX::XMStoreFloat4(&q, rotQuat);

					// 간단한 쿼터니언 -> 오일러 변환 (Y-X-Z 순서 등 엔진 좌표계에 따라 다를 수 있음)
					// 여기서는 일반적인 Pitch(X), Yaw(Y), Roll(Z) 변환 적용
					float sinr_cosp = 2.0f * (q.w * q.x + q.y * q.z);
					float cosr_cosp = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
					float pitch = std::atan2(sinr_cosp, cosr_cosp);

					float sinp = 2.0f * (q.w * q.y - q.z * q.x);
					float yaw = 0.0f;
					if (std::abs(sinp) >= 1.0f)
						yaw = std::copysign(3.14159265f / 2.0f, sinp); // Use 90 degrees if out of range
					else
						yaw = std::asin(sinp);

					float siny_cosp = 2.0f * (q.w * q.z + q.x * q.y);
					float cosy_cosp = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
					float roll = std::atan2(siny_cosp, cosy_cosp);

					// Radian -> Degree 변환
					constexpr float ToDeg = 180.0f / 3.14159265f;
					outRotDeg.x = pitch * ToDeg;
					outRotDeg.y = yaw * ToDeg;
					outRotDeg.z = roll * ToDeg;

					return true;
				}
			}
			return false;
		}

		// IK 체인 설정 헬퍼 함수
		void SetIK(int index, const std::string& boneName, int length, const DirectX::XMFLOAT3& target, float weight = 1.0f)
		{
			if (index < 0)
				return;
			if (index >= (int)ikChains.size())
				ikChains.resize(index + 1);
			ikChains[index].enabled = true;
			ikChains[index].tipBone = boneName;
			ikChains[index].chainLength = length;
			ikChains[index].targetMS = target;
			ikChains[index].weight = weight;

			// index 0이면 기존 ik 변수도 업데이트
			if (index == 0)
			{
				ik = ikChains[0];
			}
		}

		// IK 끄기
		void DisableIK(int index)
		{
			if (index >= 0 && index < (int)ikChains.size())
			{
				ikChains[index].enabled = false;

				// index 0이면 기존 ik 변수도 업데이트
				if (index == 0)
				{
					ik.enabled = false;
				}
			}
		}

		// --------------------------------------------------------
		// 본의 Transform 정보 가져오기
		// --------------------------------------------------------

		/// 스키닝 행렬(palette)에서 순수 Model Space 행렬을 복원하는 헬퍼 함수
		/// 우선 boneGlobals 캐시를 사용하고, 없으면 palette/InvBind/GlobalInverse로 복원합니다.
		bool GetBoneModelMatrix(const std::string& boneName, DirectX::XMMATRIX& outMatrix) const
		{
			auto it = boneToIndex.find(boneName);
			if (it == boneToIndex.end()) return false;

			return GetBoneModelMatrixByIndex(it->second, outMatrix);
		}

		/// 부모 본 기준의 상대 위치 (씬 그래프의 부모 본 기준 로컬 좌표)
		DirectX::XMFLOAT3 GetRelativeLocationToBone(const std::string& boneName) const
		{
			auto it = boneToIndex.find(boneName);
			if (it == boneToIndex.end()) return { 0,0,0 };
			int idx = it->second;

			// 내 Model 행렬
			DirectX::XMMATRIX myModelM;
			if (!GetBoneModelMatrix(boneName, myModelM)) return { 0,0,0 };

			// 부모 Model 행렬
			DirectX::XMMATRIX parentModelM = DirectX::XMMatrixIdentity();
			if (idx >= 0 && idx < (int)parentIndices.size())
			{
				int pIdx = parentIndices[idx];
				DirectX::XMMATRIX pM;
				if (GetBoneModelMatrixByIndex(pIdx, pM))
					parentModelM = pM;
			}

			// Local = Model * Parent_Model^(-1)
			DirectX::XMVECTOR det;
			DirectX::XMMATRIX parentInv = DirectX::XMMatrixInverse(&det, parentModelM);
			DirectX::XMMATRIX localM = myModelM * parentInv;

			DirectX::XMVECTOR s, r, t;
			DirectX::XMMatrixDecompose(&s, &r, &t, localM);
			DirectX::XMFLOAT3 pos;
			DirectX::XMStoreFloat3(&pos, t);
			return pos;
		}

		/// 부모 본 기준의 상대 스케일
		DirectX::XMFLOAT3 GetRelativeScaleToBone(const std::string& boneName) const
		{
			auto it = boneToIndex.find(boneName);
			if (it == boneToIndex.end()) return { 1,1,1 };
			int idx = it->second;

			DirectX::XMMATRIX myModelM;
			if (!GetBoneModelMatrix(boneName, myModelM)) return { 1,1,1 };

			DirectX::XMMATRIX parentModelM = DirectX::XMMatrixIdentity();
			if (idx >= 0 && idx < (int)parentIndices.size())
			{
				int pIdx = parentIndices[idx];
				DirectX::XMMATRIX pM;
				if (GetBoneModelMatrixByIndex(pIdx, pM))
					parentModelM = pM;
			}

			DirectX::XMVECTOR det;
			DirectX::XMMATRIX localM = myModelM * DirectX::XMMatrixInverse(&det, parentModelM);

			DirectX::XMVECTOR s, r, t;
			DirectX::XMMatrixDecompose(&s, &r, &t, localM);
			DirectX::XMFLOAT3 scale;
			DirectX::XMStoreFloat3(&scale, s);
			return scale;
		}

		/// 부모 본 기준의 상대 회전 (오일러 각도, 도 단위)
		DirectX::XMFLOAT3 GetRelativeRotationToBone(const std::string& boneName) const
		{
			auto it = boneToIndex.find(boneName);
			if (it == boneToIndex.end()) return { 0,0,0 };
			int idx = it->second;

			DirectX::XMMATRIX myModelM;
			if (!GetBoneModelMatrix(boneName, myModelM)) return { 0,0,0 };

			DirectX::XMMATRIX parentModelM = DirectX::XMMatrixIdentity();
			if (idx >= 0 && idx < (int)parentIndices.size())
			{
				int pIdx = parentIndices[idx];
				DirectX::XMMATRIX pM;
				if (GetBoneModelMatrixByIndex(pIdx, pM))
					parentModelM = pM;
			}

			DirectX::XMVECTOR det;
			DirectX::XMMATRIX localM = myModelM * DirectX::XMMatrixInverse(&det, parentModelM);

			DirectX::XMVECTOR s, r, t;
			DirectX::XMMatrixDecompose(&s, &r, &t, localM);

			DirectX::XMFLOAT4 q;
			DirectX::XMStoreFloat4(&q, r);
			return QuaternionToEuler(q);
		}

		// --------------------------------------------------------
		// Model Space Getters (캐릭터 원점 기준)
		// 자식 GameObject를 부착할 때 사용합니다.
		// --------------------------------------------------------

		/// 모델 공간 위치 (캐릭터 원점 기준)
		DirectX::XMFLOAT3 GetModelLocationToBone(const std::string& boneName) const
		{
			DirectX::XMMATRIX m;
			if (GetBoneModelMatrix(boneName, m))
			{
				DirectX::XMVECTOR s, r, t;
				DirectX::XMMatrixDecompose(&s, &r, &t, m);
				DirectX::XMFLOAT3 pos;
				DirectX::XMStoreFloat3(&pos, t);
				return pos;
			}
			return { 0,0,0 };
		}

		/// 모델 공간 회전 (캐릭터 원점 기준, 오일러 각도, 도 단위)
		DirectX::XMFLOAT3 GetModelRotationToBone(const std::string& boneName) const
		{
			DirectX::XMMATRIX m;
			if (GetBoneModelMatrix(boneName, m))
			{
				DirectX::XMVECTOR s, r, t;
				DirectX::XMMatrixDecompose(&s, &r, &t, m);
				DirectX::XMFLOAT4 q;
				DirectX::XMStoreFloat4(&q, r);
				return QuaternionToEuler(q);
			}
			return { 0,0,0 };
		}

		/// 모델 공간 스케일 (캐릭터 원점 기준)
		DirectX::XMFLOAT3 GetModelScaleToBone(const std::string& boneName) const
		{
			DirectX::XMMATRIX m;
			if (GetBoneModelMatrix(boneName, m))
			{
				DirectX::XMVECTOR s, r, t;
				DirectX::XMMatrixDecompose(&s, &r, &t, m);
				DirectX::XMFLOAT3 scale;
				DirectX::XMStoreFloat3(&scale, s);
				return scale;
			}
			return { 1,1,1 };
		}

		/// 월드 공간 위치 (캐릭터의 월드 행렬 적용)
		DirectX::XMFLOAT3 GetWorldLocationToBone(const std::string& boneName, const DirectX::XMFLOAT4X4& charWorldMatrix) const
		{
			DirectX::XMMATRIX modelM;
			if (GetBoneModelMatrix(boneName, modelM))
			{
				DirectX::XMMATRIX worldM = DirectX::XMLoadFloat4x4(&charWorldMatrix);
				DirectX::XMMATRIX finalM = modelM * worldM; // Model * World

				DirectX::XMVECTOR s, r, t;
				DirectX::XMMatrixDecompose(&s, &r, &t, finalM);
				DirectX::XMFLOAT3 pos;
				DirectX::XMStoreFloat3(&pos, t);
				return pos;
			}
			// 실패 시 캐릭터 위치
			return { charWorldMatrix._41, charWorldMatrix._42, charWorldMatrix._43 };
		}

		/// 월드 공간 스케일
		DirectX::XMFLOAT3 GetWorldScaleToBone(const std::string& boneName, const DirectX::XMFLOAT4X4& charWorldMatrix) const
		{
			DirectX::XMMATRIX modelM;
			if (GetBoneModelMatrix(boneName, modelM))
			{
				DirectX::XMMATRIX worldM = DirectX::XMLoadFloat4x4(&charWorldMatrix);
				DirectX::XMMATRIX finalM = modelM * worldM;

				DirectX::XMVECTOR s, r, t;
				DirectX::XMMatrixDecompose(&s, &r, &t, finalM);
				DirectX::XMFLOAT3 scale;
				DirectX::XMStoreFloat3(&scale, s);
				return scale;
			}
			return { 1,1,1 };
		}

		/// 월드 공간 회전 (오일러 각도, 도 단위)
		DirectX::XMFLOAT3 GetWorldRotationToBone(const std::string& boneName, const DirectX::XMFLOAT4X4& charWorldMatrix) const
		{
			DirectX::XMMATRIX modelM;
			if (GetBoneModelMatrix(boneName, modelM))
			{
				DirectX::XMMATRIX worldM = DirectX::XMLoadFloat4x4(&charWorldMatrix);
				DirectX::XMMATRIX finalM = modelM * worldM;

				DirectX::XMVECTOR s, r, t;
				DirectX::XMMatrixDecompose(&s, &r, &t, finalM);

				DirectX::XMFLOAT4 q;
				DirectX::XMStoreFloat4(&q, r);
				return QuaternionToEuler(q);
			}
			return { 0,0,0 };
		}

		// --------------------------------------------------------
		// 소켓의 Model Space Transform 가져오기
		// 캐릭터(부모)의 원점을 기준으로 한 소켓(본 + 오프셋)의 Transform을 반환합니다.
		// 무기가 캐릭터의 자식(Child)으로 있을 때 사용합니다.
		// --------------------------------------------------------

		/// 소켓의 Model Space Transform (오일러 각도 버전)
		bool GetSocketModelTransform(const std::string& socketName,
			DirectX::XMFLOAT3& outPos,
			DirectX::XMFLOAT3& outRotDeg,
			DirectX::XMFLOAT3& outScale) const
		{
			DirectX::XMFLOAT4 rotQuat;
			if (!GetSocketModelTransform(socketName, outPos, rotQuat, outScale))
				return false;

			// 쿼터니언 -> 오일러 변환
			outRotDeg = QuaternionToEuler(rotQuat);
			return true;
		}

		/// 소켓의 Model Space Transform (쿼터니언 버전, 권장)
		/// 언리얼 엔진의 소켓 시스템처럼 정확하게 부착하기 위해 쿼터니언 사용
		bool GetSocketModelTransform(const std::string& socketName,
			DirectX::XMFLOAT3& outPos,
			DirectX::XMFLOAT4& outRotQuat,
			DirectX::XMFLOAT3& outScale) const
		{
			// 1. 소켓 데이터 찾기
			const AdvancedAnimSocket* targetSocket = nullptr;
			for (const auto& s : sockets)
			{
				if (s.name == socketName)
				{
					targetSocket = &s;
					break;
				}
			}
			if (!targetSocket) return false;

			// 2. 부모 본의 Model Space 행렬 계산 (캐릭터 원점 기준 본 위치)
			DirectX::XMMATRIX boneM;
			if (!GetBoneModelMatrix(targetSocket->parentBone, boneM)) return false;

			// 3. 소켓의 오프셋 행렬 생성 (Local Offset)
			// 사용자가 설정한 Pos, Rot, Scale을 행렬로 변환
			DirectX::XMMATRIX scaleM = DirectX::XMMatrixScaling(targetSocket->scale.x, targetSocket->scale.y, targetSocket->scale.z);

			DirectX::XMMATRIX rotM = DirectX::XMMatrixRotationRollPitchYaw(
				DirectX::XMConvertToRadians(targetSocket->rotDeg.x),
				DirectX::XMConvertToRadians(targetSocket->rotDeg.y),
				DirectX::XMConvertToRadians(targetSocket->rotDeg.z));

			DirectX::XMMATRIX transM = DirectX::XMMatrixTranslation(targetSocket->pos.x, targetSocket->pos.y, targetSocket->pos.z);

			// 오프셋 행렬 = S * R * T
			DirectX::XMMATRIX offsetM = scaleM * rotM * transM;

			// 4. 최종 Model Space 행렬 계산
			// Final = Offset * Bone (본 위치에 오프셋을 적용)
			DirectX::XMMATRIX finalM = offsetM * boneM;

			// 5. 행렬 분해 (Decompose) 하여 Pos, Rot, Scale 추출
			DirectX::XMVECTOR s, r, t;
			if (!DirectX::XMMatrixDecompose(&s, &r, &t, finalM)) return false;

			DirectX::XMStoreFloat3(&outPos, t);
			DirectX::XMStoreFloat3(&outScale, s);
			DirectX::XMStoreFloat4(&outRotQuat, r); // 회전은 쿼터니언으로 반환 (짐벌락 방지)

			return true;
		}

	private:
		/// 본 인덱스로 모델 공간 행렬을 복원 (row-major 기준)
		bool GetBoneModelMatrixByIndex(int idx, DirectX::XMMATRIX& outMatrix) const
		{
			if (idx < 0)
				return false;

			// 1) 글로벌 캐시가 있으면 바로 사용
			if (idx < (int)boneGlobals.size())
			{
				outMatrix = DirectX::XMLoadFloat4x4(&boneGlobals[idx]);
				return true;
			}

			// 2) 캐시가 없으면 palette/InvBind/GlobalInverse로 복원
			if (idx >= (int)palette.size() || idx >= (int)inverseBindMatrices.size())
				return false;

			DirectX::XMMATRIX skinM = DirectX::XMLoadFloat4x4(&palette[idx]);           // Row-Major
			DirectX::XMMATRIX invBindM = DirectX::XMLoadFloat4x4(&inverseBindMatrices[idx]); // Row-Major
			DirectX::XMMATRIX globalInvM = DirectX::XMLoadFloat4x4(&globalInverseRow);  // Row-Major

			DirectX::XMVECTOR detBind;
			DirectX::XMMATRIX bindM = DirectX::XMMatrixInverse(&detBind, invBindM);

			DirectX::XMVECTOR detGlobal;
			DirectX::XMMATRIX globalInvInv = DirectX::XMMatrixInverse(&detGlobal, globalInvM);

			outMatrix = bindM * skinM * globalInvInv;
			return true;
		}

		/// 쿼터니언을 오일러 각도(도 단위)로 변환
		DirectX::XMFLOAT3 QuaternionToEuler(const DirectX::XMFLOAT4& q) const
		{
			float sinr_cosp = 2.0f * (q.w * q.x + q.y * q.z);
			float cosr_cosp = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
			float pitch = std::atan2(sinr_cosp, cosr_cosp);

			float sinp = 2.0f * (q.w * q.y - q.z * q.x);
			float yaw = 0.0f;
			if (std::abs(sinp) >= 1.0f)
				yaw = std::copysign(3.14159265f / 2.0f, sinp);
			else
				yaw = std::asin(sinp);

			float siny_cosp = 2.0f * (q.w * q.z + q.x * q.y);
			float cosy_cosp = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
			float roll = std::atan2(siny_cosp, cosy_cosp);

			constexpr float ToDeg = 180.0f / 3.14159265f;
			return { pitch * ToDeg, yaw * ToDeg, roll * ToDeg };
		}
	};
}
