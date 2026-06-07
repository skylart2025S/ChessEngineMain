#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "chess.h"

// ═══════════════════════════════════════════════════════════════
//  CONSTANTS
// ═══════════════════════════════════════════════════════════════

#define FILE_A  0x7F7F7F7F7F7F7F7FULL
#define FILE_H  0xFEFEFEFEFEFEFEFEULL
#define FILE_AB 0x3F3F3F3F3F3F3F3FULL
#define FILE_GH 0xFCFCFCFCFCFCFCFCULL
#define CASTLE_WHITE_KING  0x1
#define CASTLE_WHITE_QUEEN 0x2
#define CASTLE_BLACK_KING  0x4
#define CASTLE_BLACK_QUEEN 0x8

typedef uint64_t (*attack_fn)(int, uint64_t);

static const int PIECE_VALUE[6] = {
    100,   // pawn
    320,   // knight
    330,   // bishop
    500,   // rook
    900,   // queen
    20000  // king (large anchor, mostly symbolic)
};

#define MAX_LEGAL_MOVES 256
#define MAX_PLY 128
#define SEARCH_INF 100000000
#define SEARCH_MATE_SCORE 1000000
#define DEFAULT_TIME_PER_PLY_MS 80
#define SEARCH_SOFT_STOP_MIN_DEPTH 2
#define SEARCH_HARD_TIME_MULTIPLIER 3
#define SEARCH_HARD_TIME_DIVISOR 2
#define SEARCH_NODES_PER_MS 6000ULL
#define TT_SIZE (1u << 16)
#define MAX_POSITION_HISTORY 4096

enum {
    TT_FLAG_EXACT = 0,
    TT_FLAG_LOWER = 1,
    TT_FLAG_UPPER = 2
};

typedef struct {
    uint64_t key;
    int depth;
    int score;
    int flag;
    Move best_move;
} TTEntry;

typedef struct {
    int stop;
    uint64_t nodes;
    uint64_t max_nodes;
    int depth_completed;
    int min_depth_for_soft_stop;
    int64_t soft_deadline_ms;
    int64_t hard_deadline_ms;
} SearchContext;

static TTEntry transposition_table[TT_SIZE];
static uint64_t zobrist_piece[2][6][64];
static uint64_t zobrist_side;
static int zobrist_ready = 0;
static Move killer_moves[MAX_PLY][2];
static int history_heuristic[2][64][64];
static SearchStats last_search_stats = {0};

// Piece-square tables from White's perspective (a1 index 0).
// Black uses mirrored squares.
static const int PST[6][64] = {
    [PAWN] = {
          0,   0,   0,   0,   0,   0,   0,   0,
         50,  50,  50,  50,  50,  50,  50,  50,
         10,  10,  20,  30,  30,  20,  10,  10,
          5,   5,  10,  25,  25,  10,   5,   5,
          0,   0,   0,  20,  20,   0,   0,   0,
          5,  -5, -10,   0,   0, -10,  -5,   5,
          5,  10,  10, -20, -20,  10,  10,   5,
          0,   0,   0,   0,   0,   0,   0,   0
    },
    [KNIGHT] = {
        -50, -40, -30, -30, -30, -30, -40, -50,
        -40, -20,   0,   5,   5,   0, -20, -40,
        -30,   5,  10,  15,  15,  10,   5, -30,
        -30,   0,  15,  20,  20,  15,   0, -30,
        -30,   5,  15,  20,  20,  15,   5, -30,
        -30,   0,  10,  15,  15,  10,   0, -30,
        -40, -20,   0,   0,   0,   0, -20, -40,
        -50, -40, -30, -30, -30, -30, -40, -50
    },
    [BISHOP] = {
        -20, -10, -10, -10, -10, -10, -10, -20,
        -10,   5,   0,   0,   0,   0,   5, -10,
        -10,  10,  10,  10,  10,  10,  10, -10,
        -10,   0,  10,  10,  10,  10,   0, -10,
        -10,   5,   5,  10,  10,   5,   5, -10,
        -10,   0,   5,  10,  10,   5,   0, -10,
        -10,   0,   0,   0,   0,   0,   0, -10,
        -20, -10, -10, -10, -10, -10, -10, -20
    },
    [ROOK] = {
          0,   0,   5,  10,  10,   5,   0,   0,
         -5,   0,   0,   0,   0,   0,   0,  -5,
         -5,   0,   0,   0,   0,   0,   0,  -5,
         -5,   0,   0,   0,   0,   0,   0,  -5,
         -5,   0,   0,   0,   0,   0,   0,  -5,
         -5,   0,   0,   0,   0,   0,   0,  -5,
          5,  10,  10,  10,  10,  10,  10,   5,
          0,   0,   0,   0,   0,   0,   0,   0
    },
    [QUEEN] = {
        -20, -10, -10,  -5,  -5, -10, -10, -20,
        -10,   0,   0,   0,   0,   0,   0, -10,
        -10,   0,   5,   5,   5,   5,   0, -10,
         -5,   0,   5,   5,   5,   5,   0,  -5,
          0,   0,   5,   5,   5,   5,   0,  -5,
        -10,   5,   5,   5,   5,   5,   0, -10,
        -10,   0,   5,   0,   0,   0,   0, -10,
        -20, -10, -10,  -5,  -5, -10, -10, -20
    },
    [KING] = {
        -30, -40, -40, -50, -50, -40, -40, -30,
        -30, -40, -40, -50, -50, -40, -40, -30,
        -30, -40, -40, -50, -50, -40, -40, -30,
        -30, -40, -40, -50, -50, -40, -40, -30,
        -20, -30, -30, -40, -40, -30, -30, -20,
        -10, -20, -20, -20, -20, -20, -20, -10,
         20,  20,   0,   0,   0,   0,  20,  20,
         20,  30,  10,   0,   0,  10,  30,  20
    }
};

static const int KING_PST_ENDGAME[64] = {
    -50, -40, -30, -20, -20, -30, -40, -50,
    -30, -20, -10,   0,   0, -10, -20, -30,
    -30, -10,  20,  30,  30,  20, -10, -30,
    -30, -10,  30,  40,  40,  30, -10, -30,
    -30, -10,  30,  40,  40,  30, -10, -30,
    -30, -10,  20,  30,  30,  20, -10, -30,
    -30, -30,   0,   0,   0,   0, -30, -30,
    -50, -30, -30, -30, -30, -30, -30, -50
};

// ═══════════════════════════════════════════════════════════════
//  GLOBAL STATE
// ═══════════════════════════════════════════════════════════════

