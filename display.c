#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "chess.h"

// ═══════════════════════════════════════════════════════════════
//  CONSTANTS
// ═══════════════════════════════════════════════════════════════

#define WINDOW_TITLE   "Chess"
#define BOARD_SQUARES  8
#define SQUARE_SIZE    80
#define BOARD_SIZE     (BOARD_SQUARES * SQUARE_SIZE)
#define SIDEBAR_WIDTH  200
#define WINDOW_W       (BOARD_SIZE + SIDEBAR_WIDTH)
#define WINDOW_H       BOARD_SIZE
#define DEFAULT_AI_DEPTH 3
#define DEFAULT_AI_COLOR BLACK
#define DEFAULT_AI_TIME_MS 350
#define AI_DEPTH_MIN 1
#define AI_DEPTH_MAX 6
#define AI_TIME_MIN_MS 50
#define AI_TIME_MAX_MS 3000
#define AI_TIME_STEP_MS 50
#define AI_LIVE_PERF_LOG_PATH "docs/live-ai-performance.csv"

// Colours
#define COL_LIGHT_SQ    { 240, 217, 181, 255 }
#define COL_DARK_SQ     { 181, 136,  99, 255 }
#define COL_SELECTED    { 104, 159,  56, 115 }
#define COL_VALID_MOVE  { 104, 159,  56, 200 }
#define COL_CHECK       { 196,  66,  66, 170 }
#define COL_BOARD_FRAME {  38,  26,  20, 255 }
#define COL_GRID        {  80,  60,  40, 110 }
#define COL_SIDEBAR_BG  {  24,  26,  30, 255 }
#define COL_PANEL       {  38,  42,  48, 255 }
#define COL_ACCENT      { 106, 168,  79, 255 }
#define COL_TEXT        { 220, 220, 220, 255 }
#define COL_MUTED_TEXT  { 170, 176, 186, 255 }
#define COL_WHITE_PIECE { 255, 255, 255, 255 }
#define COL_BLACK_PIECE {  20,  20,  20, 255 }
#define COL_PIECE_RING  {  95,  95,  95, 255 }
#define COL_LAST_MOVE   { 248, 221, 120, 96 }
#define COL_HOVER       { 120, 170, 230, 84 }



// ═══════════════════════════════════════════════════════════════
//  GUI STATE
// ═══════════════════════════════════════════════════════════════

typedef struct {
    SDL_Window   *window;
    SDL_Renderer *renderer;

    int  selected;        // selected square index, or EMPTY
    int  turn;            // WHITE or BLACK
    int  valid_moves[64]; // bitmask of valid destinations for selected piece
    int  move_count;      // how many valid destinations
    char status[128];     // status bar text
    int  game_over;
    int  hovered;         // hovered square index
    int  last_from;
    int  last_to;
    int  move_number;
    char last_move_uci[8];
    int  ai_enabled;
    int  ai_color;
    int  ai_depth;
    int  ai_time_ms;
    SearchStats last_ai_stats;
} GUI;

// ═══════════════════════════════════════════════════════════════
//  COORDINATE HELPERS
// ═══════════════════════════════════════════════════════════════

// Square index → pixel rect (board drawn rank 8 at top)
static SDL_FRect square_rect(int sq_idx) {
    int file = sq_idx % 8;
    int rank = sq_idx / 8;
    return (SDL_FRect){
        .x = file * SQUARE_SIZE,
        .y = (7 - rank) * SQUARE_SIZE,
        .w = SQUARE_SIZE,
        .h = SQUARE_SIZE
    };
}

// Pixel → square index, or EMPTY if outside board
static int pixel_to_square(int x, int y) {
    if (x < 0 || x >= BOARD_SIZE || y < 0 || y >= BOARD_SIZE) return EMPTY;
    int file = x / SQUARE_SIZE;
    int rank = 7 - (y / SQUARE_SIZE);
    return rank * 8 + file;
}

