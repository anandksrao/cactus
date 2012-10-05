/*
 * Copyright (C) 2009-2011 by Benedict Paten (benedictpaten@gmail.com)
 *
 * Released under the MIT license, see LICENSE.txt
 */

#include "multipleAligner.h"
#include "sonLib.h"
#include "stPosetAlignment.h"
#include "pairwiseAligner.h"
#include <stdlib.h>
#include <math.h>

/*
 * Functions to align a bunch of sequences, creating a global alignment.
 */

/*
 * Constructs a random spanning tree linking all the nodes in items into one component.
 */
void constructSpanningTree(int32_t numberOfSequences,
        stSortedSet *pairwiseAlignments) {
    stList *list = stList_construct3(0, (void(*)(void *)) stIntTuple_destruct);
    for (int32_t i = 0; i < numberOfSequences; i++) {
        stList_append(list, stIntTuple_construct(1, i));
    }
    while (stList_length(list) > 1) {
        stIntTuple *i = st_randomChoice(list);
        stList_removeItem(list, i);
        int32_t j = stIntTuple_getPosition(i, 0);
        stIntTuple_destruct(i);
        stIntTuple *k = st_randomChoice(list);
        int32_t l = stIntTuple_getPosition(k, 0);
        assert(l != j);
        stIntTuple *m = j < l ? stIntTuple_construct(2, j, l)
                : stIntTuple_construct(2, l, j);
        if (stSortedSet_search(pairwiseAlignments, m) == NULL) {
            stSortedSet_insert(pairwiseAlignments, m);
        } else {
            stIntTuple_destruct(m);
        }
    }
    stList_destruct(list);
    assert(stSortedSet_size(pairwiseAlignments) >= numberOfSequences / 2);
}

/*
 * Construct a random set of pairs.
 */
void constructRandomAlignments(int32_t numberOfSequences,
        stSortedSet *pairwiseAlignments, int32_t multiple) {
    if (numberOfSequences <= 1) {
        return;
    }
    int32_t maxPairs = (numberOfSequences * numberOfSequences
            - numberOfSequences) / 2;
    int32_t maxComparisons = numberOfSequences * multiple;
    double acceptProb = maxComparisons / (double) maxPairs;
    assert(acceptProb > 0);
    maxComparisons = maxComparisons > maxPairs ? maxPairs : maxComparisons;
    while (1) {
        for (int32_t i = 0; i < numberOfSequences; i++) {
            for (int32_t j = i + 1; j < numberOfSequences; j++) {
                if (st_random() <= acceptProb) {
                    if (stSortedSet_size(pairwiseAlignments) >= maxComparisons) {
                        return;
                    }
                    stIntTuple *m = stIntTuple_construct(2, i, j);
                    if (stSortedSet_search(pairwiseAlignments, m) == NULL) {
                        stSortedSet_insert(pairwiseAlignments, m);
                    } else {
                        stIntTuple_destruct(m);
                    }
                }
            }
        }
    }
}

void addPairwiseAlignment(int32_t i, int32_t j, stSortedSet *pairwiseAlignments) {
    assert(i != j);
    if(i > j) {
        addPairwiseAlignment(j, i, pairwiseAlignments);
        return;
    }
    stIntTuple *m = stIntTuple_construct(2, i, j);
    if (stSortedSet_search(pairwiseAlignments, m) == NULL) {
        stSortedSet_insert(pairwiseAlignments, m);
    } else {
        stIntTuple_destruct(m);
    }
}

void addPairwiseAlignments(int32_t numberOfSequences, int32_t offset, stSortedSet *pairwiseAlignments) {
    if(offset < 1) {
        return;
    }
    assert(numberOfSequences > 1);
    for(int32_t i=0; i<numberOfSequences; i++) {
        addPairwiseAlignment(i, (i + offset) % numberOfSequences, pairwiseAlignments);
    }
}