struct Bitboard game_board = {
    .color = {
        0x000000000000FFFF,  // WHITE: ranks 1-2
        0xFFFF000000000000,  // BLACK: ranks 7-8
    },
    .piece = {
        [PAWN]   = 0x00FF00000000FF00,
        [KNIGHT] = 0x4200000000000042,
        [BISHOP] = 0x2400000000000024,
        [ROOK]   = 0x8100000000000081,
        [QUEEN]  = 0x0800000000000008,
        [KING]   = 0x1000000000000010,
    }
};

static int castling_rights = CASTLE_WHITE_KING | CASTLE_WHITE_QUEEN
                           | CASTLE_BLACK_KING | CASTLE_BLACK_QUEEN;
static int en_passant_square = EMPTY;
static int halfmove_clock = 0;
static uint64_t position_history[MAX_POSITION_HISTORY];
static int position_history_count = 0;
static struct Attacks get_attacks(int color);

// ═══════════════════════════════════════════════════════════════
//  SQUARE HELPERS
// ═══════════════════════════════════════════════════════════════

static uint64_t sq(int loc) {
    return (uint64_t)1 << loc;
}

int sq_color(int location) {
    if (location < 0 || location > 63) return EMPTY;
    uint64_t mask = sq(location);
    if (game_board.color[WHITE] & mask) return WHITE;
    if (game_board.color[BLACK] & mask) return BLACK;
    return EMPTY;
}

int sq_piece(int location) {
    if (location < 0 || location > 63) return EMPTY;
    uint64_t mask = sq(location);
    for (int p = PAWN; p <= KING; p++)
        if (game_board.piece[p] & mask) return p;
    return EMPTY;
}

static void clear_square(int location) {
    uint64_t mask = ~sq(location);
    for (int c = WHITE; c <= BLACK; c++) game_board.color[c] &= mask;
    for (int p = PAWN;  p <= KING;  p++) game_board.piece[p] &= mask;
}

static void set_square(int location, int color, int piece) {
    game_board.color[color]  |= sq(location);
    game_board.piece[piece]  |= sq(location);
}

static int is_square_attacked_by(int sq_idx, int attacker_color) {
    return (get_attacks(attacker_color).all & sq(sq_idx)) != 0;
}

static int promotion_piece_for_move(const Move *move) {
    if (move->piece != PAWN) return EMPTY;
    int target_rank = move->to / 8;
    if ((move->color == WHITE && target_rank == 7) || (move->color == BLACK && target_rank == 0)) {
        return QUEEN; // phase 10: auto-queen promotion
    }
    return EMPTY;
}

static void update_castling_rights_after_move(const Move *move) {
    if (move->piece == KING) {
        if (move->color == WHITE) castling_rights &= ~(CASTLE_WHITE_KING | CASTLE_WHITE_QUEEN);
        else castling_rights &= ~(CASTLE_BLACK_KING | CASTLE_BLACK_QUEEN);
    }

    if (move->piece == ROOK) {
        if (move->from == 0)  castling_rights &= ~CASTLE_WHITE_QUEEN;
        if (move->from == 7)  castling_rights &= ~CASTLE_WHITE_KING;
        if (move->from == 56) castling_rights &= ~CASTLE_BLACK_QUEEN;
        if (move->from == 63) castling_rights &= ~CASTLE_BLACK_KING;
    }

    if (move->captured_piece == ROOK) {
        if (move->captured_square == 0)  castling_rights &= ~CASTLE_WHITE_QUEEN;
        if (move->captured_square == 7)  castling_rights &= ~CASTLE_WHITE_KING;
        if (move->captured_square == 56) castling_rights &= ~CASTLE_BLACK_QUEEN;
        if (move->captured_square == 63) castling_rights &= ~CASTLE_BLACK_KING;
    }
}

static void apply_move_on_board(const Move *move) {
    int placed_piece = (move->flags & MOVE_FLAG_PROMOTION) ? move->promotion_piece : move->piece;

    if (move->flags & MOVE_FLAG_EN_PASSANT) {
        clear_square(move->captured_square);
    } else {
        clear_square(move->to);
    }

    clear_square(move->from);
    set_square(move->to, move->color, placed_piece);

    if (move->flags & MOVE_FLAG_CASTLE) {
        int rook_from;
        int rook_to;
        if (move->to > move->from) {
            rook_from = move->from + 3;
            rook_to = move->from + 1;
        } else {
            rook_from = move->from - 4;
            rook_to = move->from - 1;
        }
        clear_square(rook_from);
        set_square(rook_to, move->color, ROOK);
    }

    en_passant_square = EMPTY;
    int pawn_delta = move->to - move->from;
    if (move->piece == PAWN && (pawn_delta == 16 || pawn_delta == -16)) {
        en_passant_square = move->from + ((move->color == WHITE) ? 8 : -8);
    }

    update_castling_rights_after_move(move);
}

static Move build_move_snapshot(int from, int to) {
    Move move;
    move.from = from;
    move.to = to;
    move.piece = sq_piece(from);
    move.color = sq_color(from);
    move.captured_piece = sq_piece(to);
    move.captured_color = sq_color(to);
    move.captured_square = to;
    move.promotion_piece = EMPTY;
    move.flags = 0;

    if (move.piece == PAWN && to == en_passant_square && move.captured_piece == EMPTY) {
        move.flags |= MOVE_FLAG_EN_PASSANT;
        move.captured_square = to + ((move.color == WHITE) ? -8 : 8);
        move.captured_piece = sq_piece(move.captured_square);
        move.captured_color = sq_color(move.captured_square);
    }

    int king_delta = to - from;
    if (move.piece == KING && (king_delta == 2 || king_delta == -2)) {
        move.flags |= MOVE_FLAG_CASTLE;
    }

    move.promotion_piece = promotion_piece_for_move(&move);
    if (move.promotion_piece != EMPTY) {
        move.flags |= MOVE_FLAG_PROMOTION;
    }
    return move;
}

static int mirror_square_for_black(int sq_idx) {
    int rank = sq_idx / 8;
    int file = sq_idx % 8;
    return (7 - rank) * 8 + file;
}

static int piece_square_bonus(int piece, int color, int sq_idx) {
    int table_idx = (color == WHITE) ? sq_idx : mirror_square_for_black(sq_idx);
    return PST[piece][table_idx];
}

static uint64_t file_mask(int file) {
    uint64_t mask = 0;
    for (int rank = 0; rank < 8; rank++) {
        mask |= sq(rank * 8 + file);
    }
    return mask;
}

static int game_phase_value(void) {
    // 24 = opening/middlegame, 0 = deep endgame.
    static const int phase_piece[6] = { 0, 1, 1, 2, 4, 0 };
    int phase = 0;
    for (int piece = PAWN; piece <= KING; piece++) {
        int count = (int)__builtin_popcountll(game_board.piece[piece]);
        phase += count * phase_piece[piece];
    }
    if (phase > 24) phase = 24;
    return phase;
}

