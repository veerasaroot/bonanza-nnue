#include "usi.h"
#include "nnue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <limits.h>

#if !defined(_WIN32)
#include <unistd.h> /* สำหรับ usleep */
#endif

// ประกาศฟังก์ชัน hash_calc_func
uint64_t hash_calc_func(tree_t* ptree);

// Global variables
static usi_option_t usi_options[32];  // Array to store options
static int num_usi_options = 0;       // Number of registered options
static usi_go_t current_go;           // Current go parameters
static bool usi_quit_flag = false;    // Flag to indicate quit command

// Search thread control
static volatile bool searching = false;
static volatile bool stop_received = false;
static volatile bool ponderhit_received = false;

// Initialize default options
void usi_init(void) {
    // Register engine options
    usi_register_option("USI_Hash", USI_OPT_SPIN, "256", 1, 16384, NULL);
    usi_register_option("USI_Ponder", USI_OPT_CHECK, "true", 0, 0, NULL);
    usi_register_option("Threads", USI_OPT_SPIN, "1", 1, 32, NULL);
    usi_register_option("BookFile", USI_OPT_STRING, "book.bin", 0, 0, NULL);
    usi_register_option("EvalFile", USI_OPT_STRING, "nn.bin", 0, 0, NULL);
    usi_register_option("UseNNUE", USI_OPT_CHECK, "true", 0, 0, NULL);
    
    // Initialize the go parameters
    memset(&current_go, 0, sizeof(usi_go_t));
}

// Process USI commands
int usi_process_command(const char* command) {
    char cmd[SIZE_CMDLINE];
    char args[SIZE_CMDLINE];
    
    // Split command into command name and arguments
    args[0] = '\0';
    if (sscanf(command, "%s %[^\n]", cmd, args) < 1) {
        return USI_CMD_NONE;
    }
    
    // Process command
    if (strcmp(cmd, "usi") == 0) {
        usi_identify();
        return USI_CMD_USI;
    } else if (strcmp(cmd, "isready") == 0) {
        usi_isready();
        return USI_CMD_ISREADY;
    } else if (strcmp(cmd, "setoption") == 0) {
        // Parse 'name [name] value [value]' format
        char option_name[64] = "";
        char option_value[256] = "";
        char *name_start = strstr(args, "name ");
        char *value_start = strstr(args, "value ");
        
        if (name_start) {
            name_start += 5; // Skip "name "
            if (value_start) {
                // Extract name up to value
                int len = value_start - name_start;
                if (len > 0 && len < 64) {
                    strncpy(option_name, name_start, len);
                    option_name[len-1] = '\0'; // Remove trailing space
                }
                
                // Extract value
                value_start += 6; // Skip "value "
                strncpy(option_value, value_start, 255);
                option_value[255] = '\0';
            } else {
                // No value part
                strncpy(option_name, name_start, 63);
                option_name[63] = '\0';
            }
            
            // Trim spaces
            char *p = option_name;
            while (*p && isspace(*p)) p++;
            memmove(option_name, p, strlen(p) + 1);
            
            p = option_name + strlen(option_name) - 1;
            while (p >= option_name && isspace(*p)) *p-- = '\0';
            
            usi_setoption(option_name, option_value);
        }
        return USI_CMD_SETOPTION;
    } else if (strcmp(cmd, "usinewgame") == 0) {
        usi_new_game();
        return USI_CMD_USINEWGAME;
    } else if (strcmp(cmd, "position") == 0) {
        usi_position(args);
        return USI_CMD_POSITION;
    } else if (strcmp(cmd, "go") == 0) {
        usi_go(args);
        return USI_CMD_GO;
    } else if (strcmp(cmd, "stop") == 0) {
        usi_stop();
        return USI_CMD_STOP;
    } else if (strcmp(cmd, "ponderhit") == 0) {
        usi_ponderhit();
        return USI_CMD_PONDERHIT;
    } else if (strcmp(cmd, "quit") == 0) {
        usi_quit();
        return USI_CMD_QUIT;
    } else if (strcmp(cmd, "gameover") == 0) {
        usi_gameover(args);
        return USI_CMD_GAMEOVER;
    } else if (strcmp(cmd, "benchmark") == 0) {
        usi_benchmark(args);
        return USI_CMD_BENCHMARK;
    }
    
    return USI_CMD_NONE;
}

// Output USI identification
void usi_identify(void) {
    printf("id name Bonanza NNUE\n");
    printf("id author Feliz Team (NNUE Modified)\n");
    
    // Output options
    for (int i = 0; i < num_usi_options; i++) {
        switch (usi_options[i].type) {
            case USI_OPT_CHECK:
                printf("option name %s type check default %s\n", 
                    usi_options[i].name, usi_options[i].default_value);
                break;
            case USI_OPT_SPIN:
                printf("option name %s type spin default %s min %d max %d\n", 
                    usi_options[i].name, usi_options[i].default_value,
                    usi_options[i].min_value, usi_options[i].max_value);
                break;
            case USI_OPT_COMBO:
                printf("option name %s type combo default %s", 
                    usi_options[i].name, usi_options[i].default_value);
                for (int j = 0; j < usi_options[i].num_combo_values; j++) {
                    printf(" var %s", usi_options[i].combo_values[j]);
                }
                printf("\n");
                break;
            case USI_OPT_BUTTON:
                printf("option name %s type button\n", usi_options[i].name);
                break;
            case USI_OPT_STRING:
            case USI_OPT_FILENAME:
                printf("option name %s type string default %s\n", 
                    usi_options[i].name, usi_options[i].default_value);
                break;
        }
    }
    
    printf("usiok\n");
}