void constructCyclicPermuations(int32_t numberOfSequences,
        stSortedSet *pairwiseAlignments, int32_t multiple) {
    if (numberOfSequences <= 1 || multiple == 0) {
        return;
    }
    int32_t i = numberOfSequences/2;
    addPairwiseAlignments(numberOfSequences, 1, pairwiseAlignments);
    double increment = i/(double)multiple;
    double offset = increment*2;
    while(((int32_t)offset) <= i) { //The cast ensures we do reach the half way point
        addPairwiseAlignments(numberOfSequences, offset, pairwiseAlignments);
        offset += increment;
    }
}

/*int32_t *calculateIndelProbs(stList *alignedPairs, int32_t sequenceLength,
 int32_t sequenceIndex) {
 int32_t *indelProbs = st_malloc(sizeof(int32_t) * sequenceLength);
 for (int32_t i = 0; i < sequenceLength; i++) {
 indelProbs[i] = PAIR_ALIGNMENT_PROB_1;
 }
 for (int32_t i = 0; i < stList_length(alignedPairs); i++) {
 stIntTuple *alignedPair = stList_get(alignedPairs, i);
 int32_t j = stIntTuple_getPosition(alignedPair, sequenceIndex + 1);
 assert(j >= 0);
 assert(j < sequenceLength);
 int32_t score = stIntTuple_getPosition(alignedPair, 0);
 assert(score >= 0);
 assert(score <= PAIR_ALIGNMENT_PROB_1);
 indelProbs[j] -= score;
 if (indelProbs[j] < 0) {
 indelProbs[j] = 0;
 }
 }
 return indelProbs;
 }*/

static double getAlignmentScore(stList *alignedPairs, int32_t seqLength1,
        int32_t seqLength2) {
    int64_t alignmentScore = 0;
    for (int32_t i = 0; i < stList_length(alignedPairs); i++) {
        stIntTuple *alignedPair = stList_get(alignedPairs, i);
        assert(stIntTuple_length(alignedPair) == 3);
        alignmentScore += stIntTuple_getPosition(alignedPair, 0);
    }
    int64_t j = seqLength1 < seqLength2 ? seqLength1 : seqLength2;
    j = j == 0 ? 1 : j;
    double d = (double) alignmentScore / (j * PAIR_ALIGNMENT_PROB_1);
    d = d > 1.0 ? 1.0 : d;
    d = d < 0.0 ? 0.0 : d;
    return d;
}

static int cmpFn(int32_t i, int32_t j) {
    return i > j ? 1 : (i < j ? -1 : 0);
}

typedef struct _pairwiseColumnWeightShared {
    double weight; //this is mirrored
    double alignmentScore;
    stSortedSet *alignedPairs; //this is mirrored
} PairwiseColumnWeightShared;

typedef struct _pairwiseColumnWeight {
    /*
     * Structure to represent am aggregate weight between two columns in the multiple alignment.
     */
    int32_t sequence;
    int32_t position;
    int32_t columnDepth;
    PairwiseColumnWeightShared *shared;
    struct _pairwiseColumnWeight *reverse;
} PairwiseColumnWeight;

static PairwiseColumnWeight *pairwiseColumnWeight_constructEmpty() {
    PairwiseColumnWeight *i = st_malloc(sizeof(PairwiseColumnWeight));
    PairwiseColumnWeightShared *j = st_malloc(
            sizeof(PairwiseColumnWeightShared));
    i->shared = j; //hook up
    i->reverse = st_malloc(sizeof(PairwiseColumnWeight)); //link the forward and reverse
    i->reverse->reverse = i;
    i->reverse->shared = j;
    return i;
}

void pairwiseColumnWeight_fillOut(PairwiseColumnWeight *i, int32_t sequence1,
        int32_t position1, int32_t columnDepth1, int32_t sequence2,
        int32_t position2, int32_t columnDepth2, double weight,
        double alignmentScore, stSortedSet *alignedPairs) {
    assert(columnDepth1 > 0);
    assert(columnDepth2 > 0);
    assert(stSortedSet_size(alignedPairs) > 0);
    if(sequence1 == sequence2) {
        assert(position1 != position2);
    }

    i->sequence = sequence1;
    i->position = position1;
    i->columnDepth = columnDepth1;

    PairwiseColumnWeightShared *j = i->shared;
    j->weight = weight;
    j->alignmentScore = alignmentScore;
    j->alignedPairs = alignedPairs;

    i->reverse->sequence = sequence2;
    i->reverse->position = position2;
    i->reverse->columnDepth = columnDepth2;
}