static int pawn_structure_score(int color, int endgame_weight) {
    uint64_t own_pawns = game_board.piece[PAWN] & game_board.color[color];
    uint64_t enemy_pawns = game_board.piece[PAWN] & game_board.color[1 - color];
    int score = 0;

    for (int file = 0; file < 8; file++) {
        int file_pawns = (int)__builtin_popcountll(own_pawns & file_mask(file));
        if (file_pawns > 1) score -= (file_pawns - 1) * 12;
    }

    uint64_t temp = own_pawns;
    while (temp) {
        int sq_idx = __builtin_ctzll(temp);
        int file = sq_idx % 8;
        int rank = sq_idx / 8;

        uint64_t left_file = (file > 0) ? (own_pawns & file_mask(file - 1)) : 0;
        uint64_t right_file = (file < 7) ? (own_pawns & file_mask(file + 1)) : 0;
        if (!left_file && !right_file) score -= 10;

        int passed = 1;
        int dir = (color == WHITE) ? 1 : -1;
        for (int r = rank + dir; r >= 0 && r < 8; r += dir) {
            for (int f = file - 1; f <= file + 1; f++) {
                if (f < 0 || f > 7) continue;
                int target = r * 8 + f;
                if (enemy_pawns & sq(target)) {
                    passed = 0;
                    goto not_passed;
                }
            }
        }
not_passed:
        if (passed) {
            int advance = (color == WHITE) ? rank : (7 - rank);
            score += 10 + advance * (6 + endgame_weight / 4);
        }

        temp &= temp - 1;
    }

    return score;
}

static int king_safety_score(int color, int middlegame_weight) {
    uint64_t king_bits = game_board.piece[KING] & game_board.color[color];
    if (!king_bits) return 0;

    int king_sq = __builtin_ctzll(king_bits);
    int king_file = king_sq % 8;
    int king_rank = king_sq / 8;
    int dir = (color == WHITE) ? 1 : -1;
    int score = 0;

    uint64_t own_pawns = game_board.piece[PAWN] & game_board.color[color];
    uint64_t enemy_heavy = (game_board.piece[ROOK] | game_board.piece[QUEEN]) & game_board.color[1 - color];

    for (int df = -1; df <= 1; df++) {
        int file = king_file + df;
        if (file < 0 || file > 7) continue;

        int shield_rank = king_rank + dir;
        int shield_ok = 0;
        if (shield_rank >= 0 && shield_rank < 8) {
            int shield_sq = shield_rank * 8 + file;
            shield_ok = (own_pawns & sq(shield_sq)) != 0;
        }

        if (shield_ok) score += 10;
        else score -= 14;

        if ((own_pawns & file_mask(file)) == 0) score -= 8;
        if (enemy_heavy & file_mask(file)) score -= 10;
    }

    return (score * middlegame_weight) / 24;
}

static int evaluate_side(int color, int middlegame_weight, int endgame_weight) {
    int score = 0;
    uint64_t pieces = game_board.color[color];

    while (pieces) {
        int sq_idx = __builtin_ctzll(pieces);
        int piece = sq_piece(sq_idx);
        if (piece != EMPTY) {
            int table_idx = (color == WHITE) ? sq_idx : mirror_square_for_black(sq_idx);
            int mg_pst = PST[piece][table_idx];
            int eg_pst = (piece == KING) ? KING_PST_ENDGAME[table_idx] : mg_pst;
            int tapered_pst = (mg_pst * middlegame_weight + eg_pst * endgame_weight) / 24;

            score += PIECE_VALUE[piece];
            score += tapered_pst;
        }
        pieces &= pieces - 1;
    }

    score += pawn_structure_score(color, endgame_weight);
    score += king_safety_score(color, middlegame_weight);
    return score;
}

static int same_move(const Move *a, const Move *b);

static int move_order_score(const Move *move, int color, int ply, int tactical_only) {
    if (!move) return 0;

    int score = 0;
    if (move->captured_piece != EMPTY) {
        // MVV-LVA style capture ordering: prefer valuable victims, cheap attackers.
        score += 100000 + PIECE_VALUE[move->captured_piece] - PIECE_VALUE[move->piece] / 10;
    } else if (!tactical_only) {
        if (ply < MAX_PLY) {
            if (same_move(move, &killer_moves[ply][0])) {
                score += 90000;
            } else if (same_move(move, &killer_moves[ply][1])) {
                score += 80000;
            }
        }
        if (color == WHITE || color == BLACK) {
            score += history_heuristic[color][move->from][move->to];
        }
    }

    return score;
}

static int same_move(const Move *a, const Move *b) {
    if (!a || !b) return 0;
    return a->from == b->from && a->to == b->to;
}

static void sort_moves_for_search(Move *moves, int count, int color, int ply, int tactical_only) {
    for (int i = 1; i < count; i++) {
        Move key = moves[i];
        int key_score = move_order_score(&key, color, ply, tactical_only);
        int j = i - 1;
        while (j >= 0 && move_order_score(&moves[j], color, ply, tactical_only) < key_score) {
            moves[j + 1] = moves[j];
            j--;
        }
        moves[j + 1] = key;
    }
}

static void reset_search_heuristics(void) {
    memset(killer_moves, 0, sizeof(killer_moves));
    memset(history_heuristic, 0, sizeof(history_heuristic));
}

static void update_killers_and_history(const Move *move, int color, int ply, int depth) {
    if (!move) return;
    if (move->captured_piece != EMPTY) return;

    if (ply < MAX_PLY && !same_move(move, &killer_moves[ply][0])) {
        killer_moves[ply][1] = killer_moves[ply][0];
        killer_moves[ply][0] = *move;
    }

    if (color == WHITE || color == BLACK) {
        int bonus = depth * depth;
        int *hist = &history_heuristic[color][move->from][move->to];
        *hist += bonus;
        if (*hist > 200000) *hist = 200000;
    }
}

static void prioritize_move(Move *moves, int count, const Move *favored) {
    if (!favored) return;
    for (int i = 0; i < count; i++) {
        if (same_move(&moves[i], favored)) {
            Move tmp = moves[0];
            moves[0] = moves[i];
            moves[i] = tmp;
            return;
        }
    }
}

static int64_t now_ms(void) {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (int64_t)ts.tv_sec * 1000 + (int64_t)(ts.tv_nsec / 1000000);
}

static int clamp_i(int value, int min_v, int max_v) {
    if (value < min_v) return min_v;
    if (value > max_v) return max_v;
    return value;
}

