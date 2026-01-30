#pragma once

#include <vector>
#include <limits>
#include <cstdint>
#include <type_traits>
#include <algorithm>
#include <typeindex>

#include "Core/Entity.h"

namespace Alice
{
    // ==== Sparse Set 기반 컴포넌트 저장소 ====
    
    /// "없음"을 나타내는 상수 (SIZE_MAX)
    constexpr std::size_t NULL_INDEX = std::numeric_limits<std::size_t>::max();
    
    /// 컴포넌트 핸들 (Index + Generation)
    template <typename T>
    struct ComponentHandle
    {
        std::size_t index = 0;
        std::uint32_t generation = 0;
        
        bool IsValid() const { return index != NULL_INDEX; }
        static ComponentHandle<T> Invalid() { return { NULL_INDEX, 0 }; }
    };

    // ==== 저장소 추상화 인터페이스 ====
    // 컴포넌트 타입에 관계없이 저장소를 통일된 방식으로 관리하기 위한 베이스 클래스
    class IStorageBase
    {
    public:
        virtual ~IStorageBase() = default;
        
        // 특정 엔티티의 컴포넌트 제거
        virtual bool Remove(EntityId id) = 0;
        
        // 모든 컴포넌트가 제거되었는지 확인
        virtual bool Empty() const = 0;
        
        // 컴포넌트 개수
        virtual std::size_t Size() const = 0;
        
        // 모든 데이터 클리어
        virtual void Clear() = 0;
        
        // 타입 정보 가져오기
        virtual std::type_index GetTypeIndex() const = 0;
        
        // 컴포넌트 존재 여부 확인
        virtual bool Has(EntityId id) const = 0;
    };

    /// Sparse Set 기반 컴포넌트 저장소
    /// - Dense array: 실제 컴포넌트들이 연속적으로 저장
    /// - Sparse array: EntityId를 인덱스로 사용하는 벡터 (값: Dense 인덱스)
    /// - Swap-and-pop: O(1) 삭제 지원
    /// 
    /// 성능 최적화:
    /// - 해싱 없이 배열 인덱스로 즉시 접근 (O(1))
    /// - 연속 메모리로 캐시 효율 극대화
    template <typename T>
    class ComponentStorage : public IStorageBase
    {
    public:
        // ==== 1. 데이터 관리 (Sparse Set) ====

        /// 컴포넌트 추가/갱신 (복사)
        T& Add(EntityId id, const T& component)
        {
            return Add(id, T(component));
        }

        /// 컴포넌트 추가/갱신 (이동)
        T& Add(EntityId id, T&& component)
        {
            // Sparse 배열이 작으면 ID에 맞춰 늘려줍니다.
            if (id >= m_sparse.size()) {
                m_sparse.resize(id + 1, NULL_INDEX);
            }

            // 이미 있다면 덮어쓰기
            if (m_sparse[id] != NULL_INDEX) {
                std::size_t idx = m_sparse[id];
                m_dense[idx] = std::move(component);
                return m_dense[idx];
            }

            // 새로 추가
            std::size_t idx = m_dense.size();
            m_sparse[id] = idx;
            m_dense.push_back(std::move(component));
            m_entityIds.push_back(id);
            m_generations.push_back(0); // 세대는 0부터 시작

            return m_dense.back();
        }

        /// 컴포넌트 가져오기 (없으면 nullptr)
        T* Get(EntityId id)
        {
            if (id >= m_sparse.size() || m_sparse[id] == NULL_INDEX)
                return nullptr;
            return &m_dense[m_sparse[id]];
        }

        /// const 버전
        const T* Get(EntityId id) const
        {
            if (id >= m_sparse.size() || m_sparse[id] == NULL_INDEX)
                return nullptr;
            return &m_dense[m_sparse[id]];
        }

        /// 컴포넌트 제거 (Swap-and-Pop 방식으로 O(1))
        // IStorageBase 인터페이스 구현
        bool Remove(EntityId id) override
        {
            if (id >= m_sparse.size() || m_sparse[id] == NULL_INDEX)
                return false;

            std::size_t removedIdx = m_sparse[id];
            std::size_t lastIdx = m_dense.size() - 1;

            if (removedIdx != lastIdx)
            {
                // Swap & Pop: 마지막 요소를 삭제된 자리로 이동
                EntityId lastEntity = m_entityIds[lastIdx];
                
                m_dense[removedIdx] = std::move(m_dense[lastIdx]);
                m_entityIds[removedIdx] = lastEntity;
                m_generations[removedIdx] = m_generations[lastIdx];

                // 이사 온 요소의 Sparse 정보 갱신
                m_sparse[lastEntity] = removedIdx;
            }

            // 마지막 데이터 삭제 및 Sparse 정보 초기화
            m_dense.pop_back();
            m_entityIds.pop_back();
            m_generations.pop_back();
            m_sparse[id] = NULL_INDEX;

            return true;
        }

