
/*
 * Implements Hopcroft-Karp maximal bipartite matching according to the pseudocode at
 * https://en.wikipedia.org/wiki/Hopcroft%E2%80%93Karp_algorithm#Pseudocode
 */

#include "tintin.h"
#include "event.h"
#include "counter.h"
#include "hopcroft-karp.h"

#define INF (NUM_COUNTERS + 2)
#define NIL NUM_COUNTERS

int bfs(int * Pair_U, int * Pair_V, int * Dist,
        struct Event * events, const int events_len,
        struct Counter * counters, const int counters_len)
{

    struct Queue Q;
    queue(&Q);

    for (int u = 0; u < events_len; ++u) {
        if (Pair_U[u] == NIL) {
            Dist[u] = 0;
            enqueue(&Q, u);
        }
        else {
            Dist[u] = INF;
        }
    }
    Dist[NIL] = INF;
    while (!empty(Q)) {
        int u = dequeue(&Q);
        if (Dist[u] < Dist[NIL]) {
            //Equivalent to for each v in Adj [u]
            for (int v = 0; v < counters_len; ++v) {
                if (!events[u].counters[counters[v].id]) continue;

                if (Dist[Pair_V[v]] == INF) {
                    Dist[Pair_V[v]] = Dist[u] + 1;
                    enqueue(&Q, Pair_V[v]);
                }

            }

        }
    }

    return (Dist[NIL] != INF);

}

int dfs(int u,
        int * Pair_U, int * Pair_V, int * Dist,
        struct Event * events,
        struct Counter * counters, const int counters_len)
{
    if (u != NUM_COUNTERS) {

        //Equivalent to for each v in Adj [u]
        for (int v = 0; v < counters_len; ++v) {
            if (!events[u].counters[counters[v].id]) continue;

            if (Dist[Pair_V[v]] == Dist[u] + 1) {
                if (dfs(Pair_V[v], Pair_U, Pair_V, Dist, events, counters, counters_len)) {
                    Pair_V[v] = u;
                    Pair_U[u] = v;
                    return 1;
                }
            }
        }
        Dist[u] = INF;
        return 0;
    }
    return 1;
}

/*
 * void hopcroft_karp(struct Event * events, const int events_len, struct Counter * counters, const int counters_len)
 * Find a maximal bipartite matching between events and counters
 * The Event struct contains an array, with each index representing the corresponding Counter id.
 * A value of 0/1 indicates whether the Event can be scheduled on the corresponding Counter 
 * Inputs:
 *  struct Event * events: an array of events, candidates for an event group
 *  const int events_len: the length of the events array
 *  struct Counter * counters: an array of counters, representing available counters on the system
 *  const int counters_len: the length of the counters array
 */
void hopcroft_karp(struct Event * events,
                const int events_len,
                struct Counter * counters,
                const int counters_len)
{

    //Candidate events and counters cannot exceed number of counters on the PMU
    int Pair_U[NUM_COUNTERS];
    int Pair_V[NUM_COUNTERS];
    int Dist[NUM_COUNTERS+1];

    for (int u = 0; u < events_len; ++u) {
        Pair_U[u] = NIL;
        events[u].counter = -1;
    }
    for (int v = 0; v < counters_len; ++v) {
        Pair_V[v] = NIL;
    }

    while (bfs(Pair_U, Pair_V, Dist, events, events_len, counters, counters_len)) {
        for (int u = 0; u < events_len; ++u) {
            if (Pair_U[u] == -1) dfs(u, Pair_U, Pair_V, Dist, events, counters, counters_len);
        }
    }

    //Algorithm complete, assign events to counters
    for (int u = 0; u < events_len; ++u) {
        int v = Pair_U[u];
        if (v != NIL) {
            events[u].counter = counters[v].id;
        }
    }
}