static PairwiseColumnWeight *pairwiseColumnWeight_construct(int32_t sequence1,
        int32_t position1, int32_t columnDepth1, int32_t sequence2,
        int32_t position2, int32_t columnDepth2, double weight,
        double alignmentScore, stSortedSet *alignedPairs) {
    PairwiseColumnWeight *i = pairwiseColumnWeight_constructEmpty();
    pairwiseColumnWeight_fillOut(i, sequence1, position1, columnDepth1,
            sequence2, position2, columnDepth2, weight, alignmentScore,
            alignedPairs);
    return i;
}

static void pairwiseColumnWeight_destruct(
        PairwiseColumnWeight *pairwiseColumnWeight) {
    assert(pairwiseColumnWeight->shared->alignedPairs != NULL);
    stSortedSet_destruct(pairwiseColumnWeight->shared->alignedPairs);
    free(pairwiseColumnWeight->shared);
    free(pairwiseColumnWeight->reverse);
    free(pairwiseColumnWeight);
}

static int pairwiseColumnWeight_compareByPositionFn(
        const PairwiseColumnWeight *a, const PairwiseColumnWeight *b) {
    /*
     * Compare the column weights by their sequence positions, first forward position, then reverse.
     */
    int i = cmpFn(a->sequence, b->sequence);
    if (i == 0) {
        i = cmpFn(a->position, b->position);
        if (i == 0) {
            i = cmpFn(a->reverse->sequence, b->reverse->sequence);
            if (i == 0) {
                i = cmpFn(a->reverse->position, b->reverse->position);
            }
        }
    }
    return i;
}

static int getPairNumber(const PairwiseColumnWeight *a,
        bool divideByObservedPairs) {
    return divideByObservedPairs ? stSortedSet_size(a->shared->alignedPairs)
            : (a->columnDepth * a->reverse->columnDepth);
}

static double pairwiseColumnWeight_getNormalisedWeight(
        const PairwiseColumnWeight *a, bool divideByObservedPairs) {
    return a->shared->weight / getPairNumber(a, divideByObservedPairs);
}

static double pairwiseColumnWeight_getNormalisedAlignmentScore(
        const PairwiseColumnWeight *a, bool divideByObservedPairs) {
    return a->shared->alignmentScore / getPairNumber(a, divideByObservedPairs);
}

static int pairwiseColumnWeight_compareByWeightFnP(
        const PairwiseColumnWeight *a, const PairwiseColumnWeight *b,
        bool divideByObservedPairs) {
    double d = pairwiseColumnWeight_getNormalisedWeight(a,
            divideByObservedPairs);
    double e = pairwiseColumnWeight_getNormalisedWeight(b,
            divideByObservedPairs);
    if (d != e) {
        return d > e ? 1 : -1;
    }
    d = pairwiseColumnWeight_getNormalisedAlignmentScore(a,
            divideByObservedPairs);
    e = pairwiseColumnWeight_getNormalisedAlignmentScore(b,
            divideByObservedPairs);
    return d > e ? 1 : (d < e ? -1 : pairwiseColumnWeight_compareByPositionFn(
            a, b));
}

static int pairwiseColumnWeight_compareByWeightFn_divideByAllPairs(
        const PairwiseColumnWeight *a, const PairwiseColumnWeight *b) {
    return pairwiseColumnWeight_compareByWeightFnP(a, b, 0);
}

static int pairwiseColumnWeight_compareByWeightFn_divideByObservedPairs(
        const PairwiseColumnWeight *a, const PairwiseColumnWeight *b) {
    return pairwiseColumnWeight_compareByWeightFnP(a, b, 1);
}

