/*
 * pinchGraphManipulation.h
 *
 *  Created on: 11 Sep 2010
 *      Author: benedictpaten
 */

#ifndef PINCHGRAPHMANIPULATION_H_
#define PINCHGRAPHMANIPULATION_H_

////////////////////////////////////////////////
////////////////////////////////////////////////
////////////////////////////////////////////////
//Methods from pinchGraphManipulation.c
////////////////////////////////////////////////
////////////////////////////////////////////////
////////////////////////////////////////////////

void removeOverAlignedEdges(struct PinchGraph *pinchGraph, float minimumTreeCoverage, int32_t maxDegree, struct List *extraEdgesToUndo,
        int32_t extensionSteps, Flower *flower);

struct List *getRecursiveComponents(struct PinchGraph *pinchGraph, int32_t (*excludedEdgesFn)(void *));

struct List *getRecursiveComponents2(struct PinchGraph *pinchGraph, struct List *edgesToExclude);

/*
 * Ensure the graph is one connected component by linking components not part of the sink component
 * by there dead stub ends.
 */
void linkStubComponentsToTheSinkComponent(struct PinchGraph *pinchGraph, Flower *flower, int32_t attachEnds);

/*
 * Disconnects the dead ends of free stub ends from the sink component.
 */
void unlinkStubComponentsFromTheSinkComponent(struct PinchGraph *pinchGraph, Flower *flower);

void removeTrivialGreyEdgeComponents(struct PinchGraph *graph, struct List *listOfVertices, Flower *flower);

float treeCoverage(struct PinchVertex *vertex, Flower *flower);

#endif /* PINCHGRAPHMANIPULATION_H_ */
