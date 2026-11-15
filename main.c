#include <genesis.h>
#include <string.h>

/*
 * ELIFOOT 16 - Brasileirao 2026 (Serie A e Serie B) para Sega Mega Drive / SGDK
 */

#define DIVS              2
#define DIV_TEAMS         20
#define TEAM_COUNT        (DIVS * DIV_TEAMS)
#define SQUAD_SIZE        11
#define MATCHES_PER_ROUND (DIV_TEAMS / 2)
#define ROUNDS_PER_LEG    (DIV_TEAMS - 1)
#define TOTAL_ROUNDS      (ROUNDS_PER_LEG * 2)
#define MENU_COUNT        6
#define OFFER_COUNT       8
#define MAX_GOALS         15
#define RELEG_COUNT       4

#define BONUS_WIN   300
#define BONUS_DRAW  100

enum { SCR_INTRO, SCR_CLUB, SCR_MENU, SCR_TABLE, SCR_SQUAD, SCR_RESULTS,
       SCR_FINANCE, SCR_MATCH, SCR_MARKET };

typedef struct {
    const char *name;
    u8 base;
} ClubDef;

typedef struct {
    u16 id;
    u8  force;
} Player;

typedef struct {
    u8  points, played, wins, draws, losses;
    u16 goals_for, goals_against;
    s32 cash;
} TeamStats;

static const ClubDef clubs[TEAM_COUNT] = {
    {"Flamengo", 82},      {"Palmeiras", 81},     {"Botafogo", 76},
    {"Cruzeiro", 75},      {"Fluminense", 74},    {"Atletico-MG", 74},
    {"Bahia", 73},         {"Sao Paulo", 73},     {"Corinthians", 73},
    {"Gremio", 72},        {"Internacional", 72}, {"Vasco da Gama", 71},
    {"Santos", 71},        {"Bragantino", 70},    {"Athletico-PR", 70},
    {"Vitoria", 68},       {"Mirassol", 68},      {"Coritiba", 67},
    {"Chapecoense", 65},   {"Remo", 65},

    {"Fortaleza", 68},     {"Ceara", 67},         {"Sport", 66},
    {"Juventude", 64},     {"Goias", 64},         {"Atletico-GO", 63},
    {"Avai", 63},          {"America-MG", 63},    {"Novorizontino", 63},
    {"Criciuma", 62},      {"CRB", 62},           {"Vila Nova", 62},
    {"Cuiaba", 62},        {"Operario-PR", 61},   {"Botafogo-SP", 61},
    {"Ponte Preta", 61},   {"Nautico", 60},       {"Londrina", 59},
    {"Sao Bernardo", 59},  {"Athletic-MG", 58}
};

static const char *const surnames[] = {
    "Silva", "Souza", "Costa", "Santos", "Oliveira", "Pereira", "Lima", "Ferreira",
    "Alves", "Ribeiro", "Carvalho", "Gomes", "Martins", "Rocha", "Araujo", "Barbosa",
    "Melo", "Cardoso", "Teixeira", "Moreira", "Nunes", "Vieira", "Monteiro", "Dias",
    "Mendes", "Freitas", "Ramos", "Castro", "Campos", "Pinto", "Correia", "Azevedo",
    "Cunha", "Andrade", "Moura", "Farias", "Borges", "Lopes", "Machado", "Batista",
    "Duarte", "Fonseca", "Reis", "Siqueira", "Guedes", "Tavares", "Peixoto", "Bento",
    "Viana", "Prado", "Leal", "Brito", "Paiva", "Matos", "Sales", "Nobre",