// Handle isready command
void usi_isready(void) {
    // Load NNUE if enabled and not loaded
    if (usi_options[5].value.check_value && !nnue.model_loaded) {
        char eval_file[256];
        strcpy(eval_file, usi_options[4].value.string_value);
        if (nnue_init(eval_file)) {
            Out("NNUE loaded: %s", eval_file);
        } else {
            Out("Failed to load NNUE: %s", eval_file);
        }
    }
    
    // Set hash size
    int hash_mb = usi_options[0].value.spin_value;
    if (hash_mb > 0) {
        // Calculate hash size in terms of entries
        unsigned int hash_entries = (hash_mb * 1024 * 1024) / sizeof(trans_entry_t);
        
        // Find the largest power of 2 less than or equal to hash_entries
        unsigned int power_of_2 = 1;
        while (power_of_2 * 2 <= hash_entries) {
            power_of_2 *= 2;
        }
        
        // Set hash table size
        // This is a placeholder - actual implementation depends on how Bonanza manages hash tables
        hash_mask = power_of_2 - 1;
        log2_ntrans_table = 0;
        while (power_of_2 > 1) {
            power_of_2 >>= 1;
            log2_ntrans_table++;
        }
        
        // Recreate hash table if needed
        if (ptrans_table_orig) {
            memory_free(ptrans_table_orig);
            ptrans_table_orig = NULL;
        }
        
        ptrans_table_orig = (trans_table_t *)memory_alloc(sizeof(trans_table_t) * (hash_mask + 1));
        if (!ptrans_table_orig) {
            Out("Failed to allocate hash table of size %d MB", hash_mb);
        } else {
            ptrans_table = ptrans_table_orig;
            clear_trans_table();
            Out("Hash table resized to %d MB (%d entries)", hash_mb, hash_mask + 1);
        }
    }
    
    // Set threads (if TLP is enabled)
#if defined(TLP)
    int threads = usi_options[2].value.spin_value;
    tlp_max = threads;
    Out("Threads set to %d", threads);
#endif
    
    // Any other initialization can be done here
    
    printf("readyok\n");
}

// Find option by name
static usi_option_t* find_option(const char* name) {
    for (int i = 0; i < num_usi_options; i++) {
        if (strcmp(usi_options[i].name, name) == 0) {
            return &usi_options[i];
        }
    }
    return NULL;
}

// Handle setoption command
void usi_setoption(const char* name, const char* value) {
    usi_option_t* option = find_option(name);
    if (!option) {
        Out("Unknown option: %s", name);
        return;
    }
    
    option->is_changed = true;
    
    switch (option->type) {
        case USI_OPT_CHECK:
            option->value.check_value = (strcmp(value, "true") == 0);
            Out("Option %s set to %s", name, option->value.check_value ? "true" : "false");
            break;
            
        case USI_OPT_SPIN:
            option->value.spin_value = atoi(value);
            if (option->value.spin_value < option->min_value) {
                option->value.spin_value = option->min_value;
            }
            if (option->value.spin_value > option->max_value) {
                option->value.spin_value = option->max_value;
            }
            Out("Option %s set to %d", name, option->value.spin_value);
            break;
            
        case USI_OPT_COMBO:
            // Find value in combo list
            option->value.combo_index = -1;
            for (int i = 0; i < option->num_combo_values; i++) {
                if (strcmp(option->combo_values[i], value) == 0) {
                    option->value.combo_index = i;
                    break;
                }
            }
            Out("Option %s set to %s", name, 
                option->value.combo_index >= 0 ? option->combo_values[option->value.combo_index] : "invalid");
            break;
            
        case USI_OPT_STRING:
        case USI_OPT_FILENAME:
            strncpy(option->value.string_value, value, 255);
            option->value.string_value[255] = '\0';
            Out("Option %s set to %s", name, option->value.string_value);
            break;
            
        case USI_OPT_BUTTON:
            // Button doesn't have a value, it triggers an action
            Out("Button %s pressed", name);
            break;
    }
}

// Handle usinewgame command
void usi_new_game(void) {
    // Reset game state
    clear_trans_table();
    
    // Initialize tree with starting position
    tree_t* ptree = &tree;
    ini_game(ptree, &min_posi_no_handicap, flag_history, "USI", "USI");
    
    // Reset search parameters
    memset(&current_go, 0, sizeof(usi_go_t));
    
    Out("New game started");
}

