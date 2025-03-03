#ifndef USI_H
#define USI_H

#include "shogi.h"
#include <stdbool.h>

// USI command types
#define USI_CMD_NONE         0
#define USI_CMD_USI          1
#define USI_CMD_ISREADY      2
#define USI_CMD_SETOPTION    3
#define USI_CMD_USINEWGAME   4
#define USI_CMD_POSITION     5
#define USI_CMD_GO           6
#define USI_CMD_STOP         7
#define USI_CMD_PONDERHIT    8
#define USI_CMD_QUIT         9
#define USI_CMD_GAMEOVER     10
#define USI_CMD_BENCHMARK    11

// USI option types
typedef enum {
    USI_OPT_CHECK,
    USI_OPT_SPIN,
    USI_OPT_COMBO,
    USI_OPT_BUTTON,
    USI_OPT_STRING,
    USI_OPT_FILENAME
} usi_option_type_t;

// USI option data
typedef struct {
    char name[64];
    usi_option_type_t type;
    char default_value[256];
    int min_value;
    int max_value;
    char combo_values[10][64]; // Up to 10 combo values
    int num_combo_values;
    bool is_changed;
    union {
        bool check_value;
        int spin_value;
        int combo_index;
        char string_value[256];
    } value;
} usi_option_t;

// USI time controls
typedef struct {
    unsigned int time_ms;        // Time left in milliseconds
    unsigned int byoyomi_ms;     // Byoyomi time in milliseconds
    unsigned int inc_ms;         // Increment per move in milliseconds
    int moves_to_go;             // Moves left until next time control
} usi_time_control_t;

// USI go parameters
typedef struct {
    bool infinite;
    bool ponder;
    unsigned int nodes;
    unsigned int depth;
    unsigned int mate;
    unsigned int movetime;
    usi_time_control_t black_time;
    usi_time_control_t white_time;
    bool searchmoves_specified;
    unsigned int searchmoves[MAX_LEGAL_MOVES];
    int num_searchmoves;
} usi_go_t;

// Initialize USI engine
void usi_init(void);

// Process USI commands
int usi_process_command(const char* command);

// Output USI identification
void usi_identify(void);

// Handle isready command
void usi_isready(void);

// Handle setoption command
void usi_setoption(const char* name, const char* value);

// Handle usinewgame command
void usi_new_game(void);

// Handle position command
void usi_position(const char* args);

// Handle go command
void usi_go(const char* args);

// Handle stop command
void usi_stop(void);

// Handle ponderhit command
void usi_ponderhit(void);

// Handle quit command
void usi_quit(void);

// Handle gameover command
void usi_gameover(const char* result);

// Run benchmark
void usi_benchmark(const char* args);

// Convert USI move notation to internal move format
unsigned int usi_move_to_internal(const char* move_str);

// Convert internal move format to USI notation
void usi_move_to_string(unsigned int move, char* buf);

// Parse SFEN string into position
int usi_parse_sfen(tree_t* ptree, const char* sfen);

// Export position to SFEN string
void usi_position_to_sfen(const tree_t* ptree, char* sfen);

// Register USI option
void usi_register_option(const char* name, usi_option_type_t type, 
                         const char* default_value, int min_value, 
                         int max_value, const char** combo_values);

// Main loop for USI mode
void usi_loop(void);

// Report best move to GUI
void usi_report_bestmove(unsigned int best_move, unsigned int ponder_move);

// Report search info to GUI
void usi_report_info(int depth, int seldepth, int score, int nodes, 
                    unsigned int time_ms, unsigned int nps, int multipv, 
                    const char* pv);

#endif // USI_H