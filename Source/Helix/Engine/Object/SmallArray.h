#pragma once

#include <Helix/Core/Basics/Assert.h>
#include <Helix/Core/Memory/Allocator.h>

#include <cstring>
#include <type_traits>

// An array of trivially copyable items that keeps the first N inside the object and moves to the heap only beyond
// them: most GameObjects have a handful of components and children, so they never allocate for their lists.
namespace helix::engine
{
    template <class T, u32 N>
    class SmallArray
    {
        static_assert(std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>);

    public:
        SmallArray() = default;
        SmallArray(const SmallArray& o) { Append(o.Data(), o.size_); }
        SmallArray& operator=(const SmallArray& o)
        {
            if (this != &o)
            {
                size_ = 0;
                Append(o.Data(), o.size_);
            }
            return *this;
        }
        ~SmallArray() { Release(); }

        u32 Size() const { return size_; }
        bool Empty() const { return size_ == 0; }
        T* Data() { return heap_ ? heap_ : reinterpret_cast<T*>(inline_); }
        const T* Data() const { return heap_ ? heap_ : reinterpret_cast<const T*>(inline_); }
        T* begin() { return Data(); }
        T* end() { return Data() + size_; }
        const T* begin() const { return Data(); }
        const T* end() const { return Data() + size_; }
        T& operator[](u32 i)
        {
            HX_ASSERT(i < size_);
            return Data()[i];
        }
        const T& operator[](u32 i) const
        {
            HX_ASSERT(i < size_);
            return Data()[i];
        }
        T& Back() { return (*this)[size_ - 1]; }

        void Push(const T& value)
        {
            Grow(size_ + 1);
            Data()[size_++] = value;
        }
        void Insert(u32 index, const T& value)
        {
            HX_ASSERT(index <= size_);
            Grow(size_ + 1);
            T* d = Data();
            std::memmove(d + index + 1, d + index, (size_ - index) * sizeof(T));
            d[index] = value;
            size_++;
        }
        // Keeps the order.
        void RemoveAt(u32 index)
        {
            HX_ASSERT(index < size_);
            T* d = Data();
            std::memmove(d + index, d + index + 1, (size_ - index - 1) * sizeof(T));
            size_--;
        }
        void Append(const T* values, u32 count)
        {
            Grow(size_ + count);
            if (count)
                std::memcpy(Data() + size_, values, count * sizeof(T));
            size_ += count;
        }
        void Clear() { size_ = 0; }

    private:
        void Grow(u32 needed)
        {
            if (needed <= capacity_)
                return;
            u32 capacity = capacity_ * 2 > needed ? capacity_ * 2 : needed;
            T* memory = static_cast<T*>(HeapAllocator().Allocate(usize(capacity) * sizeof(T), alignof(T)));
            if (size_)
                std::memcpy(memory, Data(), usize(size_) * sizeof(T));
            Release();
            heap_ = memory;
            capacity_ = capacity;
        }
        void Release()
        {
            if (heap_)
                HeapAllocator().Free(heap_, usize(capacity_) * sizeof(T), alignof(T));
            heap_ = nullptr;
            capacity_ = N;
        }

        alignas(T) unsigned char inline_[sizeof(T) * N];
        T* heap_ = nullptr;
        u32 size_ = 0, capacity_ = N;
    };
}