// ═══════════════════════════════════════════════════════════════
//  VALID MOVE CACHING
// ═══════════════════════════════════════════════════════════════

static void cache_valid_moves(GUI *gui, int from) {
    gui->move_count = 0;
    for (int dest = 0; dest < 64; dest++) {
        if (is_valid(from, dest)) {
            gui->valid_moves[gui->move_count++] = dest;
        }
    }
}

static int is_valid_destination(GUI *gui, int dest) {
    for (int i = 0; i < gui->move_count; i++)
        if (gui->valid_moves[i] == dest) return 1;
    return 0;
}

static int is_ai_turn(const GUI *gui) {
    return gui->ai_enabled && gui->turn == gui->ai_color;
}

static void square_to_label(int sq_idx, char out[3]) {
    if (sq_idx < 0 || sq_idx > 63) {
        out[0] = '-';
        out[1] = '-';
        out[2] = '\0';
        return;
    }
    out[0] = (char)('A' + (sq_idx % 8));
    out[1] = (char)('1' + (sq_idx / 8));
    out[2] = '\0';
}

static void update_last_move(GUI *gui, int from, int to) {
    char from_label[3];
    char to_label[3];
    square_to_label(from, from_label);
    square_to_label(to, to_label);

    gui->last_from = from;
    gui->last_to = to;
    gui->move_number++;
    snprintf(gui->last_move_uci, sizeof(gui->last_move_uci), "%s%s", from_label, to_label);
}

static FILE *open_ai_log_file(void) {
    static const char *paths[] = {
        AI_LIVE_PERF_LOG_PATH,
        "../docs/live-ai-performance.csv",
        "live-ai-performance.csv"
    };
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        FILE *f = fopen(paths[i], "a+");
        if (f) return f;
    }
    return NULL;
}

static void append_ai_performance_log(const GUI *gui, const Move *move) {
    if (!gui || !move) return;

    FILE *f = open_ai_log_file();
    if (!f) return;

    if (fseek(f, 0, SEEK_END) == 0) {
        long size = ftell(f);
        if (size == 0) {
            fprintf(f, "tick_ms,move_number,side,move,depth,nodes,time_ms,score_cp,nps\n");
        }
    }

    char from_label[3];
    char to_label[3];
    square_to_label(move->from, from_label);
    square_to_label(move->to, to_label);

    uint64_t nodes = gui->last_ai_stats.nodes;
    int time_ms = gui->last_ai_stats.time_ms;
    int effective_time_ms = (time_ms > 0) ? time_ms : ((nodes > 0) ? 1 : 0);
    uint64_t nps = (effective_time_ms > 0) ? (nodes * 1000ULL) / (uint64_t)effective_time_ms : 0;

    fprintf(f, "%u,%d,%s,%s%s,%d,%" PRIu64 ",%d,%d,%" PRIu64 "\n",
            (unsigned int)SDL_GetTicks(),
            gui->move_number + 1,
            gui->turn == WHITE ? "white" : "black",
            from_label,
            to_label,
            gui->last_ai_stats.depth_completed,
            nodes,
            effective_time_ms,
            gui->last_ai_stats.score_cp,
            nps);
    fclose(f);
}

static void set_status(GUI *gui, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(gui->status, sizeof(gui->status), fmt, args);
    va_end(args);
    SDL_SetWindowTitle(gui->window, gui->status);
}

// ═══════════════════════════════════════════════════════════════
//  DRAWING HELPERS
// ═══════════════════════════════════════════════════════════════

static void set_color(SDL_Renderer *r, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}

static char piece_letter(int piece) {
    switch (piece) {
        case PAWN:   return 'P';
        case KNIGHT: return 'N';
        case BISHOP: return 'B';
        case ROOK:   return 'R';
        case QUEEN:  return 'Q';
        case KING:   return 'K';
        default:     return '?';
    }
}

