#ifndef MIN_HEAP_H
#define MIN_HEAP_H

#include <stdint.h>

#define MAX_NODES 30
#define MAX_EDGES 60

struct HeapItem {
  int16_t  nodeIndex;
  uint32_t fScore;
};

#define MIN_HEAP_CAPACITY (MAX_NODES + MAX_EDGES * 2)

class MinHeap {
  public:
    MinHeap();

    void clear();
    bool isEmpty() const;
    void push(int16_t nodeIndex, uint32_t fScore);
    HeapItem popMin();

  private:
    HeapItem items[MIN_HEAP_CAPACITY];
    uint16_t size;

    void swapItems(uint16_t i, uint16_t j);
    static uint16_t parentOf(uint16_t i) { return (uint16_t)((i - 1u) / 2u); }
    static uint16_t leftOf(uint16_t i)   { return (uint16_t)((2u * i) + 1u); }
    static uint16_t rightOf(uint16_t i)  { return (uint16_t)((2u * i) + 2u); }
};

#endif
