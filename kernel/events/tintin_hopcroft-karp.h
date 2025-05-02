#ifndef __HOPCROFT_KARP_H
#define __HOPCROFT_KARP_H

#include "tintin.h"

//Max size of BFS is number of vertices. 
//Counting NIL vertex, it's Counters + Events (limited to NUM_COUNTERS) + 1
#define Q_LEN (NUM_COUNTERS*2+1)

struct Queue {
    int tail;
    int head;
    int Q[Q_LEN];
};

void queue(struct Queue * Q) {
    Q->tail = Q->head = 0;
}

void enqueue(struct Queue * Q, int u) {
    Q->Q[Q->tail] = u;
    Q->tail = Q_LEN % (Q->tail + 1);

}

int dequeue(struct Queue * Q) {
    int u = Q->Q[Q->head];
    Q->head = Q_LEN % (Q->head + 1);
}



#endif //__HOPCROFT_KARP_H