// Parse SFEN string into position
int usi_parse_sfen(tree_t* ptree, const char* sfen) {
    int file, rank;
    int sq = 0;
    int piece;
    unsigned int hand_black = 0, hand_white = 0;
    int turn;
    
    // Clear board
    for (int i = 0; i < nsquare; i++) {
        BOARD[i] = empty;
    }
    
    // Parse board position
    while (*sfen && *sfen != ' ') {
        if (*sfen == '/') {
            // Next rank
            sfen++;
            continue;
        } else if (isdigit(*sfen)) {
            // Skip empty squares
            sq += *sfen - '0';
            sfen++;
            continue;
        } else {
            // Parse piece
            piece = empty;
            bool is_promoted = false;
            
            // Check for promotion
            if (*sfen == '+') {
                is_promoted = true;
                sfen++;
            }
            
            // Parse piece type
            switch (toupper(*sfen)) {
                case 'P': piece = pawn; break;
                case 'L': piece = lance; break;
                case 'N': piece = knight; break;
                case 'S': piece = silver; break;
                case 'G': piece = gold; break;
                case 'B': piece = bishop; break;
                case 'R': piece = rook; break;
                case 'K': piece = king; break;
                default:
                    Out("Invalid piece in SFEN: %c", *sfen);
                    return 0;
            }
            
            // Apply promotion
            if (is_promoted) {
                switch (piece) {
                    case pawn: piece = pro_pawn; break;
                    case lance: piece = pro_lance; break;
                    case knight: piece = pro_knight; break;
                    case silver: piece = pro_silver; break;
                    case bishop: piece = horse; break;
                    case rook: piece = dragon; break;
                    default:
                        Out("Invalid promoted piece in SFEN");
                        return 0;
                }
            }
            
            // Apply color
            if (islower(*sfen)) {
                piece = -piece; // White piece (negative)
            }
            
            // Place piece on board
            BOARD[sq++] = piece;
            sfen++;
        }
    }
    
    // Skip space
    if (*sfen) sfen++;
    
    // Parse side to move
    if (*sfen == 'b' || *sfen == 'w') {
        turn = (*sfen == 'b') ? black : white;
        sfen++;
    } else {
        Out("Invalid side to move in SFEN: %c", *sfen);
        return 0;
    }
    
    // Skip space
    if (*sfen) sfen++;
    
    // Parse hand pieces
    if (*sfen == '-') {
        // No hand pieces
        sfen++;
    } else {
        while (*sfen && *sfen != ' ') {
            int count = 1;
            // Check for piece count
            if (isdigit(*sfen)) {
                count = *sfen - '0';
                sfen++;
            }
            
            int piece_type = empty;
            bool is_white = false;
            
            // Parse piece type
            switch (toupper(*sfen)) {
                case 'P': piece_type = pawn; break;
                case 'L': piece_type = lance; break;
                case 'N': piece_type = knight; break;
                case 'S': piece_type = silver; break;
                case 'G': piece_type = gold; break;
                case 'B': piece_type = bishop; break;
                case 'R': piece_type = rook; break;
                default:
                    Out("Invalid hand piece in SFEN: %c", *sfen);
                    return 0;
            }
            
            is_white = islower(*sfen);
            
            // Add to hand
            if (is_white) {
                // Add to white hand
                switch (piece_type) {
                    case pawn: hand_white |= (count << 0); break;
                    case lance: hand_white |= (count << 5); break;
                    case knight: hand_white |= (count << 8); break;
                    case silver: hand_white |= (count << 11); break;
                    case gold: hand_white |= (count << 14); break;
                    case bishop: hand_white |= (count << 17); break;
                    case rook: hand_white |= (count << 19); break;
                }
            } else {
                // Add to black hand
                switch (piece_type) {
                    case pawn: hand_black |= (count << 0); break;
                    case lance: hand_black |= (count << 5); break;
                    case knight: hand_black |= (count << 8); break;
                    case silver: hand_black |= (count << 11); break;
                    case gold: hand_black |= (count << 14); break;
                    case bishop: hand_black |= (count << 17); break;
                    case rook: hand_black |= (count << 19); break;
                }
            }
            
            sfen++;
        }
    }
    
    // Skip space
    if (*sfen) sfen++;
    
    // Parse move count (ignored for now)
    // We can use this if needed for USI protocol extensions
    
    // Set up the position
    HAND_B = hand_black;
    HAND_W = hand_white;
    root_turn = turn;
    
    // Find kings
    for (int i = 0; i < nsquare; i++) {
        if (BOARD[i] == king) {
            SQ_BKING = i;
        } else if (BOARD[i] == -king) {
            SQ_WKING = i;
        }
    }
    
    // Initialize hash key and bitboards
    // This requires a full board scan which Bonanza does in its init functions
    hash_calc_func(ptree);
    
    return 1;
}

// Convert position to SFEN
void usi_position_to_sfen(const tree_t* ptree, char* sfen) {
    int empty_count = 0;
    char* sfen_start = sfen;
    
    // Board position
    for (int rank = 0; rank < nrank; rank++) {
        for (int file = 0; file < nfile; file++) {
            int sq = rank * nfile + file;
            int piece = BOARD[sq];
            
            if (piece == empty) {
                empty_count++;
            } else {
                // Output empty count if needed
                if (empty_count > 0) {
                    *sfen++ = '0' + empty_count;
                    empty_count = 0;
                }
                
                // Output piece
                char piece_char;
                bool promoted = false;
                
                switch (abs(piece)) {
                    case pawn: piece_char = 'P'; break;
                    case lance: piece_char = 'L'; break;
                    case knight: piece_char = 'N'; break;
                    case silver: piece_char = 'S'; break;
                    case gold: piece_char = 'G'; break;
                    case bishop: piece_char = 'B'; break;
                    case rook: piece_char = 'R'; break;
                    case king: piece_char = 'K'; break;
                    case pro_pawn: piece_char = 'P'; promoted = true; break;
                    case pro_lance: piece_char = 'L'; promoted = true; break;
                    case pro_knight: piece_char = 'N'; promoted = true; break;
                    case pro_silver: piece_char = 'S'; promoted = true; break;
                    case horse: piece_char = 'B'; promoted = true; break;
                    case dragon: piece_char = 'R'; promoted = true; break;
                    default: piece_char = '?'; break;
                }
                
                if (promoted) {
                    *sfen++ = '+';
                }
                
                if (piece < 0) {
                    // White piece (lowercase)
                    *sfen++ = tolower(piece_char);
                } else {
                    // Black piece (uppercase)
                    *sfen++ = piece_char;
                }
            }
        }
        
        // Output empty count at end of rank
        if (empty_count > 0) {
            *sfen++ = '0' + empty_count;
            empty_count = 0;
        }
        
        // Add separator between ranks
        if (rank < nrank - 1) {
            *sfen++ = '/';
        }
    }
    
    // Side to move
    *sfen++ = ' ';
    *sfen++ = (root_turn == black) ? 'b' : 'w';
    
    // Hand pieces
    *sfen++ = ' ';
    
    unsigned int hand_b = HAND_B;
    unsigned int hand_w = HAND_W;
    
    if (hand_b == 0 && hand_w == 0) {
        *sfen++ = '-';
    } else {
        // Function to add hand pieces
        const char* piece_chars = "PLNSGBRplnsgbr";
        int offsets[] = {0, 5, 8, 11, 14, 17, 19};
        int piece_types[] = {pawn, lance, knight, silver, gold, bishop, rook};
        
        // Add black's hand pieces
        for (int i = 0; i < 7; i++) {
            int count = (hand_b >> offsets[i]) & ((i == 0) ? 0x1F : 0x07);
            if (count > 0) {
                if (count > 1) {
                    *sfen++ = '0' + count;
                }
                *sfen++ = piece_chars[i];
            }
        }
        
        // Add white's hand pieces
        for (int i = 0; i < 7; i++) {
            int count = (hand_w >> offsets[i]) & ((i == 0) ? 0x1F : 0x07);
            if (count > 0) {
                if (count > 1) {
                    *sfen++ = '0' + count;
                }
                *sfen++ = piece_chars[i + 7];
            }
        }
    }
    
    // Move count (1 by default)
    *sfen++ = ' ';
    *sfen++ = '1';
    *sfen = '\0';
}

