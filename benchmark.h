#ifndef BENCHMARK_H
#define BENCHMARK_H

#include "shogi.h"

// Structure to hold benchmark results
typedef struct {
    char sfen[256];
    int score;
    uint64_t nodes;
    unsigned int time_ms;
    unsigned int nps;
    int depth;
} benchmark_result_t;

// Run benchmark on a single SFEN position
benchmark_result_t run_benchmark_position(tree_t* ptree, const char* sfen, int depth);

// Run benchmark on multiple positions
void run_benchmark_suite(const char* sfens[], int num_positions, int depth);

// Standard benchmark positions
extern const char* benchmark_positions[];
extern const int benchmark_positions_count;

#endif // BENCHMARK_H