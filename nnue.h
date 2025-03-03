#ifndef NNUE_H
#define NNUE_H

#include "shogi.h"
#include <stdbool.h>

// NNUE parameters - compatible with YaneuraOu's nn.bin format
// Use the FV_SCALE from shogi.h instead
// #define FV_SCALE 16
#define NNUE_HEADER_SIZE 0x30 // 48 bytes header for YaneuraOu nn.bin format

// Architecture parameters
#define NNUE_INPUT_DIM  (fe_end * 2) // Both perspective
#define NNUE_HIDDEN1     256          // First layer hidden units
#define NNUE_HIDDEN2     32           // Second layer hidden units
#define NNUE_OUTPUT_DIM  1            // Output dimension (score)

// Network structure
typedef struct {
    char header[NNUE_HEADER_SIZE];    // File header
    int16_t ft_weights[NNUE_INPUT_DIM][NNUE_HIDDEN1]; // Feature transformer weights
    int16_t ft_biases[NNUE_HIDDEN1];                 // Feature transformer biases
    int16_t fc_weights1[NNUE_HIDDEN1*2][NNUE_HIDDEN2]; // Fully connected layer 1 weights
    int16_t fc_biases1[NNUE_HIDDEN2];                // FC1 biases
    int16_t fc_weights2[NNUE_HIDDEN2][NNUE_OUTPUT_DIM]; // Output layer weights
    int16_t fc_biases2[NNUE_OUTPUT_DIM];             // Output biases
} NNUEModel;

// Accumulator for incremental feature updates
typedef struct {
    int16_t accumulation[NNUE_HIDDEN1];   // Accumulated values for feature transformer
    bool accumulation_computed;           // Whether accumulation is valid
} Accumulator;

// NNUE context
typedef struct {
    NNUEModel model;
    bool model_loaded;
    Accumulator accumulators[PLY_MAX + 1]; // Per-ply accumulators
} NNUE;

// Global NNUE instance
extern NNUE nnue;

// Initialize NNUE
int nnue_init(const char* filename);

// Free NNUE resources
void nnue_free(void);

// Refresh the accumulator stack
void nnue_refresh_accumulator(tree_t* ptree, int ply);

// Incrementally update accumulator after a move
void nnue_update_accumulator(tree_t* ptree, int ply, unsigned int move);

// Incrementally revert accumulator after unmaking a move
void nnue_revert_accumulator(tree_t* ptree, int ply);

// Evaluate position using NNUE
int nnue_evaluate(tree_t* ptree, int ply, int turn);

// Convert piece on square to feature index
int piece_to_index(int piece, int square, int perspective);

// Run SFEN benchmark to evaluate NNUE speed
int nnue_benchmark_sfen(const char* sfen, int num_iterations);

#endif // NNUE_H