static void draw_text(SDL_Renderer *r, float x, float y, SDL_Color c, const char *text) {
    set_color(r, c);
    SDL_RenderDebugText(r, x, y, text);
}

static void draw_text_upper(SDL_Renderer *r, float x, float y, SDL_Color c, const char *text) {
    char buffer[128];
    size_t len = strlen(text);
    if (len >= sizeof(buffer)) len = sizeof(buffer) - 1;
    for (size_t i = 0; i < len; i++) {
        buffer[i] = (char)toupper((unsigned char)text[i]);
    }
    buffer[len] = '\0';
    draw_text(r, x, y, c, buffer);
}

static void fill_circle(SDL_Renderer *r, int cx, int cy, int radius);

static void fill_rect_i(SDL_Renderer *r, int x, int y, int w, int h) {
    SDL_FRect rect = { (float)x, (float)y, (float)w, (float)h };
    SDL_RenderFillRect(r, &rect);
}

static void draw_piece_symbol(SDL_Renderer *r, int piece, int color, int cx, int cy, int radius) {
    SDL_Color symbol = (color == WHITE) ? (SDL_Color)COL_BLACK_PIECE
                                        : (SDL_Color)COL_WHITE_PIECE;
    set_color(r, symbol);

    int s = radius / 2;
    if (s < 8) s = 8;

    switch (piece) {
        case PAWN:
            fill_circle(r, cx, cy - s / 2, s / 2);
            fill_rect_i(r, cx - s / 4, cy - s / 8, s / 2, s);
            fill_rect_i(r, cx - s / 2, cy + s / 2, s, s / 4);
            break;

        case KNIGHT:
            fill_circle(r, cx - s / 6, cy - s / 3, s / 3);
            SDL_RenderLine(r, cx - s / 2, cy + s / 2, cx + s / 2, cy + s / 2);
            SDL_RenderLine(r, cx - s / 2, cy + s / 2, cx - s / 8, cy - s / 2);
            SDL_RenderLine(r, cx - s / 8, cy - s / 2, cx + s / 2, cy - s / 10);
            SDL_RenderLine(r, cx + s / 2, cy - s / 10, cx + s / 4, cy + s / 2);
            SDL_RenderLine(r, cx + s / 4, cy + s / 2, cx - s / 2, cy + s / 2);
            fill_rect_i(r, cx - s / 2, cy + s / 2, s, s / 4);
            break;

        case BISHOP:
            fill_circle(r, cx, cy - s / 2, s / 2);
            fill_rect_i(r, cx - s / 4, cy - s / 8, s / 2, s);
            SDL_RenderLine(r, cx - s / 3, cy - s / 2, cx + s / 3, cy + s / 6);
            fill_rect_i(r, cx - s / 2, cy + s / 2, s, s / 4);
            break;

        case ROOK:
            fill_rect_i(r, cx - s / 2, cy - s / 2, s, s);
            fill_rect_i(r, cx - s / 2, cy - s / 2, s / 6, s / 4);
            fill_rect_i(r, cx - s / 6, cy - s / 2, s / 6, s / 4);
            fill_rect_i(r, cx + s / 6, cy - s / 2, s / 6, s / 4);
            fill_rect_i(r, cx - s / 3, cy + s / 2, (2 * s) / 3, s / 4);
            break;

        case QUEEN:
            fill_circle(r, cx - s / 2, cy - s / 2, s / 6);
            fill_circle(r, cx, cy - (2 * s) / 3, s / 6);
            fill_circle(r, cx + s / 2, cy - s / 2, s / 6);
            fill_rect_i(r, cx - s / 2, cy - s / 4, s, s / 2);
            fill_rect_i(r, cx - s / 3, cy + s / 4, (2 * s) / 3, s / 3);
            fill_rect_i(r, cx - s / 2, cy + s / 2, s, s / 4);
            break;

        case KING:
            fill_rect_i(r, cx - s / 6, cy - s / 2, s / 3, s);
            fill_rect_i(r, cx - s / 2, cy - s / 6, s, s / 3);
            fill_rect_i(r, cx - s / 3, cy + s / 3, (2 * s) / 3, s / 3);
            fill_rect_i(r, cx - s / 2, cy + s / 2, s, s / 4);
            break;
    }
}

