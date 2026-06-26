#pragma once

#include <vector>
#include <limits>
#include <cstdint>
#include <type_traits>
#include <algorithm>
#include <typeindex>

#include "Runtime/ECS/Entity.h"

namespace Alice
{
    // ==== Sparse Set 기반 컴포넌트 저장소 ====

    /// "없음"을 나타내는 상수 (SIZE_MAX)
    constexpr std::size_t NULL_INDEX = std::numeric_limits<std::size_t>::max();

    // ==== 저장소 추상화 인터페이스 ====
    class IStorageBase
    {
    public:
        virtual ~IStorageBase() = default;
        virtual bool Remove(EntityId id) = 0;
        virtual bool Empty() const = 0;
        virtual std::size_t Size() const = 0;
        virtual void Clear() = 0;
        virtual std::type_index GetTypeIndex() const = 0;
        virtual bool Has(EntityId id) const = 0;
    };

    /// Sparse Set 기반 컴포넌트 저장소
    /// Dense array + Sparse array, Swap-and-pop O(1) 삭제
    template <typename T>
    class ComponentStorage : public IStorageBase
    {
    public:
        T& Add(EntityId id, const T& component) { return Add(id, T(component)); }

        T& Add(EntityId id, T&& component)
        {
            if (id >= m_sparse.size())
                m_sparse.resize(id + 1, NULL_INDEX);

            if (m_sparse[id] != NULL_INDEX)
            {
                std::size_t idx = m_sparse[id];
                m_dense[idx] = std::move(component);
                return m_dense[idx];
            }

            std::size_t idx = m_dense.size();
            m_sparse[id] = idx;
            m_dense.push_back(std::move(component));
            m_entityIds.push_back(id);
            return m_dense.back();
        }

        T* Get(EntityId id)
        {
            if (id >= m_sparse.size() || m_sparse[id] == NULL_INDEX)
                return nullptr;
            return &m_dense[m_sparse[id]];
        }

        const T* Get(EntityId id) const
        {
            if (id >= m_sparse.size() || m_sparse[id] == NULL_INDEX)
                return nullptr;
            return &m_dense[m_sparse[id]];
        }

        bool Remove(EntityId id) override
        {
            if (id >= m_sparse.size() || m_sparse[id] == NULL_INDEX)
                return false;

            std::size_t removedIdx = m_sparse[id];
            std::size_t lastIdx = m_dense.size() - 1;

            if (removedIdx != lastIdx)
            {
                EntityId lastEntity = m_entityIds[lastIdx];
                m_dense[removedIdx] = std::move(m_dense[lastIdx]);
                m_entityIds[removedIdx] = lastEntity;
                m_sparse[lastEntity] = removedIdx;
            }

            m_dense.pop_back();
            m_entityIds.pop_back();
            m_sparse[id] = NULL_INDEX;
            return true;
        }

        bool Empty() const override { return m_dense.empty(); }
        std::size_t Size() const override { return m_dense.size(); }

        // ==== View (Const/Non-Const 통합) ====
        template <bool IsConst>
        struct ViewIterator
        {
            using StorageType = std::conditional_t<IsConst, const ComponentStorage, ComponentStorage>;
            using DataType    = std::conditional_t<IsConst, const T&, T&>;
            using PairType    = std::pair<EntityId, DataType>;

            StorageType* storage;
            std::size_t index;

            ViewIterator(StorageType* s, std::size_t i) : storage(s), index(i) {}

            ViewIterator& operator++() { ++index; return *this; }
            bool operator!=(const ViewIterator& other) const { return index != other.index; }

            PairType operator*() const {
                return { storage->m_entityIds[index], storage->m_dense[index] };
            }

            struct Proxy {
                PairType pair;
                PairType* operator->() { return &pair; }
            };
            Proxy operator->() const {
                return Proxy{ { storage->m_entityIds[index], storage->m_dense[index] } };
            }
        };

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

        auto GetView() { return View<false>{ this }; }
        auto GetView() const { return View<true>{ this }; }

        void Clear() override
        {
            m_dense.clear();
            m_entityIds.clear();
            std::fill(m_sparse.begin(), m_sparse.end(), NULL_INDEX);
        }

        std::type_index GetTypeIndex() const override { return std::type_index(typeid(T)); }
        bool Has(EntityId id) const override { return id < m_sparse.size() && m_sparse[id] != NULL_INDEX; }

    private:
        std::vector<std::size_t> m_sparse;
        std::vector<T>          m_dense;
        std::vector<EntityId>   m_entityIds;
    };
}
