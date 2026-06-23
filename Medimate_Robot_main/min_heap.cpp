#include "min_heap.h"

MinHeap::MinHeap() {
  size = 0u;
}

void MinHeap::clear() {
  size = 0u;
}

bool MinHeap::isEmpty() const {
  return size == 0u;
}

void MinHeap::swapItems(uint16_t i, uint16_t j) {
  HeapItem tmp = items[i];
  items[i] = items[j];
  items[j] = tmp;
}

void MinHeap::push(int16_t nodeIndex, uint32_t fScore) {
  if (size >= MIN_HEAP_CAPACITY) {
    return;
  }

  uint16_t i = size;
  size++;
  items[i].nodeIndex = nodeIndex;
  items[i].fScore = fScore;

  while (i > 0u) {
    uint16_t parent = parentOf(i);
    if (items[parent].fScore <= items[i].fScore) {
      break;
    }
    swapItems(i, parent);
    i = parent;
  }
}

HeapItem MinHeap::popMin() {
  HeapItem top = items[0];
  size--;
  items[0] = items[size];

  uint16_t i = 0u;
  for (;;) {
    uint16_t left     = leftOf(i);
    uint16_t right    = rightOf(i);
    uint16_t smallest  = i;

    if ((left < size) && (items[left].fScore < items[smallest].fScore)) {
      smallest = left;
    }
    if ((right < size) && (items[right].fScore < items[smallest].fScore)) {
      smallest = right;
    }
    if (smallest == i) {
      break;
    }
    swapItems(i, smallest);
    i = smallest;
  }

  return top;
}
