#ifndef CANITY_MAP_TOPOLOGY_H
#define CANITY_MAP_TOPOLOGY_H
typedef struct { int walkable,reachable,deadEnds,deadEndLimit; } MapTopology;
/* Pure grid functions; use the actual 36x36 gameplay and storage dimensions.
   No global map reads, RNG, rendering or I/O. */
MapTopology MapTopologyMeasure(const int* cells,int width,int height);
int MapTopologyAccepts(MapTopology value);
#endif