// Handle position command
void usi_position(const char* args) {
    tree_t* ptree = &tree;
    char sfen_str[512];
    char moves_str[SIZE_CMDLINE];
    moves_str[0] = '\0';
    
    if (strncmp(args, "startpos", 8) == 0) {
        // Use default starting position
        ini_game(ptree, &min_posi_no_handicap, flag_history, "USI", "USI");
        
        // Check for moves
        if (strstr(args, "moves")) {
            strcpy(moves_str, strstr(args, "moves") + 6);
        }
    } else if (strncmp(args, "sfen", 4) == 0) {
        // Extract SFEN string
        const char* sfen_start = args + 5; // Skip "sfen "
        const char* moves_start = strstr(args, "moves");
        
        if (moves_start) {
            int sfen_len = moves_start - sfen_start;
            strncpy(sfen_str, sfen_start, sfen_len);
            sfen_str[sfen_len] = '\0';
            
            // Extract moves
            strcpy(moves_str, moves_start + 6);
        } else {
            strcpy(sfen_str, sfen_start);
        }
        
        // Parse SFEN and set up position
        usi_parse_sfen(ptree, sfen_str);
    } else {
        Out("Invalid position command: %s", args);
        return;
    }
    
    // Apply moves if provided
    if (moves_str[0]) {
        char move_str[10];
        char* token = strtok(moves_str, " \t\n");
        
        while (token) {
            strcpy(move_str, token);
            unsigned int move = usi_move_to_internal(move_str);
            
            if (move) {
                // Make the move on the board
                make_move_root(ptree, move, flag_time_extendable);
            } else {
                Out("Invalid move in position command: %s", move_str);
                break;
            }
            
            token = strtok(NULL, " \t\n");
        }
    }
    
    Out("Position set up successfully");
}

// Parse USI go command parameters
void parse_go_params(const char* args, usi_go_t* go_params) {
    char* token;
    char args_copy[SIZE_CMDLINE];
    
    // Initialize go parameters
    memset(go_params, 0, sizeof(usi_go_t));
    
    // Create a copy of args for tokenization
    strcpy(args_copy, args);
    
    token = strtok(args_copy, " \t\n");
    while (token) {
        if (strcmp(token, "infinite") == 0) {
            go_params->infinite = true;
        } else if (strcmp(token, "ponder") == 0) {
            go_params->ponder = true;
        } else if (strcmp(token, "bytime") == 0) {
            token = strtok(NULL, " \t\n");
            if (token) {
                go_params->black_time.time_ms = atoi(token);
            }
        } else if (strcmp(token, "wtime") == 0) {
            token = strtok(NULL, " \t\n");
            if (token) {
                go_params->white_time.time_ms = atoi(token);
            }
        } else if (strcmp(token, "byoyomi") == 0) {
            token = strtok(NULL, " \t\n");
            if (token) {
                go_params->black_time.byoyomi_ms = atoi(token);
                go_params->white_time.byoyomi_ms = atoi(token);
            }
        } else if (strcmp(token, "binc") == 0) {
            token = strtok(NULL, " \t\n");
            if (token) {
                go_params->black_time.inc_ms = atoi(token);
            }
        } else if (strcmp(token, "winc") == 0) {
            token = strtok(NULL, " \t\n");
            if (token) {
                go_params->white_time.inc_ms = atoi(token);
            }
        } else if (strcmp(token, "depth") == 0) {
            token = strtok(NULL, " \t\n");
            if (token) {
                go_params->depth = atoi(token);
            }
        } else if (strcmp(token, "nodes") == 0) {
            token = strtok(NULL, " \t\n");
            if (token) {
                go_params->nodes = atoi(token);
            }
        } else if (strcmp(token, "mate") == 0) {
            token = strtok(NULL, " \t\n");
            if (token) {
                go_params->mate = atoi(token);
            }
        } else if (strcmp(token, "movetime") == 0) {
            token = strtok(NULL, " \t\n");
            if (token) {
                go_params->movetime = atoi(token);
            }
        } else if (strcmp(token, "movestogo") == 0) {
            token = strtok(NULL, " \t\n");
            if (token) {
                go_params->black_time.moves_to_go = atoi(token);
                go_params->white_time.moves_to_go = atoi(token);
            }
        } else if (strcmp(token, "searchmoves") == 0) {
            go_params->searchmoves_specified = true;
            go_params->num_searchmoves = 0;
            
            while ((token = strtok(NULL, " \t\n")) != NULL) {
                if (token[0] < 'a' || token[0] > 'i' || !isdigit(token[1])) {
                    // Not a move, must be next parameter
                    break;
                }
                
                unsigned int move = usi_move_to_internal(token);
                if (move && go_params->num_searchmoves < MAX_LEGAL_MOVES) {
                    go_params->searchmoves[go_params->num_searchmoves++] = move;
                }
            }
            
            continue; // Skip token increment at end of loop
        }
        
        token = strtok(NULL, " \t\n");
    }
}

