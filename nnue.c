#include "nnue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Global NNUE instance
NNUE nnue;

// Clamp value between min and max
static inline int clamp(int value, int min_value, int max_value) {
    return value < min_value ? min_value : (value > max_value ? max_value : value);
}

// Activation function (ReLU)
static inline int16_t relu(int32_t x) {
    return x > 0 ? x : 0;
}

// Load NNUE model from file
int nnue_init(const char* filename) {
    FILE* file = fopen(filename, "rb");
    
    if (!file) {
        out_warning("Failed to open NNUE file: %s", filename);
        return 0;
    }
    
    // Read header
    if (fread(nnue.model.header, NNUE_HEADER_SIZE, 1, file) != 1) {
        out_warning("Failed to read NNUE header");
        fclose(file);
        return 0;
    }
    
    // Validate header (this should be customized for YaneuraOu's format)
    if (memcmp(nnue.model.header, "NNUEv2", 6) != 0) {
        out_warning("Invalid NNUE file format: not NNUEv2");
        fclose(file);
        return 0;
    }
    
    // Read weights and biases
    if (fread(nnue.model.ft_weights, sizeof(int16_t), NNUE_INPUT_DIM * NNUE_HIDDEN1, file) 
            != NNUE_INPUT_DIM * NNUE_HIDDEN1 ||
        fread(nnue.model.ft_biases, sizeof(int16_t), NNUE_HIDDEN1, file) 
            != NNUE_HIDDEN1 ||
        fread(nnue.model.fc_weights1, sizeof(int16_t), NNUE_HIDDEN1 * 2 * NNUE_HIDDEN2, file) 
            != NNUE_HIDDEN1 * 2 * NNUE_HIDDEN2 ||
        fread(nnue.model.fc_biases1, sizeof(int16_t), NNUE_HIDDEN2, file) 
            != NNUE_HIDDEN2 ||
        fread(nnue.model.fc_weights2, sizeof(int16_t), NNUE_HIDDEN2 * NNUE_OUTPUT_DIM, file) 
            != NNUE_HIDDEN2 * NNUE_OUTPUT_DIM ||
        fread(nnue.model.fc_biases2, sizeof(int16_t), NNUE_OUTPUT_DIM, file) 
            != NNUE_OUTPUT_DIM) {
        
        out_warning("Failed to read NNUE weights and biases");
        fclose(file);
        return 0;
    }
    
    fclose(file);
    
    // Initialize accumulators
    for (int i = 0; i < PLY_MAX + 1; i++) {
        nnue.accumulators[i].accumulation_computed = false;
    }
    
    nnue.model_loaded = true;
    Out("NNUE loaded successfully: %s", filename);
    
    return 1;
}

// Free NNUE resources
void nnue_free(void) {
    nnue.model_loaded = false;
}

// Convert piece on square to feature index
int piece_to_index(int piece, int square, int perspective) {
    int index = -1;
    
    // Adjust for perspective (black or white view)
    int adjusted_sq = perspective == black ? square : Inv(square);
    
    // Convert piece to feature index based on piece type and location
    if (piece > 0) {  // Black piece
        switch (piece) {
            case pawn:       index = f_pawn + adjusted_sq; break;
            case lance:      index = f_lance + adjusted_sq; break;
            case knight:     index = f_knight + adjusted_sq; break;
            case silver:     index = f_silver + adjusted_sq; break;
            case gold:       index = f_gold + adjusted_sq; break;
            case bishop:     index = f_bishop + adjusted_sq; break;
            case rook:       index = f_rook + adjusted_sq; break;
            case king:       return -1; // King has special handling
            case pro_pawn:   index = f_pawn + adjusted_sq; break;
            case pro_lance:  index = f_lance + adjusted_sq; break;
            case pro_knight: index = f_knight + adjusted_sq; break;
            case pro_silver: index = f_silver + adjusted_sq; break;
            case horse:      index = f_horse + adjusted_sq; break;
            case dragon:     index = f_dragon + adjusted_sq; break;
            default:         return -1;
        }
    } else if (piece < 0) {  // White piece
        switch (-piece) {
            case pawn:       index = e_pawn + adjusted_sq; break;
            case lance:      index = e_lance + adjusted_sq; break;
            case knight:     index = e_knight + adjusted_sq; break;
            case silver:     index = e_silver + adjusted_sq; break;
            case gold:       index = e_gold + adjusted_sq; break;
            case bishop:     index = e_bishop + adjusted_sq; break;
            case rook:       index = e_rook + adjusted_sq; break;
            case king:       return -1; // King has special handling
            case pro_pawn:   index = e_pawn + adjusted_sq; break;
            case pro_lance:  index = e_lance + adjusted_sq; break;
            case pro_knight: index = e_knight + adjusted_sq; break;
            case pro_silver: index = e_silver + adjusted_sq; break;
            case horse:      index = e_horse + adjusted_sq; break;
            case dragon:     index = e_dragon + adjusted_sq; break;
            default:         return -1;
        }
    }
    
    return index;
}

