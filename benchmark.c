#include "benchmark.h"
#include "usi.h"
#include "nnue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>

// Standard benchmark positions in SFEN format
const char* benchmark_positions[] = {
    // Starting position
    "lnsgkgsnl/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL b - 1",
    
    // Middle game positions
    "lnsgk1snl/1r5b1/pppppp1pp/6p2/9/2P6/PP1PPPPPP/1B5R1/LNSGKGSNL w - 1",
    "l6nl/5+P1gk/2np1S3/p1p4Pp/3P2Sp1/1PPb2P1P/P5GS1/R8/LN4bKL w RGgsn5p 1",
    
    // Tactical positions
    "4k4/9/PPPPPPPPP/9/9/9/ppppppppp/9/4K4 b - 1",
    "8l/1l+R2P3/p2pBG1pp/kps1p4/Nn1P2G2/P1P1P2PP/1PS6/1KSG3+r1/LN2+p3L w Sbgn3p 1"
};

const int benchmark_positions_count = sizeof(benchmark_positions) / sizeof(benchmark_positions[0]);

// Run benchmark on a single SFEN position
benchmark_result_t run_benchmark_position(tree_t* ptree, const char* sfen, int depth) {
    benchmark_result_t result;
    unsigned int start_time, end_time;
    
    // Initialize result structure
    memset(&result, 0, sizeof(result));
    strncpy(result.sfen, sfen, 255);
    result.sfen[255] = '\0';
    
    // Parse SFEN and set up position
    if (!usi_parse_sfen(ptree, sfen)) {
        Out("Failed to parse SFEN: %s", sfen);
        return result;
    }
    
    // Clear hash table
    clear_trans_table();
    
    // Set up search parameters
    depth_limit = depth * PLY_INC;
    time_limit = UINT_MAX;
    node_limit = UINT64_MAX;
    
    // Reset node count
    ptree->node_searched = 0;
    
    // Generate legal moves
    make_root_move_list(ptree, flag_history);
    
    // Start timer
    get_elapsed(&start_time);
    
    // Run search
    root_abort = 0;
    if (nnue.model_loaded) {
        // Reset accumulators
        for (int i = 0; i < PLY_MAX + 1; i++) {
            nnue.accumulators[i].accumulation_computed = false;
        }
    }
    
    // Perform search
    int score = searchr(ptree, -score_mate1ply, score_mate1ply, root_turn, depth_limit);
    
    // Stop timer
    get_elapsed(&end_time);
    unsigned int elapsed = end_time - start_time;
    
    // Record results
    result.score = score;
    result.nodes = ptree->node_searched;
    result.time_ms = elapsed;
    result.nps = elapsed > 0 ? (unsigned int)(result.nodes * 1000 / elapsed) : 0;
    result.depth = depth;
    
    return result;
}

// Run benchmark on multiple positions
void run_benchmark_suite(const char* sfens[], int num_positions, int depth) {
    tree_t* ptree = &tree;
    benchmark_result_t results[num_positions];
    uint64_t total_nodes = 0;
    unsigned int total_time = 0;
    
    printf("Starting benchmark suite with %d positions at depth %d\n", num_positions, depth);
    printf("-------------------------------------------------------\n");
    
    for (int i = 0; i < num_positions; i++) {
        printf("Position %d: %s\n", i + 1, sfens[i]);
        
        results[i] = run_benchmark_position(ptree, sfens[i], depth);
        
        // Print results
        printf("  Score: %d\n", results[i].score);
        printf("  Nodes: %" PRIu64 "\n", results[i].nodes);
        printf("  Time: %u ms\n", results[i].time_ms);
        printf("  NPS: %u\n", results[i].nps);
        printf("-------------------------------------------------------\n");
        
        // Update totals
        total_nodes += results[i].nodes;
        total_time += results[i].time_ms;
    }
    
    // Print summary
    printf("Benchmark Summary:\n");
    printf("  Total nodes: %" PRIu64 "\n", total_nodes);
    printf("  Total time: %u ms\n", total_time);
    printf("  Average NPS: %u\n", total_time > 0 ? (unsigned int)(total_nodes * 1000 / total_time) : 0);
}