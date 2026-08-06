#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <new>

template <typename T, std::size_t BlockCount>
class OrderPool {
 public:
  OrderPool()
      : pool_(static_cast<std::byte*>(::operator new(kPoolSize, std::align_val_t{kAlignment}))), freeListHead_(reinterpret_cast<void*>(pool_)) {
    static_assert(kBlockSize % kAlignment == 0, "block stride must preserve alignment");

    for (size_t i{}; i < BlockCount - 1; ++i) {
      // calc address of current block, in bytes
      std::byte* curBytes{&pool_[i * kBlockSize]};

      // get next block
      std::byte* nextBytes{&pool_[(i + 1) * kBlockSize]};

      // tell compiler to treat a group of bytes as a node
      Node* curNode{reinterpret_cast<Node*>(curBytes)};

      // tell compiler to treat the next group of bytes as a node as well
      Node* next{reinterpret_cast<Node*>(nextBytes)};

      // write the address of next inside the first block of nodes
      curNode->next = next;
    }

    // for final byte, write nullptr inside so we know its the end
    std::byte* lastBytes{&pool_[(BlockCount - 1) * kBlockSize]};
    Node* lastNode{reinterpret_cast<Node*>(lastBytes)};

    lastNode->next = nullptr;
  }

  ~OrderPool() {
    freeListHead_ = nullptr;
    // delete[] on aligned - operator new storage is undefined behaviour
    ::operator delete(pool_, kPoolSize, std::align_val_t{kAlignment});
  }

  OrderPool(OrderPool const& other) = delete;
  OrderPool& operator=(OrderPool const& rhs) = delete;
  OrderPool(OrderPool&& other) = delete;
  OrderPool& operator=(OrderPool&& rhs) = delete;

  T* allocate() {
    // if we try to allocate when we're empty, throw
    if (freeListHead_ == nullptr) {
      throw std::bad_alloc();
    }

    // get next node to assign as new head
    Node* head{reinterpret_cast<Node*>(freeListHead_)};
    freeListHead_ = head->next;

    return reinterpret_cast<T*>(head);
  }

  void deallocate(T* ptr) {
    // though this function is called deallocate, the goal is to reinsert the block back into
    // the memory pool can't really explore more options to protect against a double dealloc
    // without compromising efficiency
    Node* newHead{reinterpret_cast<Node*>(ptr)};
    newHead->next = reinterpret_cast<Node*>(freeListHead_);
    freeListHead_ = newHead;
  }

  std::size_t inUse() {
    ;
    ;
  }

 private:
  struct Node {
    Node* next{};
  };

  // A block must satisfy BOTH T's alignment and Node's (free blocks hold a Node*).
  static constexpr std::size_t kAlignment{std::max(alignof(T), alignof(Node))};

  // Widen for types smaller than a pointer, then round the stride up to kAlignment
  // so every block lands on an aligned address.
  static constexpr std::size_t kMinBlock{std::max(sizeof(T), sizeof(Node))};
  static constexpr std::size_t kBlockSize{(kMinBlock + kAlignment - 1) / kAlignment * kAlignment};

  static constexpr std::size_t kPoolSize{BlockCount * kBlockSize};

  std::byte* pool_{};
  void* freeListHead_{};
};