static PairwiseColumnWeight *pairwiseColumnWeight_merge(
        const PairwiseColumnWeight *a, const PairwiseColumnWeight *b) {
    assert(a != b);
    assert(a != b->reverse);
    assert(a->sequence == b->sequence);
    assert(a->position == b->position);
    assert(a->reverse->sequence == b->reverse->sequence);
    assert(a->reverse->position == b->reverse->position);
    assert(a->reverse->columnDepth == b->reverse->columnDepth);

    stSortedSet *alignedPairs = stSortedSet_getUnion(a->shared->alignedPairs,
            b->shared->alignedPairs);
    assert(stSortedSet_size(alignedPairs) == stSortedSet_size(a->shared->alignedPairs) + stSortedSet_size(b->shared->alignedPairs));
    return pairwiseColumnWeight_construct(a->sequence, a->position,
            a->columnDepth + b->columnDepth, a->reverse->sequence,
            a->reverse->position, a->reverse->columnDepth,
            a->shared->weight + b->shared->weight,
            a->shared->alignmentScore + b->shared->alignmentScore, alignedPairs);
}

static void removeWeights(stSortedSet *columnWeightsSortedByWeight,
        stSortedSet *columnWeightsSortedByPosition,
        PairwiseColumnWeight *pairwiseColumnWeight) {
    assert(stSortedSet_search(columnWeightsSortedByPosition, pairwiseColumnWeight) != NULL);
    assert(stSortedSet_search(columnWeightsSortedByPosition, pairwiseColumnWeight->reverse) != NULL);
    assert(stSortedSet_search(columnWeightsSortedByWeight, pairwiseColumnWeight) != NULL);
    assert(stSortedSet_search(columnWeightsSortedByWeight, pairwiseColumnWeight->reverse) != NULL);
    stSortedSet_remove(columnWeightsSortedByPosition, pairwiseColumnWeight);
    stSortedSet_remove(columnWeightsSortedByPosition,
            pairwiseColumnWeight->reverse);
    stSortedSet_remove(columnWeightsSortedByWeight, pairwiseColumnWeight);
    stSortedSet_remove(columnWeightsSortedByWeight,
            pairwiseColumnWeight->reverse);
    assert(stSortedSet_search(columnWeightsSortedByPosition, pairwiseColumnWeight) == NULL);
    assert(stSortedSet_search(columnWeightsSortedByPosition, pairwiseColumnWeight->reverse) == NULL);
    assert(stSortedSet_search(columnWeightsSortedByWeight, pairwiseColumnWeight) == NULL);
    assert(stSortedSet_search(columnWeightsSortedByWeight, pairwiseColumnWeight->reverse) == NULL);
}

static void addWeights(stSortedSet *columnWeightsSortedByWeight,
        stSortedSet *columnWeightsSortedByPosition,
        PairwiseColumnWeight *pairwiseColumnWeight) {
    assert(stSortedSet_search(columnWeightsSortedByPosition, pairwiseColumnWeight) == NULL);
    assert(stSortedSet_search(columnWeightsSortedByPosition, pairwiseColumnWeight->reverse) == NULL);
    assert(stSortedSet_search(columnWeightsSortedByWeight, pairwiseColumnWeight) == NULL);
    assert(stSortedSet_search(columnWeightsSortedByWeight, pairwiseColumnWeight->reverse) == NULL);
    stSortedSet_insert(columnWeightsSortedByPosition, pairwiseColumnWeight);
    stSortedSet_insert(columnWeightsSortedByWeight, pairwiseColumnWeight);
    stSortedSet_insert(columnWeightsSortedByPosition,
            pairwiseColumnWeight->reverse);
    stSortedSet_insert(columnWeightsSortedByWeight,
            pairwiseColumnWeight->reverse);
    assert(stSortedSet_search(columnWeightsSortedByPosition, pairwiseColumnWeight) != NULL);
    assert(stSortedSet_search(columnWeightsSortedByPosition, pairwiseColumnWeight->reverse) != NULL);
    assert(stSortedSet_search(columnWeightsSortedByWeight, pairwiseColumnWeight) != NULL);
    assert(stSortedSet_search(columnWeightsSortedByWeight, pairwiseColumnWeight->reverse) != NULL);
}

