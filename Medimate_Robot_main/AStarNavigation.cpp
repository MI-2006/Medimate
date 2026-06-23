#include "AStarNavigation.h"
#include <min_heap.h>
#include <stdint.h>

static const uint32_t A_STAR_NO_PATH = 0xFFFFFFFFUL;
static const int16_t  NO_PARENT      = -1;

static int16_t findNodeIndex(const GraphNode nodes[], uint8_t nodeCount, uint8_t id) {
  for (uint8_t i = 0u; i < nodeCount; i++) {
    if (nodes[i].id == id) {
      return (int16_t)i;
    }
  }
  return -1;
}

static uint32_t absDiff16(int16_t a, int16_t b) {
  int32_t d = (int32_t)a - (int32_t)b;
  return (d < 0) ? (uint32_t)(-d) : (uint32_t)d;
}

static uint32_t heuristic(const GraphNode &a, const GraphNode &b) {
  return absDiff16(a.x, b.x) + absDiff16(a.y, b.y);
}

bool findPathAStar(const GraphNode nodes[], uint8_t nodeCount,
                    const GraphEdge edges[], uint8_t edgeCount,
                    uint8_t startNodeId, uint8_t targetNodeId,
                    AStarResult &resultOut) {
  resultOut.found      = false;
  resultOut.pathLength = 0u;
  resultOut.totalCost  = 0u;

  int16_t startIdx  = findNodeIndex(nodes, nodeCount, startNodeId);
  int16_t targetIdx = findNodeIndex(nodes, nodeCount, targetNodeId);

  if ((startIdx < 0) || (targetIdx < 0)) {
    return false;
  }
  if (nodes[startIdx].blocked || nodes[targetIdx].blocked) {
    return false;
  }

  uint32_t gScore[MAX_NODES];
  int16_t  parentIdx[MAX_NODES];
  bool     closedSet[MAX_NODES];

  for (uint8_t i = 0u; i < nodeCount; i++) {
    gScore[i]    = A_STAR_NO_PATH;
    parentIdx[i] = NO_PARENT;
    closedSet[i] = false;
  }
  gScore[startIdx] = 0u;

  MinHeap openSet;
  openSet.push(startIdx, heuristic(nodes[startIdx], nodes[targetIdx]));

  while (!openSet.isEmpty()) {
    HeapItem top     = openSet.popMin();
    int16_t  current = top.nodeIndex;

    if (closedSet[current]) {
      continue;
    }

    uint32_t currentBestF = gScore[current] + heuristic(nodes[current], nodes[targetIdx]);
    if (top.fScore > currentBestF) {
      continue;
    }

    closedSet[current] = true;

    if (current == targetIdx) {
      break;
    }

    for (uint8_t e = 0u; e < edgeCount; e++) {
      if (edges[e].blocked) {
        continue;
      }

      int16_t neighborIdx = -1;
      if (edges[e].from == nodes[current].id) {
        neighborIdx = findNodeIndex(nodes, nodeCount, edges[e].to);
      } else if (edges[e].to == nodes[current].id) {
        neighborIdx = findNodeIndex(nodes, nodeCount, edges[e].from);
      } else {
        continue;
      }

      if ((neighborIdx < 0) || closedSet[neighborIdx] || nodes[neighborIdx].blocked) {
        continue;
      }

      uint32_t tentativeG = gScore[current] + edges[e].weight;
      if (tentativeG < gScore[neighborIdx]) {
        gScore[neighborIdx]    = tentativeG;
        parentIdx[neighborIdx] = current;
        uint32_t neighborF = tentativeG + heuristic(nodes[neighborIdx], nodes[targetIdx]);
        openSet.push(neighborIdx, neighborF);
      }
    }
  }

  if (gScore[targetIdx] == A_STAR_NO_PATH) {
    return false;
  }

  int16_t tempPath[MAX_NODES];
  uint8_t count = 0u;
  int16_t cur = targetIdx;
  while ((cur != NO_PARENT) && (count < MAX_NODES)) {
    tempPath[count] = cur;
    count++;
    cur = parentIdx[cur];
  }

  resultOut.pathLength = count;
  for (uint8_t i = 0u; i < count; i++) {
    resultOut.path[i] = nodes[tempPath[count - 1u - i]].id;
  }
  resultOut.totalCost = gScore[targetIdx];
  resultOut.found      = true;
  return true;
}