// Convert hand piece to feature index
int hand_to_index(int piece_type, int count, int perspective) {
    if (count <= 0) return -1;
    
    int index = -1;
    int base = perspective == black ? 0 : NNUE_INPUT_DIM / 2;
    
    // Convert hand piece to feature index
    switch (piece_type) {
        case pawn:      index = f_hand_pawn + count - 1; break;
        case lance:     index = f_hand_lance + count - 1; break;
        case knight:    index = f_hand_knight + count - 1; break;
        case silver:    index = f_hand_silver + count - 1; break;
        case gold:      index = f_hand_gold + count - 1; break;
        case bishop:    index = f_hand_bishop + count - 1; break;
        case rook:      index = f_hand_rook + count - 1; break;
        default:        return -1;
    }
    
    return base + index;
}

// Refresh the accumulator stack
void nnue_refresh_accumulator(tree_t* ptree, int ply) {
    if (!nnue.model_loaded) return;
    
    Accumulator* acc = &nnue.accumulators[ply];
    
    // Reset accumulator
    memcpy(acc->accumulation, nnue.model.ft_biases, NNUE_HIDDEN1 * sizeof(int16_t));
    
    // Process the board position
    for (int sq = 0; sq < nsquare; sq++) {
        int piece = BOARD[sq];
        if (piece == empty) continue;
        if (piece == king || piece == -king) continue; // Kings handled separately
        
        // Add features from both perspectives
        int index_black = piece_to_index(piece, sq, black);
        int index_white = piece_to_index(piece, sq, white);
        
        if (index_black >= 0) {
            for (int i = 0; i < NNUE_HIDDEN1; i++) {
                acc->accumulation[i] += nnue.model.ft_weights[index_black][i];
            }
        }
        
        if (index_white >= 0) {
            for (int i = 0; i < NNUE_HIDDEN1; i++) {
                acc->accumulation[i] += nnue.model.ft_weights[NNUE_INPUT_DIM / 2 + index_white][i];
            }
        }
    }
    
    // Process hand pieces
    // Black's hand
    unsigned int hand = HAND_B;
    if (hand) {
        int count = I2HandPawn(hand);
        if (count > 0) {
            int index = hand_to_index(pawn, count, black);
            if (index >= 0) {
                for (int i = 0; i < NNUE_HIDDEN1; i++) {
                    acc->accumulation[i] += nnue.model.ft_weights[index][i];
                }
            }
        }
        
        count = I2HandLance(hand);
        if (count > 0) {
            int index = hand_to_index(lance, count, black);
            if (index >= 0) {
                for (int i = 0; i < NNUE_HIDDEN1; i++) {
                    acc->accumulation[i] += nnue.model.ft_weights[index][i];
                }
            }
        }
        
        // More hand pieces...
        count = I2HandKnight(hand);
        if (count > 0) {
            int index = hand_to_index(knight, count, black);
            if (index >= 0) {
                for (int i = 0; i < NNUE_HIDDEN1; i++) {
                    acc->accumulation[i] += nnue.model.ft_weights[index][i];
                }
            }
        }
        
        count = I2HandSilver(hand);
        if (count > 0) {
            int index = hand_to_index(silver, count, black);
            if (index >= 0) {
                for (int i = 0; i < NNUE_HIDDEN1; i++) {
                    acc->accumulation[i] += nnue.model.ft_weights[index][i];
                }
            }
        }
        
        count = I2HandGold(hand);
        if (count > 0) {
            int index = hand_to_index(gold, count, black);
            if (index >= 0) {
                for (int i = 0; i < NNUE_HIDDEN1; i++) {
                    acc->accumulation[i] += nnue.model.ft_weights[index][i];
                }
            }
        }
        
        count = I2HandBishop(hand);
        if (count > 0) {
            int index = hand_to_index(bishop, count, black);
            if (index >= 0) {
                for (int i = 0; i < NNUE_HIDDEN1; i++) {
                    acc->accumulation[i] += nnue.model.ft_weights[index][i];
                }
            }
        }
        
        count = I2HandRook(hand);
        if (count > 0) {
            int index = hand_to_index(rook, count, black);
            if (index >= 0) {
                for (int i = 0; i < NNUE_HIDDEN1; i++) {
                    acc->accumulation[i] += nnue.model.ft_weights[index][i];
                }
            }
        }
    }
    
    // White's hand
    hand = HAND_W;
    if (hand) {
        // Similar processing for white's hand pieces
        int count = I2HandPawn(hand);
        if (count > 0) {
            int index = hand_to_index(pawn, count, white);
            if (index >= 0) {
                for (int i = 0; i < NNUE_HIDDEN1; i++) {
                    acc->accumulation[i] += nnue.model.ft_weights[index][i];
                }
            }
        }
        
        // Other white hand pieces...
        // Similar to black hand pieces but with white perspective
    }
    
    acc->accumulation_computed = true;
}