// Handle go command
void usi_go(const char* args) {
    tree_t* ptree = &tree;
    
    // Stop any ongoing search
    if (searching) {
        usi_stop();
        // Wait for search to complete
        while (searching) {
            // Small delay
#if defined(_WIN32)
            Sleep(10);
#else
            usleep(10000);
#endif
        }
    }
    
    // Parse go parameters
    parse_go_params(args, &current_go);
    
    // Generate root moves
    make_root_move_list(ptree, current_go.searchmoves_specified ? 
        (flag_rejections | flag_history) : (flag_history));
    
    // Filter moves if searchmoves is specified
    if (current_go.searchmoves_specified && current_go.num_searchmoves > 0) {
        int filtered_count = 0;
        
        for (int i = 0; i < root_nmove; i++) {
            bool keep = false;
            
            for (int j = 0; j < current_go.num_searchmoves; j++) {
                if (root_move_list[i].move == current_go.searchmoves[j]) {
                    keep = true;
                    break;
                }
            }
            
            if (keep) {
                root_move_list[filtered_count++] = root_move_list[i];
            }
        }
        
        root_nmove = filtered_count;
    }
    
    // Configure time control
    if (current_go.infinite || current_go.ponder) {
        // No time limit
        time_limit = UINT_MAX;
        sec_limit = UINT_MAX;
    } else if (current_go.movetime > 0) {
        // Fixed time per move
        time_limit = current_go.movetime;
        sec_limit = (time_limit / 1000) + 1;
        time_max_limit = time_limit + 10; // Small buffer
    } else {
        // Calculate time based on time control
        usi_time_control_t* tc = (root_turn == black) ? 
                                &current_go.black_time : &current_go.white_time;
        
        unsigned int time_available = tc->time_ms;
        unsigned int byoyomi = tc->byoyomi_ms;
        unsigned int increment = tc->inc_ms;
        int moves_to_go = tc->moves_to_go;
        
        // Default strategy: use 1/20 of available time plus byoyomi/increment
        unsigned int time_to_use;
        
        if (moves_to_go > 0) {
            // If moves_to_go is specified, divide remaining time
            time_to_use = time_available / moves_to_go;
        } else {
            // Otherwise use a fraction of available time
            // More sophisticated time management would be implemented here
            time_to_use = time_available / 20;
        }
        
        // Add byoyomi/increment
        time_to_use += byoyomi;
        
        // Set limits
        time_limit = time_to_use;
        sec_limit = (time_limit / 1000) + 1;
        
        // Maximum time (for safety)
        if (byoyomi > 0) {
            // With byoyomi, we can use all available time plus byoyomi
            time_max_limit = time_available + byoyomi - 100; // 100ms safety buffer
        } else {
            // Without byoyomi, be more conservative
            time_max_limit = time_available / 4;
        }
        
        // Ensure we don't exceed available time
        if (time_max_limit > time_available) {
            time_max_limit = time_available - 100; // 100ms safety buffer
        }
    }
    
    // Set depth limit
    if (current_go.depth > 0) {
        depth_limit = current_go.depth * PLY_INC;
    } else {
        depth_limit = PLY_MAX * PLY_INC;
    }
    
    // Set node limit
    if (current_go.nodes > 0) {
        node_limit = current_go.nodes;
    } else {
        node_limit = UINT64_MAX;
    }
    
    // Reset search abort flags
    root_abort = 0;
    stop_received = false;
    ponderhit_received = false;
    
    // Set ponder flag
    if (current_go.ponder) {
        game_status |= flag_pondering;
    } else {
        game_status &= ~flag_pondering;
    }
    
    // Start search
    searching = true;
    
    // Initialize timekeeping
    get_elapsed(&time_turn_start);
    time_start = time_turn_start;
    
    // Special mate search if requested
    if (current_go.mate > 0) {
        // TO-DO: Implement mate search
        // For now, just run normal search
        Out("Mate search not implemented, running normal search");
    }
    
    // Run iterative deepening search
    iterate(ptree, flag_time | flag_history | flag_rep | flag_detect_hang | flag_rejections);
    
    // Handle search completion (output best move)
    if (!(game_status & (flag_move_now | flag_quit | flag_search_error))) {
        unsigned int move;
        unsigned int ponder_move = 0;
        
        // Determine best move
        if (root_nmove > 0) {
            move = root_move_list[0].move;
            
            // Get ponder move (if enabled)
            if (current_go.ponder || usi_options[1].value.check_value) {
                // Make the best move
                if (make_move_root(ptree, move, flag_time_extendable)) {
                    // Generate opponent's response moves
                    make_root_move_list(ptree, flag_history);
                    
                    if (root_nmove > 0) {
                        ponder_move = root_move_list[0].move;
                    }
                    
                    // Unmake the move
                    unmake_move_root(ptree, move);
                }
            }
        } else {
            move = MOVE_RESIGN;
        }
        
        // Report best move to GUI
        usi_report_bestmove(move, ponder_move);
    }
    
    // Reset search state
    searching = false;
    game_status &= ~flag_pondering;
}

// Handle stop command
void usi_stop(void) {
    if (searching) {
        // Set abort flags
        root_abort = 1;
        stop_received = true;
        
        // If in ponder mode, clear ponder flag
        if (game_status & flag_pondering) {
            game_status &= ~flag_pondering;
        }
        
        Out("Search stopped");
    }
}

// Handle ponderhit command
void usi_ponderhit(void) {
    if (searching && (game_status & flag_pondering)) {
        // Switch from pondering to normal search
        game_status &= ~flag_pondering;
        ponderhit_received = true;
        
        // Reset time control based on the current go parameters
        get_elapsed(&time_turn_start);
        
        Out("Ponderhit received");
    }
}

// Handle quit command
void usi_quit(void) {
    // Stop any ongoing search
    if (searching) {
        usi_stop();
    }
    
    // Set quit flag
    usi_quit_flag = true;
    game_status |= flag_quit;
    
    Out("Quit command received");
}

// Handle gameover command
void usi_gameover(const char* result) {
    // Stop any ongoing search
    if (searching) {
        usi_stop();
    }
    
    // Process result
    if (strcmp(result, "win") == 0) {
        Out("Game over: win");
    } else if (strcmp(result, "lose") == 0) {
        Out("Game over: lose");
    } else if (strcmp(result, "draw") == 0) {
        Out("Game over: draw");
    } else {
        Out("Game over: %s", result);
    }
}