// Draw a filled circle (used for piece bodies and move dots)
static void fill_circle(SDL_Renderer *r, int cx, int cy, int radius) {
    for (int dy = -radius; dy <= radius; dy++) {
        int dx = (int)SDL_sqrt((double)(radius * radius - dy * dy));
        SDL_RenderLine(r, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}

// Draw a piece as a labelled circle
static void draw_piece(SDL_Renderer *r, int sq_idx, int piece, int color) {
    SDL_FRect rect = square_rect(sq_idx);
    int cx = (int)(rect.x + SQUARE_SIZE / 2);
    int cy = (int)(rect.y + SQUARE_SIZE / 2);
    int radius = SQUARE_SIZE / 2 - 10;

    // Outer ring
    SDL_Color ring = COL_PIECE_RING;
    set_color(r, ring);
    fill_circle(r, cx, cy, radius + 2);

    // Piece body
    SDL_Color body = (color == WHITE) ? (SDL_Color)COL_WHITE_PIECE
                                      : (SDL_Color)COL_BLACK_PIECE;
    set_color(r, body);
    fill_circle(r, cx, cy, radius);

    draw_piece_symbol(r, piece, color, cx, cy, radius);
}

static void draw_board(GUI *gui) {
    SDL_Color light = COL_LIGHT_SQ;
    SDL_Color dark  = COL_DARK_SQ;

    for (int sq_idx = 0; sq_idx < 64; sq_idx++) {
        int file = sq_idx % 8;
        int rank = sq_idx / 8;
        SDL_FRect rect = square_rect(sq_idx);

        // Base square colour
        set_color(gui->renderer, (file + rank) % 2 == 0 ? dark : light);
        SDL_SetRenderDrawBlendMode(gui->renderer, SDL_BLENDMODE_NONE);
        SDL_RenderFillRect(gui->renderer, &rect);

        // Last move highlight
        if (sq_idx == gui->last_from || sq_idx == gui->last_to) {
            SDL_Color lm = COL_LAST_MOVE;
            set_color(gui->renderer, lm);
            SDL_SetRenderDrawBlendMode(gui->renderer, SDL_BLENDMODE_BLEND);
            SDL_RenderFillRect(gui->renderer, &rect);
        }

        // Hover highlight
        if (sq_idx == gui->hovered && !is_ai_turn(gui)) {
            SDL_Color hv = COL_HOVER;
            set_color(gui->renderer, hv);
            SDL_SetRenderDrawBlendMode(gui->renderer, SDL_BLENDMODE_BLEND);
            SDL_RenderFillRect(gui->renderer, &rect);
        }

        // Selected square highlight
        if (sq_idx == gui->selected) {
            SDL_Color sel = COL_SELECTED;
            set_color(gui->renderer, sel);
            SDL_SetRenderDrawBlendMode(gui->renderer, SDL_BLENDMODE_BLEND);
            SDL_RenderFillRect(gui->renderer, &rect);

            SDL_Color outline = COL_ACCENT;
            set_color(gui->renderer, outline);
            SDL_RenderRect(gui->renderer, &rect);
        }

        // King in check highlight
        if (sq_piece(sq_idx) == KING && sq_color(sq_idx) == gui->turn
                && in_check(gui->turn)) {
            SDL_Color chk = COL_CHECK;
            set_color(gui->renderer, chk);
            SDL_SetRenderDrawBlendMode(gui->renderer, SDL_BLENDMODE_BLEND);
            SDL_RenderFillRect(gui->renderer, &rect);
        }

        // Valid move dots
        if (is_valid_destination(gui, sq_idx)) {
            SDL_Color vm = COL_VALID_MOVE;
            set_color(gui->renderer, vm);
            SDL_SetRenderDrawBlendMode(gui->renderer, SDL_BLENDMODE_BLEND);
            fill_circle(gui->renderer,
                        (int)(rect.x + rect.w / 2),
                        (int)(rect.y + rect.h / 2),
                        SQUARE_SIZE / 8);
        }
    }

    // Subtle board grid
    SDL_Color grid = COL_GRID;
    set_color(gui->renderer, grid);
    SDL_SetRenderDrawBlendMode(gui->renderer, SDL_BLENDMODE_BLEND);
    for (int i = 1; i < BOARD_SQUARES; i++) {
        float pos = (float)(i * SQUARE_SIZE);
        SDL_RenderLine(gui->renderer, pos, 0, pos, BOARD_SIZE);
        SDL_RenderLine(gui->renderer, 0, pos, BOARD_SIZE, pos);
    }

    // Board frame
    SDL_Color frame = COL_BOARD_FRAME;
    set_color(gui->renderer, frame);
    SDL_SetRenderDrawBlendMode(gui->renderer, SDL_BLENDMODE_NONE);
    SDL_FRect border = { 0, 0, BOARD_SIZE, BOARD_SIZE };
    SDL_RenderRect(gui->renderer, &border);
}

static void draw_pieces(GUI *gui) {
    for (int sq_idx = 0; sq_idx < 64; sq_idx++) {
        int piece = sq_piece(sq_idx);
        int color = sq_color(sq_idx);
        if (piece != EMPTY)
            draw_piece(gui->renderer, sq_idx, piece, color);
    }
}

static void draw_coords(GUI *gui) {
    SDL_Color text = COL_TEXT;

    for (int file = 0; file < 8; file++) {
        char label[2] = { (char)('A' + file), '\0' };
        float x = (float)(file * SQUARE_SIZE + SQUARE_SIZE - 14);
        float y = (float)(BOARD_SIZE - 12);
        draw_text(gui->renderer, x, y, text, label);
    }

    for (int rank = 0; rank < 8; rank++) {
        char label[2] = { (char)('8' - rank), '\0' };
        float x = 4.0f;
        float y = (float)(rank * SQUARE_SIZE + 4);
        draw_text(gui->renderer, x, y, text, label);
    }
}

static void draw_sidebar(GUI *gui) {
    SDL_Color bg = COL_SIDEBAR_BG;
    set_color(gui->renderer, bg);
    SDL_SetRenderDrawBlendMode(gui->renderer, SDL_BLENDMODE_NONE);
    SDL_FRect sidebar = { BOARD_SIZE, 0, SIDEBAR_WIDTH, WINDOW_H };
    SDL_RenderFillRect(gui->renderer, &sidebar);

    // Sidebar panel blocks
    SDL_Color panel = COL_PANEL;
    set_color(gui->renderer, panel);
    SDL_FRect panel_top = { BOARD_SIZE + 12, 14, SIDEBAR_WIDTH - 24, 92 };
    SDL_FRect panel_mid = { BOARD_SIZE + 12, 118, SIDEBAR_WIDTH - 24, 120 };
    SDL_FRect panel_bot = { BOARD_SIZE + 12, 250, SIDEBAR_WIDTH - 24, WINDOW_H - 262 };
    SDL_RenderFillRect(gui->renderer, &panel_top);
    SDL_RenderFillRect(gui->renderer, &panel_mid);
    SDL_RenderFillRect(gui->renderer, &panel_bot);

    SDL_Color text = COL_TEXT;
    SDL_Color muted = COL_MUTED_TEXT;
    draw_text(gui->renderer, BOARD_SIZE + 20, 24, text, "TURN");

    // Turn indicator chip
    SDL_Color turn_col = (gui->turn == WHITE)
                       ? (SDL_Color)COL_WHITE_PIECE
                       : (SDL_Color)COL_BLACK_PIECE;
    set_color(gui->renderer, turn_col);
    SDL_FRect turn_chip = { BOARD_SIZE + 20, 46, 18, 18 };
    SDL_RenderFillRect(gui->renderer, &turn_chip);
    draw_text(gui->renderer, BOARD_SIZE + 46, 50, text,
              gui->turn == WHITE ? "WHITE" : "BLACK");

    draw_text(gui->renderer, BOARD_SIZE + 20, 128, text, "SELECTED");

    // Selected piece details
    if (gui->selected != EMPTY) {
        int piece = sq_piece(gui->selected);
        if (piece != EMPTY) {
            int file = gui->selected % 8;
            int rank = gui->selected / 8;
            char selected_info[32];
            snprintf(selected_info, sizeof(selected_info),
                     "%c @ %c%d",
                     piece_letter(piece),
                     (char)('A' + file),
                     rank + 1);
            draw_text(gui->renderer, BOARD_SIZE + 20, 154, text, selected_info);
        } else {
            draw_text(gui->renderer, BOARD_SIZE + 20, 154, text, "EMPTY");
        }
    } else {
        draw_text(gui->renderer, BOARD_SIZE + 20, 154, text, "NONE");
    }

    draw_text(gui->renderer, BOARD_SIZE + 20, 188, text, "LAST MOVE");
    if (gui->move_number > 0) {
        char last_line[32];
        snprintf(last_line, sizeof(last_line), "#%d %s", gui->move_number, gui->last_move_uci);
        draw_text(gui->renderer, BOARD_SIZE + 20, 212, text, last_line);
    } else {
        draw_text(gui->renderer, BOARD_SIZE + 20, 212, muted, "NONE YET");
    }

    draw_text(gui->renderer, BOARD_SIZE + 20, 260, text, "STATUS");
    draw_text_upper(gui->renderer, BOARD_SIZE + 20, 286, text, gui->status);

    draw_text(gui->renderer, BOARD_SIZE + 20, 338, text, "MODE");
    draw_text(gui->renderer, BOARD_SIZE + 20, 362, text,
              gui->ai_enabled ? "HUMAN VS AI" : "HUMAN VS HUMAN");
    if (gui->ai_enabled) {
        char ai_info[48];
        snprintf(ai_info, sizeof(ai_info), "AI %s D%d T%dMS",
                 gui->ai_color == WHITE ? "WHITE" : "BLACK",
                 gui->ai_depth,
                 gui->ai_time_ms);
        draw_text(gui->renderer, BOARD_SIZE + 20, 384, text, ai_info);

        if (gui->last_ai_stats.nodes > 0) {
            char perf_line[64];
            snprintf(perf_line, sizeof(perf_line), "LAST: D%d %llUN",
                     gui->last_ai_stats.depth_completed,
                     (unsigned long long)gui->last_ai_stats.nodes);
            draw_text(gui->renderer, BOARD_SIZE + 20, 406, muted, perf_line);
        }
    }

    draw_text(gui->renderer, BOARD_SIZE + 20, 430, text, "KEYS");
    draw_text(gui->renderer, BOARD_SIZE + 20, 452, text, "A TOGGLE AI");
    draw_text(gui->renderer, BOARD_SIZE + 20, 474, text, "C AI COLOR");
    draw_text(gui->renderer, BOARD_SIZE + 20, 496, text, "-/+ AI DEPTH");
    draw_text(gui->renderer, BOARD_SIZE + 20, 518, text, ",/. AI TIME");
}

static void render(GUI *gui) {
    SDL_SetRenderDrawColor(gui->renderer, 20, 20, 20, 255);
    SDL_RenderClear(gui->renderer);

    draw_board(gui);
    draw_pieces(gui);
    draw_coords(gui);
    draw_sidebar(gui);

    SDL_RenderPresent(gui->renderer);
}

// ═══════════════════════════════════════════════════════════════
//  INPUT HANDLING
// ═══════════════════════════════════════════════════════════════

static void update_status(GUI *gui) {
    int has_legal = generate_legal_moves(gui->turn, NULL, 1) > 0;

    if (is_draw_by_repetition()) {
        set_status(gui, "Draw by repetition");
        gui->game_over = 1;
    } else if (is_draw_by_fifty_move()) {
        set_status(gui, "Draw by fifty-move rule");
        gui->game_over = 1;
    } else if (!has_legal && in_check(gui->turn)) {
        const char *winner = (gui->turn == WHITE) ? "Black" : "White";
        set_status(gui, "Checkmate! %s wins!", winner);
        gui->game_over = 1;
    } else if (!has_legal) {
        set_status(gui, "Stalemate");
        gui->game_over = 1;
    } else if (in_check(gui->turn)) {
        const char *side = (gui->turn == WHITE) ? "White" : "Black";
        set_status(gui, "%s is in check!", side);
    } else {
        const char *side = (gui->turn == WHITE) ? "White" : "Black";
        set_status(gui, "%s to move", side);
    }
}

static void handle_click(GUI *gui, int x, int y) {
    if (gui->game_over) return;
    if (is_ai_turn(gui)) return;

    int clicked = pixel_to_square(x, y);
    if (clicked == EMPTY) return;

    // Nothing selected yet — try to select a piece of the current player
    if (gui->selected == EMPTY) {
        if (sq_color(clicked) == gui->turn) {
            gui->selected = clicked;
            cache_valid_moves(gui, clicked);
        }
        return;
    }

    // Clicked the same square — deselect
    if (clicked == gui->selected) {
        gui->selected   = EMPTY;
        gui->move_count = 0;
        return;
    }

    // Clicked another friendly piece — reselect
    if (sq_color(clicked) == gui->turn) {
        gui->selected = clicked;
        cache_valid_moves(gui, clicked);
        return;
    }

    // Attempt the move
    if (move_piece(gui->selected, clicked)) {
        update_last_move(gui, gui->selected, clicked);
        gui->selected   = EMPTY;
        gui->move_count = 0;
        gui->turn       = 1 - gui->turn;
        update_status(gui);
    } else {
        // Illegal move — keep selection
        snprintf(gui->status, sizeof(gui->status), "Illegal move!");
        SDL_SetWindowTitle(gui->window, gui->status);
    }
}

static void process_ai_turn(GUI *gui) {
    if (gui->game_over || !is_ai_turn(gui)) return;
    if (generate_legal_moves(gui->turn, NULL, 1) == 0) {
        update_status(gui);
        return;
    }

    set_status(gui, "AI thinking...");

    Move best_move;
    (void)find_best_move_timed(gui->turn, gui->ai_depth, gui->ai_time_ms, &best_move);
    (void)get_last_search_stats(&gui->last_ai_stats);

    if (move_piece(best_move.from, best_move.to)) {
        append_ai_performance_log(gui, &best_move);
        update_last_move(gui, best_move.from, best_move.to);
        gui->selected = EMPTY;
        gui->move_count = 0;
        gui->turn = 1 - gui->turn;
    }

    update_status(gui);
}

static void handle_keydown(GUI *gui, SDL_Keycode key) {
    if (key == SDLK_A) {
        gui->ai_enabled = !gui->ai_enabled;
        gui->selected = EMPTY;
        gui->move_count = 0;
        set_status(gui, gui->ai_enabled ? "Human vs AI enabled" : "Human vs Human mode");
        return;
    }

    if (key == SDLK_C) {
        gui->ai_color = 1 - gui->ai_color;
        set_status(gui, "AI color: %s", gui->ai_color == WHITE ? "White" : "Black");
        return;
    }

    if (key == SDLK_MINUS || key == SDLK_KP_MINUS) {
        if (gui->ai_depth > AI_DEPTH_MIN) gui->ai_depth--;
        set_status(gui, "AI depth: %d", gui->ai_depth);
        return;
    }

    if (key == SDLK_EQUALS || key == SDLK_PLUS || key == SDLK_KP_PLUS) {
        if (gui->ai_depth < AI_DEPTH_MAX) gui->ai_depth++;
        set_status(gui, "AI depth: %d", gui->ai_depth);
        return;
    }

    if (key == SDLK_COMMA) {
        if (gui->ai_time_ms > AI_TIME_MIN_MS) gui->ai_time_ms -= AI_TIME_STEP_MS;
        if (gui->ai_time_ms < AI_TIME_MIN_MS) gui->ai_time_ms = AI_TIME_MIN_MS;
        set_status(gui, "AI time: %d ms", gui->ai_time_ms);
        return;
    }

    if (key == SDLK_PERIOD) {
        if (gui->ai_time_ms < AI_TIME_MAX_MS) gui->ai_time_ms += AI_TIME_STEP_MS;
        if (gui->ai_time_ms > AI_TIME_MAX_MS) gui->ai_time_ms = AI_TIME_MAX_MS;
        set_status(gui, "AI time: %d ms", gui->ai_time_ms);
        return;
    }

    if (key == SDLK_ESCAPE) {
        return;
    }
}

// ═══════════════════════════════════════════════════════════════
//  INIT & CLEANUP
// ═══════════════════════════════════════════════════════════════

static int gui_init(GUI *gui) {
    memset(gui, 0, sizeof(*gui));
    gui->selected   = EMPTY;
    gui->hovered    = EMPTY;
    gui->last_from  = EMPTY;
    gui->last_to    = EMPTY;
    gui->move_number = 0;
    gui->last_move_uci[0] = '\0';
    gui->turn       = WHITE;
    gui->game_over  = 0;
    gui->ai_enabled = 1;
    gui->ai_color   = DEFAULT_AI_COLOR;
    gui->ai_depth   = DEFAULT_AI_DEPTH;
    gui->ai_time_ms = DEFAULT_AI_TIME_MS;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
        return 0;
    }

    gui->window = SDL_CreateWindow(WINDOW_TITLE, WINDOW_W, WINDOW_H, 0);
    if (!gui->window) {
        fprintf(stderr, "SDL_CreateWindow error: %s\n", SDL_GetError());
        return 0;
    }

    gui->renderer = SDL_CreateRenderer(gui->window, NULL);
    if (!gui->renderer) {
        fprintf(stderr, "SDL_CreateRenderer error: %s\n", SDL_GetError());
        return 0;
    }

    SDL_SetRenderDrawBlendMode(gui->renderer, SDL_BLENDMODE_BLEND);
    update_status(gui);
    return 1;
}

static void gui_destroy(GUI *gui) {
    if (gui->renderer) SDL_DestroyRenderer(gui->renderer);
    if (gui->window)   SDL_DestroyWindow(gui->window);
    SDL_Quit();
}

// ═══════════════════════════════════════════════════════════════
//  MAIN LOOP
// ═══════════════════════════════════════════════════════════════
#ifdef GUI_MODE
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    GUI gui;
    if (!gui_init(&gui)) return 1;

    SDL_Event event;
    int running = 1;

    while (running) {
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    running = 0;
                    break;

                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    if (event.button.button == SDL_BUTTON_LEFT)
                        handle_click(&gui,
                                     (int)event.button.x,
                                     (int)event.button.y);
                    break;

                case SDL_EVENT_MOUSE_MOTION:
                    gui.hovered = pixel_to_square((int)event.motion.x, (int)event.motion.y);
                    break;

                case SDL_EVENT_KEY_DOWN:
                    if (event.key.key == SDLK_ESCAPE) {
                        running = 0;
                    } else {
                        handle_keydown(&gui, event.key.key);
                    }
                    break;
            }
        }

        process_ai_turn(&gui);
        render(&gui);
        SDL_Delay(16); // ~60 fps
    }

    gui_destroy(&gui);
    return 0;
}
#endif