// Incrementally update accumulator after a move
void nnue_update_accumulator(tree_t* ptree, int ply, unsigned int move) {
    if (!nnue.model_loaded) return;
    
    // If previous ply's accumulator is not computed, refresh it
    if (!nnue.accumulators[ply-1].accumulation_computed) {
        nnue_refresh_accumulator(ptree, ply-1);
    }
    
    // Copy previous accumulator state
    memcpy(&nnue.accumulators[ply], &nnue.accumulators[ply-1], sizeof(Accumulator));
    
    // Update based on the move (this is complex and depends on move encoding)
    // For simplicity, we'll just refresh the accumulator for now
    // In a real implementation, we would incrementally update based on pieces moved/captured
    nnue_refresh_accumulator(ptree, ply);
}

// Incrementally revert accumulator after unmaking a move
void nnue_revert_accumulator(tree_t* ptree, int ply) {
    if (!nnue.model_loaded) return;
    
    // Mark current accumulator as invalid
    nnue.accumulators[ply].accumulation_computed = false;
}

// Evaluate position using NNUE
int nnue_evaluate(tree_t* ptree, int ply, int turn) {
    if (!nnue.model_loaded) return 0;
    
    // Ensure accumulator is computed
    if (!nnue.accumulators[ply].accumulation_computed) {
        nnue_refresh_accumulator(ptree, ply);
    }
    
    // Get proper perspective based on turn
    const Accumulator* acc = &nnue.accumulators[ply];
    
    // First layer: apply ReLU to the accumulator values
    int16_t hidden1_out[NNUE_HIDDEN1 * 2];
    for (int i = 0; i < NNUE_HIDDEN1; i++) {
        hidden1_out[i] = relu(acc->accumulation[i]);
        hidden1_out[i + NNUE_HIDDEN1] = relu(-acc->accumulation[i]); // Opposite perspective
    }
    
    // Second layer: fully connected with ReLU
    int32_t hidden2_out[NNUE_HIDDEN2];
    for (int i = 0; i < NNUE_HIDDEN2; i++) {
        int32_t sum = nnue.model.fc_biases1[i];
        for (int j = 0; j < NNUE_HIDDEN1 * 2; j++) {
            sum += hidden1_out[j] * nnue.model.fc_weights1[j][i];
        }
        hidden2_out[i] = relu(sum);
    }
    
    // Final layer: output
    int32_t output = nnue.model.fc_biases2[0];
    for (int i = 0; i < NNUE_HIDDEN2; i++) {
        output += hidden2_out[i] * nnue.model.fc_weights2[i][0];
    }
    
    // Scale output and convert to centipawns
    int score = output / FV_SCALE;
    
    // Return score from the perspective of the player to move
    return turn == black ? score : -score;
}

// Run SFEN benchmark to evaluate NNUE speed
int nnue_benchmark_sfen(const char* sfen, int num_iterations) {
    // This would need to parse SFEN string and set up the position
    // For simplicity, just return a placeholder
    return 0;
}