static stList *getPositions(stSortedSet *columnWeightsSortedByPosition,
        PairwiseColumnWeight *pairwiseColumnWeight) {
    //Get all positions in the reverse sequence position to replace.
    stSortedSetIterator *it = stSortedSet_getIteratorFrom(
            columnWeightsSortedByPosition, pairwiseColumnWeight);
    stList *list = stList_construct();
    PairwiseColumnWeight *i;
    assert(stSortedSet_getNext(it) == pairwiseColumnWeight);
    while ((i = stSortedSet_getNext(it)) != NULL && i->sequence
            == pairwiseColumnWeight->sequence && i->position
            == pairwiseColumnWeight->position) {
        ; //We iterate to the beginning of the sequence of positions.
    }
    i = stSortedSet_getPrevious(it);
    do {
        assert(i != NULL);
        assert(i->sequence == pairwiseColumnWeight->sequence);
        assert(i->position == pairwiseColumnWeight->position);
        if (pairwiseColumnWeight != i) {
            stList_append(list, i);
        }
    } while (((i = stSortedSet_getPrevious(it)) != NULL && i->sequence
            == pairwiseColumnWeight->sequence && i->position
            == pairwiseColumnWeight->position));
    stSortedSet_destructIterator(it);
    return list;
}

static void updatePairwiseColumnWeights(
        stSortedSet *columnWeightsSortedByWeight,
        stSortedSet *columnWeightsSortedByPosition,
        PairwiseColumnWeight *pairwiseColumnWeight) {
    /*
     * Replace all the positions in the first alignment by the second.
     */
    //Column weights involving the reverse position (which is to be merged with the first position)
    stList *list = getPositions(columnWeightsSortedByPosition,
            pairwiseColumnWeight->reverse);
    //Column weights involving the second position
    stList *list2 = getPositions(columnWeightsSortedByPosition,
            pairwiseColumnWeight);

    //Now remove the reverse positions and reinsert them merged with the forward positions
    PairwiseColumnWeight *i;
    assert(
            pairwiseColumnWeight->sequence
                    != pairwiseColumnWeight->reverse->sequence);
    for (int32_t j = 0; j < stList_length(list); j++) {
        i = stList_get(list, j);
        //Check weight is as we expect
        assert(i != pairwiseColumnWeight);
        assert(i != pairwiseColumnWeight->reverse);
        assert(i->sequence == pairwiseColumnWeight->reverse->sequence);
        assert(i->position == pairwiseColumnWeight->reverse->position);
        assert(i->columnDepth == pairwiseColumnWeight->reverse->columnDepth);
        if(i->reverse->sequence == pairwiseColumnWeight->sequence) {
            assert(i->reverse->position != pairwiseColumnWeight->position);
        }
        //Remove the position
        removeWeights(columnWeightsSortedByWeight,
                columnWeightsSortedByPosition, i);
        //Now modify to match the merged
        i->sequence = pairwiseColumnWeight->sequence;
        i->position = pairwiseColumnWeight->position;
        PairwiseColumnWeight *k = stSortedSet_search(
                columnWeightsSortedByPosition, i); //look for a column weight to merge with
        if (k != NULL) { //we have a valid merge
            //Check weight to merge is as we expect
            assert(k != i);
            assert(k != i->reverse);
            assert(k != pairwiseColumnWeight);
            assert(k != pairwiseColumnWeight->reverse);
            assert(k->sequence == pairwiseColumnWeight->sequence);
            assert(k->position == pairwiseColumnWeight->position);
            if(k->reverse->sequence == pairwiseColumnWeight->sequence) {
                assert(k->reverse->position != pairwiseColumnWeight->position);
            }
            if(i->reverse->sequence == pairwiseColumnWeight->reverse->sequence) {
                assert(i->reverse->position != pairwiseColumnWeight->reverse->position);
            }
            assert(k->columnDepth == pairwiseColumnWeight->columnDepth);
            assert(stList_contains(list2, k));
            stList_removeItem(list2, k); //Remove this from the second list, as it is now merged.
            assert(!stList_contains(list2, k));
            removeWeights(columnWeightsSortedByWeight,
                    columnWeightsSortedByPosition, k);
            addWeights(columnWeightsSortedByWeight,
                    columnWeightsSortedByPosition,
                    pairwiseColumnWeight_merge(i, k));
            pairwiseColumnWeight_destruct(i);
            pairwiseColumnWeight_destruct(k);
        } else {
            i->columnDepth = i->columnDepth + pairwiseColumnWeight->columnDepth;
            addWeights(columnWeightsSortedByWeight,
                    columnWeightsSortedByPosition, i);
        }
    }
    //Now update the column depth of the column weights of the positions in the second list
    for (int32_t j = 0; j < stList_length(list2); j++) {
        i = stList_get(list2, j);
        //Check weight to adjust is as we expect
        assert(i != pairwiseColumnWeight);
        assert(i != pairwiseColumnWeight->reverse);
        assert(i->sequence == pairwiseColumnWeight->sequence);
        assert(i->position == pairwiseColumnWeight->position);
        assert(i->columnDepth == pairwiseColumnWeight->columnDepth);
        if(i->reverse->sequence == pairwiseColumnWeight->sequence) {
            assert(i->reverse->position != pairwiseColumnWeight->position);
        }
        if(i->reverse->sequence == pairwiseColumnWeight->reverse->sequence) {
            assert(i->reverse->position != pairwiseColumnWeight->reverse->position);
        }
        removeWeights(columnWeightsSortedByWeight,
                columnWeightsSortedByPosition, i);
        i->columnDepth = pairwiseColumnWeight->columnDepth
                + pairwiseColumnWeight->reverse->columnDepth;
        addWeights(columnWeightsSortedByWeight, columnWeightsSortedByPosition,
                i);
    }
#ifndef NDEBUG
    stList *list3 = getPositions(columnWeightsSortedByPosition, pairwiseColumnWeight);
    assert(stList_length(list3) <= stList_length(list) + stList_length(list2));
    stList_destruct(list3);
    list3 = getPositions(columnWeightsSortedByPosition, pairwiseColumnWeight->reverse);
    assert(stList_length(list3) == 0);
    stList_destruct(list3);
#endif
    stList_destruct(list);
    stList_destruct(list2);
}

