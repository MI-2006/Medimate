#ifndef ASTAR_NAVIGATION_H
#define ASTAR_NAVIGATION_H

#include <stdint.h>
#include <min_heap.h>

struct GraphNode {
  uint8_t id;
  int16_t x;
  int16_t y;
  bool    blocked;
};

struct GraphEdge {
  uint8_t  id;
  uint8_t  from;
  uint8_t  to;
  uint16_t weight;
  bool     blocked;
};

struct AStarResult {
  bool     found;
  uint8_t  path[MAX_NODES];
  uint8_t  pathLength;
  uint32_t totalCost;
};

bool findPathAStar(const GraphNode nodes[], uint8_t nodeCount,const GraphEdge edges[], uint8_t edgeCount,uint8_t startNodeId, uint8_t targetNodeId,AStarResult &resultOut);

#endif