// Register USI option
void usi_register_option(const char* name, usi_option_type_t type, 
                         const char* default_value, int min_value, 
                         int max_value, const char** combo_values) {
    if (num_usi_options >= 32) {
        Out("Too many USI options");
        return;
    }
    
    usi_option_t* option = &usi_options[num_usi_options++];
    strncpy(option->name, name, 63);
    option->name[63] = '\0';
    option->type = type;
    
    if (default_value) {
        strncpy(option->default_value, default_value, 255);
        option->default_value[255] = '\0';
    } else {
        option->default_value[0] = '\0';
    }
    
    option->min_value = min_value;
    option->max_value = max_value;
    option->is_changed = false;
    
    // Set initial value based on type
    switch (type) {
        case USI_OPT_CHECK:
            option->value.check_value = (strcmp(default_value, "true") == 0);
            break;
            
        case USI_OPT_SPIN:
            option->value.spin_value = default_value ? atoi(default_value) : 0;
            if (option->value.spin_value < min_value) {
                option->value.spin_value = min_value;
            }
            if (option->value.spin_value > max_value) {
                option->value.spin_value = max_value;
            }
            break;
            
        case USI_OPT_COMBO:
            option->num_combo_values = 0;
            if (combo_values) {
                for (int i = 0; combo_values[i] && i < 10; i++) {
                    strncpy(option->combo_values[i], combo_values[i], 63);
                    option->combo_values[i][63] = '\0';
                    option->num_combo_values++;
                    
                    // Set default index
                    if (strcmp(default_value, combo_values[i]) == 0) {
                        option->value.combo_index = i;
                    }
                }
            }
            break;
            
        case USI_OPT_STRING:
        case USI_OPT_FILENAME:
            if (default_value) {
                strncpy(option->value.string_value, default_value, 255);
                option->value.string_value[255] = '\0';
            } else {
                option->value.string_value[0] = '\0';
            }
            break;
            
        case USI_OPT_BUTTON:
            // Button doesn't have a value
            break;
    }
}

// Convert USI move notation to internal move format
unsigned int usi_move_to_internal(const char* move_str) {
    if (!move_str || strlen(move_str) < 4) {
        return 0;
    }
    
    // Check for special moves
    if (strcmp(move_str, "resign") == 0) {
        return MOVE_RESIGN;
    }
    
    if (strcmp(move_str, "pass") == 0) {
        return MOVE_PASS;
    }
    
    // Parse normal move
    int from_file = move_str[0] - 'a' + 1;
    int from_rank = move_str[1] - '1' + 1;
    int to_file = move_str[2] - 'a' + 1;
    int to_rank = move_str[3] - '1' + 1;
    
    // Convert to square indices
    int from_sq = (from_rank - 1) * nfile + (from_file - 1);
    int to_sq = (to_rank - 1) * nfile + (to_file - 1);
    
    // Check for drop move (from square is beyond board)
    if (from_file == 0 || from_rank == 0) {
        // Drop move
        int piece_type = 0;
        
        // Determine piece type
        switch (move_str[0]) {
            case 'P': piece_type = pawn; break;
            case 'L': piece_type = lance; break;
            case 'N': piece_type = knight; break;
            case 'S': piece_type = silver; break;
            case 'G': piece_type = gold; break;
            case 'B': piece_type = bishop; break;
            case 'R': piece_type = rook; break;
            default: return 0; // Invalid piece
        }
        
        // Create drop move
        return Drop2Move(piece_type) | To2Move(to_sq) | Piece2Move(piece_type);
    }
    
    // Regular move
    bool is_promote = (strlen(move_str) >= 5 && move_str[4] == '+');
    
    // Get piece type and captured piece
    tree_t *ptree = &tree; // ใช้ global tree
    int piece_type = abs(BOARD[from_sq]);
    int captured = BOARD[to_sq];
    
    // Build move
    unsigned int move = From2Move(from_sq) | To2Move(to_sq) | Piece2Move(piece_type);
    
    // Add promotion flag if needed
    if (is_promote) {
        move |= FLAG_PROMO;
    }
    
    // Add captured piece info
    if (captured != empty) {
        move |= Cap2Move(abs(captured));
    }
    
    return move;
}

// Convert internal move format to USI notation
void usi_move_to_string(unsigned int move, char* buf) {
    if (!buf) return;
    
    // Check for special moves
    if (move == MOVE_RESIGN) {
        strcpy(buf, "resign");
        return;
    }
    
    if (move == MOVE_PASS) {
        strcpy(buf, "pass");
        return;
    }
    
    int from = I2From(move);
    int to = I2To(move);
    bool is_promote = I2IsPromote(move);
    
    // Check if it's a drop move
    if (from >= nsquare) {
        // Drop move
        int piece_type = From2Drop(from);
        char piece_char = '?';
        
        // Convert piece type to character
        switch (piece_type) {
            case pawn: piece_char = 'P'; break;
            case lance: piece_char = 'L'; break;
            case knight: piece_char = 'N'; break;
            case silver: piece_char = 'S'; break;
            case gold: piece_char = 'G'; break;
            case bishop: piece_char = 'B'; break;
            case rook: piece_char = 'R'; break;
        }
        
        // Destination coordinates
        int to_file = aifile[to] + 'a' - 1;
        int to_rank = airank[to] + '1' - 1;
        
        // Format: [piece]*[to_square]
        sprintf(buf, "%c*%c%c", piece_char, to_file, to_rank);
    } else {
        // Regular move
        int from_file = aifile[from] + 'a' - 1;
        int from_rank = airank[from] + '1' - 1;
        int to_file = aifile[to] + 'a' - 1;
        int to_rank = airank[to] + '1' - 1;
        
        // Format: [from_square][to_square][+]
        sprintf(buf, "%c%c%c%c%s", from_file, from_rank, to_file, to_rank, is_promote ? "+" : "");
    }
}

// Run benchmark
void usi_benchmark(const char* args) {
    char sfen[256] = "";
    int num_iterations = 1;
    
    // Parse arguments
    if (sscanf(args, "%255s %d", sfen, &num_iterations) < 1) {
        // Use default SFEN
        strcpy(sfen, "lnsgkgsnl/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL b - 1");
    }
    
    // Run benchmark
    printf("Running benchmark on SFEN: %s\n", sfen);
    printf("Iterations: %d\n", num_iterations);
    
    // Measure time
    unsigned int start_time;
    get_elapsed(&start_time);
    
    // Run NNUE benchmark
    int result = nnue_benchmark_sfen(sfen, num_iterations);
    
    // Calculate elapsed time
    unsigned int end_time;
    get_elapsed(&end_time);
    unsigned int elapsed = end_time - start_time;
    
    printf("Benchmark complete\n");
    printf("Elapsed time: %u ms\n", elapsed);
    printf("Score: %d\n", result);
}