static uint64_t splitmix64_next(uint64_t *state) {
    uint64_t z = (*state += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

static void init_zobrist(void) {
    if (zobrist_ready) return;

    uint64_t seed = 0x1234fedcba987654ULL;
    for (int color = WHITE; color <= BLACK; color++) {
        for (int piece = PAWN; piece <= KING; piece++) {
            for (int sq_idx = 0; sq_idx < 64; sq_idx++) {
                zobrist_piece[color][piece][sq_idx] = splitmix64_next(&seed);
            }
        }
    }
    zobrist_side = splitmix64_next(&seed);
    zobrist_ready = 1;
}

static uint64_t board_hash(int side_to_move) {
    uint64_t h = 0;
    for (int sq_idx = 0; sq_idx < 64; sq_idx++) {
        int piece = sq_piece(sq_idx);
        if (piece == EMPTY) continue;
        int color = sq_color(sq_idx);
        h ^= zobrist_piece[color][piece][sq_idx];
    }
    if (side_to_move == BLACK) h ^= zobrist_side;
    return h;
}

static uint64_t position_hash_with_state(int side_to_move) {
    uint64_t h = board_hash(side_to_move);
    h ^= ((uint64_t)castling_rights * 0x9e3779b97f4a7c15ULL);
    h ^= ((uint64_t)(en_passant_square + 2) * 0xc2b2ae3d27d4eb4fULL);
    return h;
}

static void reset_position_history(int side_to_move) {
    position_history_count = 0;
    if (position_history_count < MAX_POSITION_HISTORY) {
        position_history[position_history_count++] = position_hash_with_state(side_to_move);
    }
}

static void push_position_history(int side_to_move) {
    if (position_history_count < MAX_POSITION_HISTORY) {
        position_history[position_history_count++] = position_hash_with_state(side_to_move);
    }
}

static int repetition_count_for_current_position(void) {
    if (position_history_count <= 0) return 0;
    uint64_t key = position_history[position_history_count - 1];
    int count = 0;
    for (int i = 0; i < position_history_count; i++) {
        if (position_history[i] == key) count++;
    }
    return count;
}

static int tt_probe(uint64_t key, int depth, int alpha, int beta, int *score_out, Move *best_move_out) {
    TTEntry *entry = &transposition_table[key & (TT_SIZE - 1)];
    if (entry->key != key) return 0;
    if (best_move_out) *best_move_out = entry->best_move;
    if (entry->depth < depth) return 0;

    if (entry->flag == TT_FLAG_EXACT) {
        *score_out = entry->score;
        return 1;
    }
    if (entry->flag == TT_FLAG_LOWER && entry->score >= beta) {
        *score_out = entry->score;
        return 1;
    }
    if (entry->flag == TT_FLAG_UPPER && entry->score <= alpha) {
        *score_out = entry->score;
        return 1;
    }
    return 0;
}

static void tt_store(uint64_t key, int depth, int score, int flag, const Move *best_move) {
    TTEntry *entry = &transposition_table[key & (TT_SIZE - 1)];
    if (entry->key == key && entry->depth > depth) return;

    entry->key = key;
    entry->depth = depth;
    entry->score = score;
    entry->flag = flag;
    if (best_move) entry->best_move = *best_move;
}

static int should_stop(SearchContext *ctx) {
    if (!ctx) return 0;
    if ((ctx->nodes & 2047ULL) != 0) return 0;
    if (ctx->max_nodes > 0 && ctx->nodes >= ctx->max_nodes) {
        ctx->stop = 1;
        return 1;
    }
    int64_t now = now_ms();
    if (ctx->hard_deadline_ms > 0 && now >= ctx->hard_deadline_ms) {
        ctx->stop = 1;
        return 1;
    }
    if (ctx->soft_deadline_ms > 0
            && now >= ctx->soft_deadline_ms
            && ctx->depth_completed >= ctx->min_depth_for_soft_stop) {
        ctx->stop = 1;
        return 1;
    }
    return 0;
}

// ═══════════════════════════════════════════════════════════════
//  ATTACK GENERATION
// ═══════════════════════════════════════════════════════════════

static uint64_t ray_attacks(int sq_idx, int delta, uint64_t occupied) {
    static const int wraps_at_a[] = {  1, -7,  9, 0 };
    static const int wraps_at_h[] = { -1,  7, -9, 0 };

    uint64_t attacks = 0;
    for (int cur = sq_idx + delta; cur >= 0 && cur < 64; cur += delta) {
        for (int i = 0; wraps_at_a[i]; i++)
            if (delta == wraps_at_a[i] && cur % 8 == 0) goto done;
        for (int i = 0; wraps_at_h[i]; i++)
            if (delta == wraps_at_h[i] && cur % 8 == 7) goto done;
        attacks |= sq(cur);
        if (occupied & sq(cur)) break;
    }
    done: return attacks;
}

static uint64_t rook_attacks(int s, uint64_t occ) {
    return ray_attacks(s,  1, occ) | ray_attacks(s, -1, occ)
         | ray_attacks(s,  8, occ) | ray_attacks(s, -8, occ);
}

static uint64_t bishop_attacks(int s, uint64_t occ) {
    return ray_attacks(s,  9, occ) | ray_attacks(s, -9, occ)
         | ray_attacks(s,  7, occ) | ray_attacks(s, -7, occ);
}

static uint64_t queen_attacks(int s, uint64_t occ) {
    return rook_attacks(s, occ) | bishop_attacks(s, occ);
}

static uint64_t knight_attacks(int s, uint64_t occ) {
    (void)occ;
    uint64_t b  = sq(s);
    uint64_t l1 = (b >> 1) & FILE_A;
    uint64_t l2 = (b >> 2) & FILE_AB;
    uint64_t r1 = (b << 1) & FILE_H;
    uint64_t r2 = (b << 2) & FILE_GH;
    return (l1 | r1) << 16 | (l1 | r1) >> 16
         | (l2 | r2) << 8  | (l2 | r2) >> 8;
}

static uint64_t king_attacks(int s, uint64_t occ) {
    (void)occ;
    uint64_t b = sq(s);
    return ((b << 1) & FILE_H) | ((b >> 1) & FILE_A)
         |  (b << 8)           |  (b >> 8)
         | ((b << 9) & FILE_H) | ((b << 7) & FILE_A)
         | ((b >> 7) & FILE_H) | ((b >> 9) & FILE_A);
}

static uint64_t pawn_attacks(int s, uint64_t color) {
    uint64_t b = sq(s);
    if ((int)color == WHITE)
        return ((b << 9) & FILE_H) | ((b << 7) & FILE_A);
    else
        return ((b >> 7) & FILE_H) | ((b >> 9) & FILE_A);
}

// ─── Accumulator ─────────────────────────────────────────────

static void accumulate(uint64_t pieces, uint64_t occupied,
                        uint64_t *dest, attack_fn fn) {
    while (pieces) {
        int s = __builtin_ctzll(pieces);
        *dest |= fn(s, occupied);
        pieces &= pieces - 1;
    }
}

static struct Attacks get_attacks(int color) {
    struct Attacks attacks = {0};
    uint64_t occupied = game_board.color[WHITE] | game_board.color[BLACK];
    uint64_t pieces   = game_board.color[color];

    accumulate(game_board.piece[PAWN]   & pieces, (uint64_t)color, &attacks.piece[PAWN],   pawn_attacks);
    accumulate(game_board.piece[KNIGHT] & pieces, occupied,        &attacks.piece[KNIGHT], knight_attacks);
    accumulate(game_board.piece[BISHOP] & pieces, occupied,        &attacks.piece[BISHOP], bishop_attacks);
    accumulate(game_board.piece[ROOK]   & pieces, occupied,        &attacks.piece[ROOK],   rook_attacks);
    accumulate(game_board.piece[QUEEN]  & pieces, occupied,        &attacks.piece[QUEEN],  queen_attacks);
    accumulate(game_board.piece[KING]   & pieces, occupied,        &attacks.piece[KING],   king_attacks);

    for (int p = PAWN; p <= KING; p++)
        attacks.all |= attacks.piece[p];

    return attacks;
}

// ═══════════════════════════════════════════════════════════════
//  CHECK & CHECKMATE
// ═══════════════════════════════════════════════════════════════

int in_checkmate(int color); // forward declaration

int in_check(int color) {
    uint64_t king_sq = game_board.piece[KING] & game_board.color[color];
    int king_idx     = __builtin_ctzll(king_sq);
    return (get_attacks(1 - color).all & sq(king_idx)) != 0;
}

int is_draw_by_repetition(void) {
    return repetition_count_for_current_position() >= 3;
}

int is_draw_by_fifty_move(void) {
    return halfmove_clock >= 100;
}

int is_draw(void) {
    return is_draw_by_repetition() || is_draw_by_fifty_move();
}

// ═══════════════════════════════════════════════════════════════
//  MOVE VALIDATION & EXECUTION
// ═══════════════════════════════════════════════════════════════

static uint64_t reachable_squares(int piece, int start, int color) {
    uint64_t occupied  = game_board.color[WHITE] | game_board.color[BLACK];
    int      file      = start % 8;
    int      dir       = (color == WHITE) ? 1 : -1;
    int      home_rank = (color == WHITE) ? 1 : 6;

    switch (piece) {
        case PAWN: {
            uint64_t reachable = 0;
            int fwd   = start + 8 * dir;
            int fwd2  = start + 16 * dir;
            int cap_l = fwd - 1;
            int cap_r = fwd + 1;
            if (sq_color(fwd) == EMPTY)
                reachable |= sq(fwd);
            if (start / 8 == home_rank && sq_color(fwd) == EMPTY && sq_color(fwd2) == EMPTY)
                reachable |= sq(fwd2);
            if (file > 0 && sq_color(cap_l) == 1 - color)
                reachable |= sq(cap_l);
            if (file < 7 && sq_color(cap_r) == 1 - color)
                reachable |= sq(cap_r);
            if (file > 0 && cap_l == en_passant_square)
                reachable |= sq(cap_l);
            if (file < 7 && cap_r == en_passant_square)
                reachable |= sq(cap_r);
            return reachable;
        }
        case KNIGHT: return knight_attacks(start, occupied);
        case BISHOP: return bishop_attacks(start, occupied);
        case ROOK:   return rook_attacks  (start, occupied);
        case QUEEN:  return queen_attacks  (start, occupied);
        case KING: {
            uint64_t king_moves = king_attacks(start, occupied);
            if (color == WHITE && start == 4 && !in_check(WHITE)) {
                if ((castling_rights & CASTLE_WHITE_KING)
                        && sq_color(5) == EMPTY && sq_color(6) == EMPTY
                        && !is_square_attacked_by(5, BLACK)
                        && !is_square_attacked_by(6, BLACK)) {
                    king_moves |= sq(6);
                }
                if ((castling_rights & CASTLE_WHITE_QUEEN)
                        && sq_color(3) == EMPTY && sq_color(2) == EMPTY && sq_color(1) == EMPTY
                        && !is_square_attacked_by(3, BLACK)
                        && !is_square_attacked_by(2, BLACK)) {
                    king_moves |= sq(2);
                }
            }
            if (color == BLACK && start == 60 && !in_check(BLACK)) {
                if ((castling_rights & CASTLE_BLACK_KING)
                        && sq_color(61) == EMPTY && sq_color(62) == EMPTY
                        && !is_square_attacked_by(61, WHITE)
                        && !is_square_attacked_by(62, WHITE)) {
                    king_moves |= sq(62);
                }
                if ((castling_rights & CASTLE_BLACK_QUEEN)
                        && sq_color(59) == EMPTY && sq_color(58) == EMPTY && sq_color(57) == EMPTY
                        && !is_square_attacked_by(59, WHITE)
                        && !is_square_attacked_by(58, WHITE)) {
                    king_moves |= sq(58);
                }
            }
            return king_moves;
        }
        default:     return 0;
    }
}

int is_valid(int start, int end) {
    Move move = build_move_snapshot(start, end);

    if (move.piece == EMPTY)                                         return 0;
    if (move.captured_color == move.color)                           return 0;
    if (!(reachable_squares(move.piece, start, move.color) & sq(end))) return 0;
    if ((move.flags & MOVE_FLAG_EN_PASSANT) && move.captured_piece != PAWN) return 0;

    struct Bitboard saved = game_board;
    int saved_castling_rights = castling_rights;
    int saved_en_passant_square = en_passant_square;
    apply_move_on_board(&move);
    int leaves_check = in_check(move.color);
    game_board = saved;
    castling_rights = saved_castling_rights;
    en_passant_square = saved_en_passant_square;

    return !leaves_check;
}

int make_move(const Move *move, MoveUndo *undo) {
    if (!move) return 0;
    if (!is_valid(move->from, move->to)) return 0;

    Move current = build_move_snapshot(move->from, move->to);
    if (position_history_count == 0) {
        init_zobrist();
        reset_position_history(current.color);
    }

    if (undo) {
        undo->move = current;
        undo->previous_board = game_board;
        undo->prev_castling_rights = castling_rights;
        undo->prev_en_passant_square = en_passant_square;
        undo->prev_halfmove_clock = halfmove_clock;
        undo->prev_history_count = position_history_count;
    }

    apply_move_on_board(&current);
    if (current.piece == PAWN || current.captured_piece != EMPTY) halfmove_clock = 0;
    else halfmove_clock++;
    push_position_history(1 - current.color);
    return 1;
}

void undo_move(const MoveUndo *undo) {
    if (!undo) return;
    game_board = undo->previous_board;
    castling_rights = undo->prev_castling_rights;
    en_passant_square = undo->prev_en_passant_square;
    halfmove_clock = undo->prev_halfmove_clock;
    position_history_count = undo->prev_history_count;
}

int generate_legal_moves(int color, Move *moves, int max_moves) {
    if (max_moves <= 0) return 0;

    int count = 0;
    uint64_t pieces = game_board.color[color];

    while (pieces) {
        int from = __builtin_ctzll(pieces);
        uint64_t reachable = reachable_squares(sq_piece(from), from, color);
        for (int to = 0; to < 64; to++) {
            if (!(reachable & sq(to))) continue;
            if (!is_valid(from, to)) continue;

            if (moves && count < max_moves) {
                moves[count] = build_move_snapshot(from, to);
            }

            count++;
            if (!moves && count >= max_moves) return count;
        }
        pieces &= pieces - 1;
    }

    return count;
}

static int generate_capture_moves(int color, Move *moves, int max_moves) {
    if (max_moves <= 0) return 0;

    int count = 0;
    uint64_t pieces = game_board.color[color];

    while (pieces) {
        int from = __builtin_ctzll(pieces);
        uint64_t reachable = reachable_squares(sq_piece(from), from, color);
        for (int to = 0; to < 64; to++) {
            if (!(reachable & sq(to))) continue;
            if (sq_color(to) != (1 - color)) {
                int is_ep_capture = (sq_piece(from) == PAWN && to == en_passant_square);
                if (!is_ep_capture) continue;
            }
            if (!is_valid(from, to)) continue;

            if (moves && count < max_moves) {
                moves[count] = build_move_snapshot(from, to);
            }

            count++;
            if (!moves && count >= max_moves) return count;
        }
        pieces &= pieces - 1;
    }

    return count;
}

int evaluate_material(int perspective_color) {
    int own = 0;
    int opp = 0;
    for (int piece = PAWN; piece <= KING; piece++) {
        uint64_t own_bits = game_board.piece[piece] & game_board.color[perspective_color];
        uint64_t opp_bits = game_board.piece[piece] & game_board.color[1 - perspective_color];
        own += (int)__builtin_popcountll(own_bits) * PIECE_VALUE[piece];
        opp += (int)__builtin_popcountll(opp_bits) * PIECE_VALUE[piece];
    }
    return own - opp;
}

int evaluate_position(int perspective_color) {
    int mg_weight = game_phase_value();
    int eg_weight = 24 - mg_weight;

    int own_score = evaluate_side(perspective_color, mg_weight, eg_weight);
    int opp_score = evaluate_side(1 - perspective_color, mg_weight, eg_weight);

    // Light mobility term to prefer active positions.
    int own_mobility = generate_legal_moves(perspective_color, NULL, 128);
    int opp_mobility = generate_legal_moves(1 - perspective_color, NULL, 128);

    int mobility_weight = 1 + mg_weight / 12;
    return (own_score - opp_score) + mobility_weight * (own_mobility - opp_mobility);
}

static int quiescence(int color, int alpha, int beta, int ply, SearchContext *ctx) {
    if (ctx) {
        ctx->nodes++;
        if (should_stop(ctx)) return evaluate_position(color);
    }

    if (is_draw()) return 0;

    int stand_pat = evaluate_position(color);
    if (stand_pat >= beta) return beta;
    if (stand_pat > alpha) alpha = stand_pat;

    Move moves[MAX_LEGAL_MOVES];
    int move_count = 0;

    // If king is in check, search all legal evasions (not captures only).
    if (in_check(color)) {
        move_count = generate_legal_moves(color, moves, MAX_LEGAL_MOVES);
        if (move_count == 0) return -SEARCH_MATE_SCORE + ply;
    } else {
        move_count = generate_capture_moves(color, moves, MAX_LEGAL_MOVES);
        if (move_count == 0) return alpha;
    }

    sort_moves_for_search(moves, move_count, color, ply, 1);

    for (int i = 0; i < move_count; i++) {
        if (ctx && should_stop(ctx)) break;

        MoveUndo undo;
        if (!make_move(&moves[i], &undo)) continue;

        int score = -quiescence(1 - color, -beta, -alpha, ply + 1, ctx);
        undo_move(&undo);

        if (ctx && ctx->stop) break;
        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }

    return alpha;
}

static int negamax(int color, int depth, int alpha, int beta, int ply, SearchContext *ctx) {
    if (ctx) {
        ctx->nodes++;
        if (should_stop(ctx)) return evaluate_position(color);
    }

    uint64_t key = board_hash(color);
    Move tt_move = {0};
    int tt_score = 0;
    if (tt_probe(key, depth, alpha, beta, &tt_score, &tt_move)) {
        return tt_score;
    }

    if (is_draw()) return 0;

    if (depth == 0) return quiescence(color, alpha, beta, ply, ctx);

    Move moves[MAX_LEGAL_MOVES];
    int move_count = generate_legal_moves(color, moves, MAX_LEGAL_MOVES);

    if (move_count == 0) {
        if (in_check(color)) {
            return -SEARCH_MATE_SCORE + ply;
        }
        return 0; // stalemate
    }

    sort_moves_for_search(moves, move_count, color, ply, 0);
    prioritize_move(moves, move_count, &tt_move);

    int alpha_start = alpha;
    int best_score = -SEARCH_INF;
    Move best_local_move = moves[0];
    for (int i = 0; i < move_count; i++) {
        if (ctx && should_stop(ctx)) break;

        MoveUndo undo;
        if (!make_move(&moves[i], &undo)) continue;

        int score = -negamax(1 - color, depth - 1, -beta, -alpha, ply + 1, ctx);
        undo_move(&undo);

        if (ctx && ctx->stop) break;

        if (score > best_score) {
            best_score = score;
            best_local_move = moves[i];
        }
        if (score > alpha) alpha = score;
        if (alpha >= beta) {
            update_killers_and_history(&moves[i], color, ply, depth);
            break;
        }
    }

    if (!(ctx && ctx->stop)) {
        int flag = TT_FLAG_EXACT;
        if (best_score <= alpha_start) flag = TT_FLAG_UPPER;
        else if (best_score >= beta) flag = TT_FLAG_LOWER;
        tt_store(key, depth, best_score, flag, &best_local_move);
    }

    return best_score;
}

int find_best_move(int color, int depth, Move *best_move) {
    int time_budget_ms = depth * DEFAULT_TIME_PER_PLY_MS;
    return find_best_move_timed(color, depth, time_budget_ms, best_move);
}

int find_best_move_timed(int color, int max_depth, int time_budget_ms, Move *best_move) {
    if (!best_move || max_depth <= 0) return -SEARCH_INF;
    if (time_budget_ms <= 0) time_budget_ms = max_depth * DEFAULT_TIME_PER_PLY_MS;

    Move moves[MAX_LEGAL_MOVES];
    int move_count = generate_legal_moves(color, moves, MAX_LEGAL_MOVES);
    if (move_count == 0) {
        last_search_stats.nodes = 0;
        last_search_stats.depth_completed = 0;
        last_search_stats.time_ms = 0;
        last_search_stats.score_cp = in_check(color) ? -SEARCH_MATE_SCORE : 0;
        return in_check(color) ? -SEARCH_MATE_SCORE : 0;
    }
    if (is_draw()) {
        last_search_stats.nodes = 0;
        last_search_stats.depth_completed = 0;
        last_search_stats.time_ms = 1;
        last_search_stats.score_cp = 0;
        return 0;
    }

    init_zobrist();
    reset_search_heuristics();
    *best_move = moves[0]; // fallback if search is interrupted

    int adaptive_budget_ms = time_budget_ms;
    if (in_check(color)) adaptive_budget_ms += time_budget_ms / 2;      // panic extension
    if (move_count <= 8) adaptive_budget_ms += time_budget_ms / 3;      // tactical/forced line extension
    adaptive_budget_ms = clamp_i(adaptive_budget_ms, 20, 5000);

    int64_t start_ms = now_ms();

    SearchContext ctx = {
        .stop = 0,
        .nodes = 0,
        .max_nodes = (uint64_t)adaptive_budget_ms * SEARCH_NODES_PER_MS,
        .depth_completed = 0,
        .min_depth_for_soft_stop = SEARCH_SOFT_STOP_MIN_DEPTH,
        .soft_deadline_ms = start_ms + adaptive_budget_ms,
        .hard_deadline_ms = start_ms + (adaptive_budget_ms * SEARCH_HARD_TIME_MULTIPLIER) / SEARCH_HARD_TIME_DIVISOR
    };

    int last_complete_score = -SEARCH_INF;
    for (int depth = 1; depth <= max_depth; depth++) {
        if (ctx.stop) break;
        if (depth > 1 && now_ms() >= ctx.soft_deadline_ms && ctx.depth_completed >= ctx.min_depth_for_soft_stop) break;

        Move root_moves[MAX_LEGAL_MOVES];
        int root_count = generate_legal_moves(color, root_moves, MAX_LEGAL_MOVES);
        if (root_count == 0) break;

        Move tt_move = {0};
        int dummy_score = 0;
        (void)tt_probe(board_hash(color), depth, -SEARCH_INF, SEARCH_INF, &dummy_score, &tt_move);

        sort_moves_for_search(root_moves, root_count, color, 0, 0);
        prioritize_move(root_moves, root_count, &tt_move);

        int alpha = -SEARCH_INF;
        int beta = SEARCH_INF;
        int best_score = -SEARCH_INF;
        Move best_at_depth = root_moves[0];

        for (int i = 0; i < root_count; i++) {
            if (should_stop(&ctx)) break;

            MoveUndo undo;
            if (!make_move(&root_moves[i], &undo)) continue;

            int score = -negamax(1 - color, depth - 1, -beta, -alpha, 1, &ctx);
            undo_move(&undo);

            if (ctx.stop) break;

            if (score > best_score) {
                best_score = score;
                best_at_depth = root_moves[i];
            }
            if (score > alpha) alpha = score;
        }

        if (ctx.stop) break;

        *best_move = best_at_depth;
        last_complete_score = best_score;
        ctx.depth_completed = depth;
        tt_store(board_hash(color), depth, best_score, TT_FLAG_EXACT, &best_at_depth);
    }

    if (last_complete_score == -SEARCH_INF) {
        last_search_stats.nodes = ctx.nodes;
        last_search_stats.depth_completed = ctx.depth_completed;
        int elapsed_ms = (int)(now_ms() - start_ms);
        if (elapsed_ms <= 0 && ctx.nodes > 0) elapsed_ms = 1;
        last_search_stats.time_ms = elapsed_ms;
        last_search_stats.score_cp = evaluate_position(color);
        return last_search_stats.score_cp;
    }
    last_search_stats.nodes = ctx.nodes;
    last_search_stats.depth_completed = ctx.depth_completed;
    int elapsed_ms = (int)(now_ms() - start_ms);
    if (elapsed_ms <= 0 && ctx.nodes > 0) elapsed_ms = 1;
    last_search_stats.time_ms = elapsed_ms;
    last_search_stats.score_cp = last_complete_score;
    return last_complete_score;
}

int get_last_search_stats(SearchStats *out_stats) {
    if (!out_stats) return 0;
    *out_stats = last_search_stats;
    return 1;
}

uint64_t perft(int color, int depth) {
    if (depth <= 0) return 1ULL;

    Move moves[MAX_LEGAL_MOVES];
    int move_count = generate_legal_moves(color, moves, MAX_LEGAL_MOVES);
    if (depth == 1) return (uint64_t)move_count;

    uint64_t nodes = 0;
    for (int i = 0; i < move_count; i++) {
        MoveUndo undo;
        if (!make_move(&moves[i], &undo)) continue;
        nodes += perft(1 - color, depth - 1);
        undo_move(&undo);
    }
    return nodes;
}

int move_piece(int start, int end) {
    Move move = build_move_snapshot(start, end);
    return make_move(&move, NULL);
}

int in_checkmate(int color) {
    if (!in_check(color)) return 0;
    return generate_legal_moves(color, NULL, 1) == 0;
}

// ═══════════════════════════════════════════════════════════════
//  TERMINAL DISPLAY
// ═══════════════════════════════════════════════════════════════

void print_board(void) {
    static const char piece_chars[] = {'P','N','B','R','Q','K'};
    printf("\n  a b c d e f g h\n");
    printf("  ───────────────\n");
    for (int rank = 7; rank >= 0; rank--) {
        printf("%d│", rank + 1);
        for (int file = 0; file < 8; file++) {
            int idx   = rank * 8 + file;
            int piece = sq_piece(idx);
            int color = sq_color(idx);
            char c    = (piece == EMPTY) ? '.' : piece_chars[piece];
            if (color == BLACK) c += 32;
            printf("%c ", c);
        }
        printf("│%d\n", rank + 1);
    }
    printf("  ───────────────\n");
    printf("  a b c d e f g h\n\n");
}

// ═══════════════════════════════════════════════════════════════
//  INPUT & GAME LOOP  (terminal mode — not used with SDL GUI)
// ═══════════════════════════════════════════════════════════════

static int parse_square(const char *s) {
    if (s[0] < 'a' || s[0] > 'h') return EMPTY;
    if (s[1] < '1' || s[1] > '8') return EMPTY;
    return (s[1] - '1') * 8 + (s[0] - 'a');
}

static void square_to_coord(int sq_idx, char out[3]) {
    out[0] = (char)('a' + (sq_idx % 8));
    out[1] = (char)('1' + (sq_idx / 8));
    out[2] = '\0';
}

static void move_to_uci(const Move *move, char out[6]) {
    char from[3];
    char to[3];
    square_to_coord(move->from, from);
    square_to_coord(move->to, to);
    out[0] = from[0];
    out[1] = from[1];
    out[2] = to[0];
    out[3] = to[1];
    out[4] = '\0';
    if (move->flags & MOVE_FLAG_PROMOTION) {
        out[4] = 'q';
        out[5] = '\0';
    }
}

static uint64_t perft_divide(int color, int depth) {
    Move moves[MAX_LEGAL_MOVES];
    int move_count = generate_legal_moves(color, moves, MAX_LEGAL_MOVES);
    uint64_t total = 0;

    for (int i = 0; i < move_count; i++) {
        MoveUndo undo;
        if (!make_move(&moves[i], &undo)) continue;
        uint64_t nodes = perft(1 - color, depth - 1);
        undo_move(&undo);

        char uci[6];
        move_to_uci(&moves[i], uci);
        printf("%s: %" PRIu64 "\n", uci, nodes);
        total += nodes;
    }

    printf("total: %" PRIu64 "\n", total);
    return total;
}

static int run_perft_cli(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s --perft <depth> [--divide]\n", argv[0]);
        return 1;
    }

    int depth = atoi(argv[2]);
    if (depth < 0) {
        fprintf(stderr, "Depth must be >= 0\n");
        return 1;
    }

    if (argc >= 4 && strcmp(argv[3], "--divide") == 0) {
        perft_divide(WHITE, depth);
        return 0;
    }

    uint64_t nodes = perft(WHITE, depth);
    printf("perft(depth=%d): %" PRIu64 "\n", depth, nodes);
    return 0;
}

static int run_bench_cli(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s --bench <depth> <time_ms> [plies]\n", argv[0]);
        return 1;
    }

    int depth = atoi(argv[2]);
    int time_ms = atoi(argv[3]);
    int plies = (argc >= 5) ? atoi(argv[4]) : 8;
    if (depth < 1 || time_ms < 1 || plies < 1) {
        fprintf(stderr, "depth, time_ms, and plies must be >= 1\n");
        return 1;
    }

    int side = WHITE;
    uint64_t total_nodes = 0;
    int total_time_ms = 0;
    int completed = 0;

    for (int ply = 1; ply <= plies; ply++) {
        if (generate_legal_moves(side, NULL, 1) == 0) break;

        Move best;
        int score = find_best_move_timed(side, depth, time_ms, &best);
        SearchStats stats;
        (void)get_last_search_stats(&stats);

        if (!move_piece(best.from, best.to)) break;

        char uci[6];
        move_to_uci(&best, uci);
        printf("BENCH ply=%d side=%s move=%s score=%d depth=%d nodes=%" PRIu64 " time_ms=%d\n",
               ply,
               side == WHITE ? "white" : "black",
               uci,
               score,
               stats.depth_completed,
               stats.nodes,
               stats.time_ms);

        total_nodes += stats.nodes;
        total_time_ms += stats.time_ms;
        completed++;

        side = 1 - side;
        if (generate_legal_moves(side, NULL, 1) == 0) break;
    }

    if (completed > 0) {
        uint64_t nps = (total_time_ms > 0) ? (total_nodes * 1000ULL) / (uint64_t)total_time_ms : 0;
        printf("BENCH_SUMMARY plies=%d avg_nodes=%" PRIu64 " avg_time_ms=%d avg_nps=%" PRIu64 "\n",
               completed,
               total_nodes / (uint64_t)completed,
               total_time_ms / completed,
               nps);
    } else {
        printf("BENCH_SUMMARY plies=0 avg_nodes=0 avg_time_ms=0 avg_nps=0\n");
    }

    return 0;
}

