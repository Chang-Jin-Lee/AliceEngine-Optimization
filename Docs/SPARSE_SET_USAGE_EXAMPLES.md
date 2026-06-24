# Sparse Set 패턴 사용 예시

## 개요
World의 컴포넌트 관리가 Sparse Set 패턴으로 리팩토링되었습니다. 이는 메모리 연속성을 확보하고 성능을 개선합니다.

## 주요 특징

1. **메모리 연속성**: 같은 타입의 컴포넌트들이 연속 메모리에 저장되어 캐시 효율 극대화
2. **O(1) 삭제**: Swap-and-Pop 방식으로 중간 요소 삭제 시에도 O(1) 시간 복잡도
3. **안전한 순회**: 순회 중 삭제가 발생해도 안전하게 처리됨

## 사용 예시

### 1. TransformComponent 순회 (읽기 전용)

```cpp
// 헤더 위치
#include "Runtime/ECS/World.h"
#include "Runtime/ECS/Components/TransformComponent.h"

// 모든 Transform 컴포넌트를 효율적으로 순회
for (const auto& [entityId, transform] : world.GetComponents<TransformComponent>())
{
    // transform은 const TransformComponent&
    // 연속 메모리에서 순회되므로 캐시 효율이 매우 좋음
    
    float x = transform.position.x;
    float y = transform.position.y;
    float z = transform.position.z;
    
    // 월드 행렬 계산 등
}
```

### 2. TransformComponent 순회 (수정 가능)

```cpp
// 모든 Transform 컴포넌트를 수정하며 순회
for (auto& [entityId, transform] : world.GetComponents<TransformComponent>())
{
    // transform은 TransformComponent&
    transform.position.x += 1.0f;  // 수정 가능
    
    // 물리 업데이트 등
}
```

### 3. SkinnedMeshComponent와 TransformComponent 함께 사용

```cpp
// SkinnedMeshSystem에서 사용하는 패턴
void BuildDrawList(const World& world, std::vector<SkinnedDrawCommand>& outCommands) const
{
    outCommands.clear();
    
    // Sparse Set 기반 순회: 연속 메모리에서 효율적으로 처리
    for (const auto& [entityId, skinnedMesh] : world.GetComponents<SkinnedMeshComponent>())
    {
        if (!skinnedMesh.boneMatrices || skinnedMesh.boneCount == 0)
            continue;
        
        // 다른 컴포넌트도 함께 조회
        const TransformComponent* transform = world.GetComponent<TransformComponent>(entityId);
        if (!transform)
            continue;
        
        // 렌더링 명령 생성
        SkinnedDrawCommand cmd = {};
        cmd.world = CalculateWorldMatrix(*transform);
        cmd.bones = skinnedMesh.boneMatrices;
        // ...
        
        outCommands.push_back(cmd);
    }
}
```

### 4. 빈 컨테이너 확인

```cpp
auto transforms = world.GetComponents<TransformComponent>();
if (transforms.empty())
{
    // Transform이 없는 경우 처리
    return;
}

std::size_t count = transforms.size();
```

### 5. 컴포넌트 추가/제거

```cpp
// 컴포넌트 추가
EntityId entity = world.CreateEntity();
auto& transform = world.AddComponent<TransformComponent>(entity);
transform.SetPosition(0.0f, 0.0f, 0.0f);

// 컴포넌트 가져오기
TransformComponent* t = world.GetComponent<TransformComponent>(entity);
if (t)
{
    t->position.x = 10.0f;
}

// 컴포넌트 제거 (O(1) 시간 복잡도)
world.RemoveComponent<TransformComponent>(entity);
```

## 성능 최적화 포인트

1. **캐시 효율**: 모든 TransformComponent가 연속 메모리에 저장되어 순회 시 캐시 미스 최소화
2. **해시맵 조회 제거**: 순회 시 해시맵 조회 없이 직접 접근
3. **Swap-and-Pop**: 삭제 시 마지막 요소를 삭제 위치로 이동하여 O(1) 보장

## 주의사항

- 순회 중 컴포넌트를 삭제해도 안전하지만, 현재 순회 중인 요소는 건너뛰어질 수 있음
- 스크립트 컴포넌트는 여전히 기존 방식으로 관리됨 (GetComponents()로 전체 순회 불가)