        /// 모든 컴포넌트가 제거되었는지 확인
        // IStorageBase 인터페이스 구현
        bool Empty() const override { return m_dense.empty(); }

        /// 컴포넌트 개수
        // IStorageBase 인터페이스 구현
        std::size_t Size() const override { return m_dense.size(); }

        // ==== 2. View 통합 (템플릿으로 Const/Non-Const 통합) ====
        
        /// 템플릿을 사용하여 Const와 Non-Const 뷰를 하나로 통합
        template <bool IsConst>
        struct ViewIterator
        {
            // const 여부에 따라 타입 결정
            using StorageType = std::conditional_t<IsConst, const ComponentStorage, ComponentStorage>;
            using DataType    = std::conditional_t<IsConst, const T&, T&>;
            using PairType    = std::pair<EntityId, DataType>;

            StorageType* storage;
            std::size_t index;

            ViewIterator(StorageType* s, std::size_t i) : storage(s), index(i) {}

            // 반복자 필수 연산자들
            ViewIterator& operator++() { ++index; return *this; }
            bool operator!=(const ViewIterator& other) const { return index != other.index; }
            
            // 구조적 바인딩(Structured Binding)을 위한 pair 반환
            PairType operator*() const {
                return { storage->m_entityIds[index], storage->m_dense[index] };
            }

            // operator->() 지원: Proxy 객체를 반환하여 ->first, ->second 사용 가능
            // 주의: 이 구현은 매 호출마다 임시 pair를 생성하지만, 일반적으로 begin()에서만 사용되므로 문제없습니다.
            struct Proxy {
                PairType pair;
                PairType* operator->() { return &pair; }
            };
            Proxy operator->() const {
                return Proxy{ { storage->m_entityIds[index], storage->m_dense[index] } };
            }
        };

        /// 뷰 헬퍼 클래스
        // iterator 돌기 위함
        template <bool IsConst>
        struct View
        {
            using StorageType = std::conditional_t<IsConst, const ComponentStorage, ComponentStorage>;
            StorageType* storage;

            auto begin() const { return ViewIterator<IsConst>{ storage, 0 }; }
            auto end() const { return ViewIterator<IsConst>{ storage, storage->m_dense.size() }; }
            std::size_t size() const { return storage->m_dense.size(); }
            bool empty() const { return storage->m_dense.empty(); }
        };

        /// 사용자 편의 함수 (const 오버로딩으로 자동 판단)
        auto GetView() { return View<false>{ this }; }
        auto GetView() const { return View<true>{ this }; }

        /// 모든 데이터 클리어
        // IStorageBase 인터페이스 구현
        void Clear() override
        {
            m_dense.clear();
            m_entityIds.clear();
            m_generations.clear();
            // Sparse는 clear 대신 fill로 초기화 (메모리 재할당 방지)
            std::fill(m_sparse.begin(), m_sparse.end(), NULL_INDEX);
        }

        // IStorageBase 인터페이스 구현 - 타입 정보 반환
        std::type_index GetTypeIndex() const override
        {
            return std::type_index(typeid(T));
        }

        // IStorageBase 인터페이스 구현 - 컴포넌트 존재 여부 확인
        bool Has(EntityId id) const override
        {
            return (id < m_sparse.size() && m_sparse[id] != NULL_INDEX);
        }

        /// 메모리 최적화용임 사용하지 않는 Sparse 배열 공간 제거
        /// 이 메서드는 호출 시점에 가장 큰 EntityId 이후의 공간만 제거합니다.
        void ShrinkSparse()
        {
            if (m_dense.empty())
            {
                m_sparse.clear();
                return;
            }

            // 가장 큰 EntityId 찾기
            EntityId maxId = 0;
            for (EntityId id : m_entityIds)
            {
                if (id > maxId)
                    maxId = id;
            }

            // maxId + 1까지만 유지
            if (maxId + 1 < m_sparse.size())
            {
                m_sparse.resize(maxId + 1);
            }
        }

    private:
        // Sparse Array: 인덱스: EntityId, 값: Dense Index
        // NULL_INDEX는 해당 엔티티가 컴포넌트를 가지고 있지 않음을 의미
        std::vector<std::size_t> m_sparse;

        // Dense Array: 실제 컴포넌트 데이터 (연속 메모리)
        std::vector<T> m_dense;
        std::vector<EntityId> m_entityIds;                         // 각 인덱스가 어떤 엔티티에 속하는지
        std::vector<std::uint32_t> m_generations;                  // 각 컴포넌트의 generation
    };
}