// Main loop for USI mode
void usi_loop(void) {
    char cmd[SIZE_CMDLINE];
    
    // Initialize USI
    usi_init();
    
    // Main command loop
    while (!usi_quit_flag) {
        if (next_cmdline(1) == 0) {
            break;  // End of input
        }
        
        strncpy(cmd, str_cmdline, SIZE_CMDLINE-1);
        cmd[SIZE_CMDLINE-1] = '\0';
        
        usi_process_command(cmd);
    }
}

// Report best move to GUI
void usi_report_bestmove(unsigned int best_move, unsigned int ponder_move) {
    char best_move_str[10];
    char ponder_move_str[10];
    
    if (best_move == MOVE_RESIGN) {
        strcpy(best_move_str, "resign");
    } else if (best_move == MOVE_PASS) {
        strcpy(best_move_str, "pass");
    } else {
        usi_move_to_string(best_move, best_move_str);
    }
    
    if (ponder_move) {
        usi_move_to_string(ponder_move, ponder_move_str);
        printf("bestmove %s ponder %s\n", best_move_str, ponder_move_str);
    } else {
        printf("bestmove %s\n", best_move_str);
    }
}

// Report search info to GUI
void usi_report_info(int depth, int seldepth, int score, int nodes, 
                    unsigned int time_ms, unsigned int nps, int multipv, 
                    const char* pv) {
    printf("info depth %d seldepth %d score cp %d nodes %d time %u nps %u multipv %d pv %s\n", 
           depth, seldepth, score, nodes, time_ms, nps, multipv, pv);
}