static void destroyWeights(PairwiseColumnWeight *pairwiseColumnWeight) {
    while (stSortedSet_size(pairwiseColumnWeight->shared->alignedPairs) > 0) {
        stIntTuple *alignedPair = stSortedSet_getFirst(
                pairwiseColumnWeight->shared->alignedPairs);
        stSortedSet_remove(pairwiseColumnWeight->shared->alignedPairs,
                alignedPair);
        stIntTuple_destruct(alignedPair);
    }
}

stList *makeAlignment(stList *sequences, int32_t spanningTrees, float gapGamma,
        PairwiseAlignmentParameters *pairwiseAlignmentBandingParameters) {
    //Get the set of pairwise alignments (by constructing spanning trees)
    stSortedSet *pairwiseAlignments = stSortedSet_construct3(
            (int(*)(const void *, const void *)) stIntTuple_cmpFn,
            (void(*)(void *)) stIntTuple_destruct);

    int32_t sequenceNo = stList_length(sequences);
    //int32_t maxAlignmentPairs = (sequenceNo * sequenceNo - sequenceNo) / 2;
    bool divideByObservedPairs = 1; //0;
    constructCyclicPermuations(sequenceNo, pairwiseAlignments, spanningTrees);
    if(stSortedSet_size(pairwiseAlignments) == (sequenceNo*sequenceNo - sequenceNo)/2) {
        divideByObservedPairs = 0; //We can use the proper divisor.
    }

    //Construct the alignments
    //and sort them by weight
    stSortedSetIterator *pairwiseAlignmentsIterator = stSortedSet_getIterator(
            pairwiseAlignments);
    stIntTuple *pairwiseAlignment;
    stSortedSet
            *columnWeightsSortedByWeight =
                    stSortedSet_construct3(
                            (int(*)(const void *, const void *)) (divideByObservedPairs ?
                                    pairwiseColumnWeight_compareByWeightFn_divideByObservedPairs
                                    : pairwiseColumnWeight_compareByWeightFn_divideByAllPairs),
                            NULL); //destructor not set as will be empty when destroyed.
    stSortedSet
            *columnWeightsSortedByPosition =
                    stSortedSet_construct3(
                            (int(*)(const void *, const void *)) pairwiseColumnWeight_compareByPositionFn,
                            NULL);
    stList *pairwiseAlignmentsStack = stList_construct();
    while ((pairwiseAlignment = (stIntTuple *) stSortedSet_getNext(
            pairwiseAlignmentsIterator)) != NULL) { //Splitting loop into two whiles, so first loop can be open-mp'd.
        int32_t sequence1 = stIntTuple_getPosition(pairwiseAlignment, 0);
        int32_t sequence2 = stIntTuple_getPosition(pairwiseAlignment, 1);
        char *string1 = stList_get(sequences, sequence1);
        char *string2 = stList_get(sequences, sequence2);
        stList_append(pairwiseAlignmentsStack, getAlignedPairs(
                string1, string2, pairwiseAlignmentBandingParameters));
    }

    while ((pairwiseAlignment = (stIntTuple *) stSortedSet_getPrevious(
                pairwiseAlignmentsIterator)) != NULL) {
        int32_t sequence1 = stIntTuple_getPosition(pairwiseAlignment, 0);
        int32_t sequence2 = stIntTuple_getPosition(pairwiseAlignment, 1);
        char *string1 = stList_get(sequences, sequence1);
        char *string2 = stList_get(sequences, sequence2);
        int32_t seqLength1 = strlen(string1);
        int32_t seqLength2 = strlen(string2);
        stList *alignedPairs2 = stList_pop(pairwiseAlignmentsStack);
        //Make indel probs
        /*int32_t *indelProbs1 =
         calculateIndelProbs(alignedPairs2, seqLength1, 0);
         int32_t *indelProbs2 =
         calculateIndelProbs(alignedPairs2, seqLength2, 1);*/
        double alignmentScore = getAlignmentScore(alignedPairs2, seqLength1,
                seqLength2);

        assert(alignmentScore >= -0.00001);
        assert(alignmentScore <= 1.00001);
        //Now deal with the match probs..
        while (stList_length(alignedPairs2) > 0) {
            stIntTuple *alignedPair = (stIntTuple *) stList_pop(alignedPairs2);
            assert(stIntTuple_length(alignedPair) == 3);
            int32_t position1 = stIntTuple_getPosition(alignedPair, 1);
            int32_t position2 = stIntTuple_getPosition(alignedPair, 2);
            //Work out the scores
            double pairwiseProbability = (double) stIntTuple_getPosition(
                    alignedPair, 0) / PAIR_ALIGNMENT_PROB_1;
            assert(pairwiseProbability >= -0.0001);
            assert(pairwiseProbability <= 1.00001);
            pairwiseProbability += st_random() * 0.00001; //This randomness avoids nasty types of unbalanced trees.
            //Construct the aligned pair structures.
            stIntTuple *alignedPair2 = stIntTuple_construct(5,
            /* score */stIntTuple_getPosition(alignedPair, 0),
            /* seq 1 */sequence1, position1,
            /* seq 2 */sequence2, position2);
            stSortedSet
                    *sortedSet =
                            stSortedSet_construct3(
                                    (int(*)(const void *, const void *)) stIntTuple_cmpFn,
                                    NULL);
            stSortedSet_insert(sortedSet, alignedPair2);
            addWeights(
                    columnWeightsSortedByWeight,
                    columnWeightsSortedByPosition,
                    pairwiseColumnWeight_construct(sequence1, position1, 1,
                            sequence2, position2, 1,
                            pairwiseProbability * alignmentScore, //pairwiseWeight - gapGamma * (indelWeight1 + indelWeight2),
                            pairwiseProbability, sortedSet));
            stIntTuple_destruct(alignedPair);
        }
        stList_destruct(alignedPairs2);
        //free(indelProbs1);
        //free(indelProbs2);
    }
    stList_destruct(pairwiseAlignmentsStack);
    stSortedSet_destructIterator(pairwiseAlignmentsIterator);
    //Greedily construct poset and filter pairs..
    stPosetAlignment *posetAlignment = stPosetAlignment_construct(
            stList_length(sequences));
    stList *acceptedAlignedPairs = stList_construct3(0,
            (void(*)(void *)) stIntTuple_destruct);
    double pWeight = INT64_MAX;

    while (stSortedSet_size(columnWeightsSortedByWeight) > 0) {
        //The two trees of weights should have identical cardinality and should be of even parity (as we have reverses)
        assert(stSortedSet_size(columnWeightsSortedByWeight) == stSortedSet_size(columnWeightsSortedByPosition));
        assert(stSortedSet_size(columnWeightsSortedByWeight) % 2 == 0);
        PairwiseColumnWeight *pairwiseColumnWeight = stSortedSet_getLast(
                columnWeightsSortedByWeight);
        double score = pairwiseColumnWeight_getNormalisedAlignmentScore(
                pairwiseColumnWeight, divideByObservedPairs);
        double weight = pairwiseColumnWeight_getNormalisedWeight(
                pairwiseColumnWeight, divideByObservedPairs);

        //NormalisedWeight(pairwiseColumnWeight);
        assert(weight <= pWeight + 0.0001);
        pWeight = weight;
        assert(stSortedSet_size(pairwiseColumnWeight->shared->alignedPairs) > 0);
        if (score >= gapGamma) {
            stIntTuple *alignedPair = stSortedSet_getFirst(
                    pairwiseColumnWeight->shared->alignedPairs);
            if (stPosetAlignment_isPossible(posetAlignment,
                    stIntTuple_getPosition(alignedPair, 1),
                    stIntTuple_getPosition(alignedPair, 2),
                    stIntTuple_getPosition(alignedPair, 3),
                    stIntTuple_getPosition(alignedPair, 4))) {

                bool i = stPosetAlignment_add(posetAlignment,
                        stIntTuple_getPosition(alignedPair, 1),
                        stIntTuple_getPosition(alignedPair, 2),
                        stIntTuple_getPosition(alignedPair, 3),
                        stIntTuple_getPosition(alignedPair, 4));
                (void)i;
                assert(i);
                //Add to the output
                stList *list = stSortedSet_getList(
                        pairwiseColumnWeight->shared->alignedPairs);
                stList_appendAll(acceptedAlignedPairs, list);
                stList_destruct(list);
                //Now update the weights..
                updatePairwiseColumnWeights(columnWeightsSortedByWeight,
                        columnWeightsSortedByPosition, pairwiseColumnWeight);
                removeWeights(columnWeightsSortedByWeight,
                        columnWeightsSortedByPosition, pairwiseColumnWeight); //remove from the hash
            } else {
                removeWeights(columnWeightsSortedByWeight,
                        columnWeightsSortedByPosition, pairwiseColumnWeight); //remove from the hash
                destroyWeights(pairwiseColumnWeight);
            }
        } else {
            removeWeights(columnWeightsSortedByWeight,
                    columnWeightsSortedByPosition, pairwiseColumnWeight); //remove from the hash
            destroyWeights(pairwiseColumnWeight);
        }
        pairwiseColumnWeight_destruct(pairwiseColumnWeight);
    }

    //Cleanup
    assert(stSortedSet_size(columnWeightsSortedByWeight) == 0);
    assert(stSortedSet_size(columnWeightsSortedByPosition) == 0);
    stSortedSet_destruct(columnWeightsSortedByWeight);
    stSortedSet_destruct(columnWeightsSortedByPosition);
    stPosetAlignment_destruct(posetAlignment);
    stSortedSet_destruct(pairwiseAlignments);
    //Return the accepted pairs
    return acceptedAlignedPairs;
}