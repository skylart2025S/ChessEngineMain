#ifndef CHESS_H
#define CHESS_H

#include <stdint.h>

// ═══════════════════════════════════════════════════════════════
//  CONSTANTS
// ═══════════════════════════════════════════════════════════════

#define EMPTY  -1
#define WHITE   0
#define BLACK   1

#define PAWN    0
#define KNIGHT  1
#define BISHOP  2
#define ROOK    3
#define QUEEN   4
#define KING    5

#define MOVE_FLAG_EN_PASSANT 0x1u
#define MOVE_FLAG_CASTLE     0x2u
#define MOVE_FLAG_PROMOTION  0x4u

// ═══════════════════════════════════════════════════════════════
//  TYPES
// ═══════════════════════════════════════════════════════════════

struct Bitboard {
    uint64_t color[2];
    uint64_t piece[6];
};

struct Attacks {
    uint64_t piece[6];
    uint64_t all;
};

typedef struct {
    int from;
    int to;
    int piece;
    int color;
    int captured_piece;
    int captured_color;
    int captured_square;
    int promotion_piece;
    unsigned int flags;
} Move;

typedef struct {
    Move move;
    struct Bitboard previous_board;
    int prev_castling_rights;
    int prev_en_passant_square;
    int prev_halfmove_clock;
    int prev_history_count;
} MoveUndo;

typedef struct {
    uint64_t nodes;
    int depth_completed;
    int time_ms;
    int score_cp;
} SearchStats;

// ═══════════════════════════════════════════════════════════════
//  GLOBAL STATE
// ═══════════════════════════════════════════════════════════════

extern struct Bitboard game_board;

// ═══════════════════════════════════════════════════════════════
//  PUBLIC API
// ═══════════════════════════════════════════════════════════════

// Square queries
int      sq_color(int location);
int      sq_piece(int location);

// Move API
int      is_valid(int start, int end);
int      move_piece(int start, int end);
int      generate_legal_moves(int color, Move *moves, int max_moves);
int      make_move(const Move *move, MoveUndo *undo);
void     undo_move(const MoveUndo *undo);

// Evaluation API (AI phase 2)
int      evaluate_material(int perspective_color);
int      evaluate_position(int perspective_color);

// Search API (AI phase 3)
int      find_best_move(int color, int depth, Move *best_move);
int      find_best_move_timed(int color, int max_depth, int time_budget_ms, Move *best_move);
int      get_last_search_stats(SearchStats *out_stats);
uint64_t perft(int color, int depth);

// Game state
int      in_check(int color);
int      in_checkmate(int color);
int      is_draw_by_repetition(void);
int      is_draw_by_fifty_move(void);
int      is_draw(void);

// Terminal display (optional)
void     print_board(void);

#endif // CHESS_H