// Helper to calculate board hash
uint64_t hash_calc_func(tree_t* ptree) {
    uint64_t hash = 0;
    
    // Reset bit boards
    BBIni(BB_BOCCUPY);
    BBIni(BB_WOCCUPY);
    BBIni(BB_BTGOLD);
    BBIni(BB_WTGOLD);
    BBIni(BB_BPAWN);
    BBIni(BB_WPAWN);
    BBIni(BB_BLANCE);
    BBIni(BB_WLANCE);
    BBIni(BB_BKNIGHT);
    BBIni(BB_WKNIGHT);
    BBIni(BB_BSILVER);
    BBIni(BB_WSILVER);
    BBIni(BB_BGOLD);
    BBIni(BB_WGOLD);
    BBIni(BB_BBISHOP);
    BBIni(BB_WBISHOP);
    BBIni(BB_BROOK);
    BBIni(BB_WROOK);
    BBIni(BB_BPRO_PAWN);
    BBIni(BB_WPRO_PAWN);
    BBIni(BB_BPRO_LANCE);
    BBIni(BB_WPRO_LANCE);
    BBIni(BB_BPRO_KNIGHT);
    BBIni(BB_WPRO_KNIGHT);
    BBIni(BB_BPRO_SILVER);
    BBIni(BB_WPRO_SILVER);
    BBIni(BB_BHORSE);
    BBIni(BB_WHORSE);
    BBIni(BB_BDRAGON);
    BBIni(BB_WDRAGON);
    BBIni(BB_B_HDK);
    BBIni(BB_W_HDK);
    BBIni(BB_B_BH);
    BBIni(BB_W_BH);
    BBIni(BB_B_RD);
    BBIni(BB_W_RD);
    BBIni(BB_BPAWN_ATK);
    BBIni(BB_WPAWN_ATK);
    BBIni(OCCUPIED_FILE);
    BBIni(OCCUPIED_DIAG1);
    BBIni(OCCUPIED_DIAG2);
    
    // Populate bit boards and calculate hash
    for (int sq = 0; sq < nsquare; sq++) {
        int piece = BOARD[sq];
        if (piece == empty) continue;
        
        switch (piece) {
        case pawn:
            hash ^= b_pawn_rand[sq];
            Xor(sq, BB_BPAWN);
            Xor(sq, BB_BOCCUPY);
            if (sq + 9 < nsquare) Xor(sq + 9, BB_BPAWN_ATK);
            break;
        case lance:
            hash ^= b_lance_rand[sq];
            Xor(sq, BB_BLANCE);
            Xor(sq, BB_BOCCUPY);
            break;
        case knight:
            hash ^= b_knight_rand[sq];
            Xor(sq, BB_BKNIGHT);
            Xor(sq, BB_BOCCUPY);
            break;
        case silver:
            hash ^= b_silver_rand[sq];
            Xor(sq, BB_BSILVER);
            Xor(sq, BB_BOCCUPY);
            break;
        case gold:
            hash ^= b_gold_rand[sq];
            Xor(sq, BB_BGOLD);
            Xor(sq, BB_BTGOLD);
            Xor(sq, BB_BOCCUPY);
            break;
        case bishop:
            hash ^= b_bishop_rand[sq];
            Xor(sq, BB_BBISHOP);
            Xor(sq, BB_B_BH);
            Xor(sq, BB_BOCCUPY);
            break;
        case rook:
            hash ^= b_rook_rand[sq];
            Xor(sq, BB_BROOK);
            Xor(sq, BB_B_RD);
            Xor(sq, BB_BOCCUPY);
            break;
        case king:
            hash ^= b_king_rand[sq];
            SQ_BKING = sq;
            Xor(sq, BB_B_HDK);
            Xor(sq, BB_BOCCUPY);
            break;
        case pro_pawn:
            hash ^= b_pro_pawn_rand[sq];
            Xor(sq, BB_BPRO_PAWN);
            Xor(sq, BB_BTGOLD);
            Xor(sq, BB_BOCCUPY);
            break;
        case pro_lance:
            hash ^= b_pro_lance_rand[sq];
            Xor(sq, BB_BPRO_LANCE);
            Xor(sq, BB_BTGOLD);
            Xor(sq, BB_BOCCUPY);
            break;
        case pro_knight:
            hash ^= b_pro_knight_rand[sq];
            Xor(sq, BB_BPRO_KNIGHT);
            Xor(sq, BB_BTGOLD);
            Xor(sq, BB_BOCCUPY);
            break;
        case pro_silver:
            hash ^= b_pro_silver_rand[sq];
            Xor(sq, BB_BPRO_SILVER);
            Xor(sq, BB_BTGOLD);
            Xor(sq, BB_BOCCUPY);
            break;
        case horse:
            hash ^= b_horse_rand[sq];
            Xor(sq, BB_BHORSE);
            Xor(sq, BB_B_BH);
            Xor(sq, BB_B_HDK);
            Xor(sq, BB_BOCCUPY);
            break;
        case dragon:
            hash ^= b_dragon_rand[sq];
            Xor(sq, BB_BDRAGON);
            Xor(sq, BB_B_RD);
            Xor(sq, BB_B_HDK);
            Xor(sq, BB_BOCCUPY);
            break;
            
        case -pawn:
            hash ^= w_pawn_rand[sq];
            Xor(sq, BB_WPAWN);
            Xor(sq, BB_WOCCUPY);
            if (sq - 9 >= 0) Xor(sq - 9, BB_WPAWN_ATK);
            break;
        case -lance:
            hash ^= w_lance_rand[sq];
            Xor(sq, BB_WLANCE);
            Xor(sq, BB_WOCCUPY);
            break;
        case -knight:
            hash ^= w_knight_rand[sq];
            Xor(sq, BB_WKNIGHT);
            Xor(sq, BB_WOCCUPY);
            break;
        case -silver:
            hash ^= w_silver_rand[sq];
            Xor(sq, BB_WSILVER);
            Xor(sq, BB_WOCCUPY);
            break;
        case -gold:
            hash ^= w_gold_rand[sq];
            Xor(sq, BB_WGOLD);
            Xor(sq, BB_WTGOLD);
            Xor(sq, BB_WOCCUPY);
            break;
        case -bishop:
            hash ^= w_bishop_rand[sq];
            Xor(sq, BB_WBISHOP);
            Xor(sq, BB_W_BH);
            Xor(sq, BB_WOCCUPY);
            break;
        case -rook:
            hash ^= w_rook_rand[sq];
            Xor(sq, BB_WROOK);
            Xor(sq, BB_W_RD);
            Xor(sq, BB_WOCCUPY);
            break;
        case -king:
            hash ^= w_king_rand[sq];
            SQ_WKING = sq;
            Xor(sq, BB_W_HDK);
            Xor(sq, BB_WOCCUPY);
            break;
        case -pro_pawn:
            hash ^= w_pro_pawn_rand[sq];
            Xor(sq, BB_WPRO_PAWN);
            Xor(sq, BB_WTGOLD);
            Xor(sq, BB_WOCCUPY);
            break;
        case -pro_lance:
            hash ^= w_pro_lance_rand[sq];
            Xor(sq, BB_WPRO_LANCE);
            Xor(sq, BB_WTGOLD);
            Xor(sq, BB_WOCCUPY);
            break;
        case -pro_knight:
            hash ^= w_pro_knight_rand[sq];
            Xor(sq, BB_WPRO_KNIGHT);
            Xor(sq, BB_WTGOLD);
            Xor(sq, BB_WOCCUPY);
            break;
        case -pro_silver:
            hash ^= w_pro_silver_rand[sq];
            Xor(sq, BB_WPRO_SILVER);
            Xor(sq, BB_WTGOLD);
            Xor(sq, BB_WOCCUPY);
            break;
        case -horse:
            hash ^= w_horse_rand[sq];
            Xor(sq, BB_WHORSE);
            Xor(sq, BB_W_BH);
            Xor(sq, BB_W_HDK);
            Xor(sq, BB_WOCCUPY);
            break;
        case -dragon:
            hash ^= w_dragon_rand[sq];
            Xor(sq, BB_WDRAGON);
            Xor(sq, BB_W_RD);
            Xor(sq, BB_W_HDK);
            Xor(sq, BB_WOCCUPY);
            break;
        }
        
        // Update occupied bitboards
        int sq_file = aifile[sq] - 1;
        int sq_rank = airank[sq] - 1;
        int sq_diag1 = 8 - sq_file + sq_rank;
        int sq_diag2 = sq_file + sq_rank;
        
        Xor(sq_file * 9 + sq_rank, OCCUPIED_FILE);
        if (sq_diag1 >= 0 && sq_diag1 < 17) Xor(sq_diag1, OCCUPIED_DIAG1);
        if (sq_diag2 >= 0 && sq_diag2 < 17) Xor(sq_diag2, OCCUPIED_DIAG2);
    }
    
    // Process hands
    unsigned int hand_black = HAND_B;
    unsigned int hand_white = HAND_W;
    
    if (hand_black) {
        int count;
        
        count = I2HandPawn(hand_black);
        if (count > 0) hash ^= b_hand_pawn_rand[count-1];
        
        count = I2HandLance(hand_black);
        if (count > 0) hash ^= b_hand_lance_rand[count-1];
        
        count = I2HandKnight(hand_black);
        if (count > 0) hash ^= b_hand_knight_rand[count-1];
        
        count = I2HandSilver(hand_black);
        if (count > 0) hash ^= b_hand_silver_rand[count-1];
        
        count = I2HandGold(hand_black);
        if (count > 0) hash ^= b_hand_gold_rand[count-1];
        
        count = I2HandBishop(hand_black);
        if (count > 0) hash ^= b_hand_bishop_rand[count-1];
        
        count = I2HandRook(hand_black);
        if (count > 0) hash ^= b_hand_rook_rand[count-1];
    }
    
    if (hand_white) {
        int count;
        
        count = I2HandPawn(hand_white);
        if (count > 0) hash ^= w_hand_pawn_rand[count-1];
        
        count = I2HandLance(hand_white);
        if (count > 0) hash ^= w_hand_lance_rand[count-1];
        
        count = I2HandKnight(hand_white);
        if (count > 0) hash ^= w_hand_knight_rand[count-1];
        
        count = I2HandSilver(hand_white);
        if (count > 0) hash ^= w_hand_silver_rand[count-1];
        
        count = I2HandGold(hand_white);
        if (count > 0) hash ^= w_hand_gold_rand[count-1];
        
        count = I2HandBishop(hand_white);
        if (count > 0) hash ^= w_hand_bishop_rand[count-1];
        
        count = I2HandRook(hand_white);
        if (count > 0) hash ^= w_hand_rook_rand[count-1];
    }
    
    // Store hash key
    HASH_KEY = hash;
    
    return hash;
}