static void game_loop(void) {
    char input[16];
    int  turn = WHITE;

    print_board();
    init_zobrist();
    reset_position_history(turn);

    while (1) {
        if (is_draw_by_repetition()) {
            printf("Draw by repetition.\n");
            break;
        }
        if (is_draw_by_fifty_move()) {
            printf("Draw by fifty-move rule.\n");
            break;
        }
        if (in_checkmate(turn)) {
            printf("%s is in checkmate! %s wins!\n",
                turn == WHITE ? "White" : "Black",
                turn == WHITE ? "Black" : "White");
            break;
        }
        if (in_check(turn))
            printf("%s is in check!\n", turn == WHITE ? "White" : "Black");

        printf("%s to move (e.g. e2e4, or 'quit'): ",
               turn == WHITE ? "White" : "Black");

        if (!fgets(input, sizeof(input), stdin)) break;
        input[strcspn(input, "\n")] = '\0';

        if (strcmp(input, "quit") == 0) { printf("Game ended.\n"); break; }
        if (strlen(input) != 4)         { printf("Invalid format. Use e.g. e2e4\n"); continue; }

        int from = parse_square((char[]){ input[0], input[1], '\0' });
        int to   = parse_square((char[]){ input[2], input[3], '\0' });

        if (from == EMPTY || to == EMPTY) { printf("Invalid squares.\n");       continue; }
        if (sq_color(from) != turn)       { printf("That's not your piece!\n"); continue; }

        if (move_piece(from, to)) {
            print_board();
            turn = 1 - turn;
        } else {
            printf("Illegal move, try again.\n");
        }
    }
}
#ifdef TERMINAL_MODE
// ═══════════════════════════════════════════════════════════════
//  ENTRY POINT  (comment out when linking with display.c)
// ═══════════════════════════════════════════════════════════════

 int main(int argc, char **argv) {
  if (argc >= 2 && strcmp(argv[1], "--perft") == 0) {
      return run_perft_cli(argc, argv);
  }
  if (argc >= 2 && strcmp(argv[1], "--bench") == 0) {
      return run_bench_cli(argc, argv);
  }
  printf("TERMINAL MAIN REACHED\n");
  game_loop();
    return 0; 
}
#endif