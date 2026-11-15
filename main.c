#include <genesis.h>
#include <string.h>

/*
 * ELIFOOT 16 - prototipo v2 para Sega Mega Drive (SGDK)
 *
 * Mudancas em relacao a v1:
 *  - Redesenho apenas quando algo muda (sem cintilacao).
 *  - Calendario round-robin real (7 rodadas de ida + 7 de volta).
 *  - Placar com varios gols possiveis (binomial, media ~1.3 gol/jogo).
 *  - Classificacao ordenada por pontos, saldo e gols pro.
 *  - Elenco por clube, tela de resultados e financas basicas.
 *  - Fim de temporada com campeao e inicio de nova temporada.
 *  - Formatacao propria de texto (nao depende de largura no sprintf).
 */

#define TEAM_COUNT        8
#define SQUAD_SIZE        5
#define MATCHES_PER_ROUND (TEAM_COUNT / 2)
#define ROUNDS_PER_LEG    (TEAM_COUNT - 1)
#define TOTAL_ROUNDS      (ROUNDS_PER_LEG * 2)
#define MENU_COUNT        6

#define GATE_INCOME   40000
#define WAGES         35000
#define BONUS_WIN     25000
#define BONUS_DRAW    10000

enum { SCR_CLUB, SCR_MENU, SCR_TABLE, SCR_SQUAD, SCR_RESULTS, SCR_FINANCE };

typedef struct {
    const char *name;
    u8 attack;
    u8 defense;
    s32 cash;
} TeamDef;

typedef struct {
    u8 points, played, wins, draws, losses;
    u16 goals_for, goals_against;
    s32 cash;
} TeamStats;

static const TeamDef defs[TEAM_COUNT] = {
    {"Aurora FC",    72, 68, 1200000},
    {"Estrela SC",   66, 72,  950000},
    {"Portuario",    75, 61, 1100000},
    {"Real Brasil",  69, 69, 1000000},
    {"Uniao Verde",  63, 74,  850000},
    {"Nacional",     71, 64,  900000},
    {"Atletico Sul", 67, 66,  800000},
    {"Imperial",     64, 63,  750000}
};

static const char *const player_names[TEAM_COUNT * SQUAD_SIZE] = {
    "Joao", "Marcos", "Paulo", "Andre", "Ricardo",
    "Caio", "Davi", "Felipe", "Bruno", "Rafael",
    "Diego", "Leandro", "Tiago", "Gustavo", "Renato",
    "Lucas", "Mateus", "Rodrigo", "Vitor", "Fabio",
    "Hugo", "Otavio", "Samuel", "Daniel", "Alex",
    "Igor", "Murilo", "Nelson", "Edu", "Cesar",
    "Wagner", "Jorge", "Luis", "Pedro", "Sergio",
    "Elias", "Julio", "Flavio", "Thiago", "Moacir"
};

static const char *const pos_names[SQUAD_SIZE] = {
    "GOLEIRO", "ZAGUEIRO", "MEIO-CAMPO", "MEIO-CAMPO", "ATACANTE"
};

static const char *const menu_items[MENU_COUNT] = {
    "JOGAR PROXIMA RODADA", "CLASSIFICACAO", "ELENCO",
    "MERCADO", "FINANCAS", "FIM"
};

static TeamStats stats[TEAM_COUNT];

static u16  screen;
static u16  selected_club;
static u16  user_team;
static u16  menu_pos;
static u16  round_no;      /* proxima rodada a jogar: 1..TOTAL_ROUNDS+1 */
static u16  season_no;
static u16  tick;
static bool dirty;
static char status_line[40];

/* ultimos resultados */
static u16 last_round;
static u16 last_home[MATCHES_PER_ROUND], last_away[MATCHES_PER_ROUND];
static u16 last_hg[MATCHES_PER_ROUND], last_ag[MATCHES_PER_ROUND];

/* financas da temporada (clube do jogador) */
static s32 fin_income, fin_expense, fin_last_net;

/* ---------- formatacao de texto ---------- */

/* copia s para d e completa com espacos ate 'width' (0 = sem preenchimento) */
static char *put_str(char *d, const char *s, u16 width) {
    u16 n = 0;
    while (*s) { *d++ = *s++; n++; }
    while (n < width) { *d++ = ' '; n++; }
    return d;
}

/* escreve inteiro alinhado a direita em 'width' colunas */
static char *put_num(char *d, s32 v, u16 width) {
    char tmp[12];
    u16 n = 0, len, i;
    bool neg = (v < 0);
    u32 u = neg ? (u32)(-v) : (u32)v;
    do { tmp[n++] = (char)('0' + (u % 10)); u /= 10; } while (u);
    if (neg) tmp[n++] = '-';
    len = n;
    for (i = len; i < width; i++) *d++ = ' ';
    while (n) *d++ = tmp[--n];
    return d;
}

static char *put_char(char *d, char c) { *d++ = c; return d; }

static void put_line(char *end, char *start, u16 x, u16 y) {
    *end = '\0';
    VDP_drawText(start, x, y);
}

/* ---------- dados do elenco ---------- */

static u16 player_force(u16 team, u16 idx) {
    s16 base, off, f;
    u16 a = defs[team].attack, d = defs[team].defense;
    switch (idx) {
        case 0: case 1: base = d; break;
        case 2: case 3: base = (a + d) / 2; break;
        default:        base = a; break;
    }
    off = (s16)((team * 7 + idx * 3) % 7) - 3;
    f = base + off;
    if (f < 40) f = 40;
    if (f > 99) f = 99;
    return (u16)f;
}

/* ---------- logica de jogo ---------- */

static void set_status(const char *s) {
    strncpy(status_line, s, sizeof(status_line) - 1);
    status_line[sizeof(status_line) - 1] = '\0';
}

static void reset_season(void) {
    for (u16 i = 0; i < TEAM_COUNT; i++) {
        s32 cash = stats[i].cash;
        memset(&stats[i], 0, sizeof(TeamStats));
        stats[i].cash = cash;
    }
    round_no = 1;
    last_round = 0;
    fin_income = fin_expense = fin_last_net = 0;
}

static void reset_game(void) {
    for (u16 i = 0; i < TEAM_COUNT; i++) stats[i].cash = defs[i].cash;
    season_no = 1;
    reset_season();
    menu_pos = 0;
    set_status("BOA SORTE, TREINADOR!");
}

/* a esta melhor que b na tabela? */
static bool better(u16 a, u16 b) {
    s16 gda = (s16)stats[a].goals_for - (s16)stats[a].goals_against;
    s16 gdb = (s16)stats[b].goals_for - (s16)stats[b].goals_against;
    if (stats[a].points != stats[b].points) return stats[a].points > stats[b].points;
    if (gda != gdb) return gda > gdb;
    if (stats[a].goals_for != stats[b].goals_for) return stats[a].goals_for > stats[b].goals_for;
    return a < b;
}

static void sorted_table(u16 *order) {
    for (u16 i = 0; i < TEAM_COUNT; i++) order[i] = i;
    for (u16 i = 1; i < TEAM_COUNT; i++) {
        u16 v = order[i];
        s16 j = (s16)i - 1;
        while (j >= 0 && better(v, order[j])) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = v;
    }
}

/* metodo do circulo: time 7 fixo, os outros 7 giram */
static void get_fixture(u16 rnd, u16 slot, u16 *home, u16 *away) {
    u16 r = rnd % ROUNDS_PER_LEG;
    u16 a, b, t;
    if (slot == 0) {
        a = TEAM_COUNT - 1;
        b = r;
    } else {
        a = (r + slot) % ROUNDS_PER_LEG;
        b = (r + ROUNDS_PER_LEG - slot) % ROUNDS_PER_LEG;
    }
    if ((r + slot) & 1) { t = a; a = b; b = t; }
    if (rnd >= ROUNDS_PER_LEG) { t = a; a = b; b = t; } /* returno: inverte mando */
    *home = a;
    *away = b;
}

/* 10 chances, cada uma com chance p% -> media = lambda/100 gols */
static u16 roll_goals(s16 lambda100) {
    u16 p, g = 0;
    if (lambda100 < 30) lambda100 = 30;
    if (lambda100 > 350) lambda100 = 350;
    p = (u16)lambda100 / 10;
    for (u16 i = 0; i < 10; i++)
        if ((random() % 100) < p) g++;
    return g;
}

static void apply_result(u16 t, u16 gf, u16 ga) {
    stats[t].played++;
    stats[t].goals_for += gf;
    stats[t].goals_against += ga;
    if (gf > ga)      { stats[t].wins++;   stats[t].points += 3; }
    else if (gf == ga){ stats[t].draws++;  stats[t].points += 1; }
    else              { stats[t].losses++; }
}

static void play_round(void) {
    s32 net;
    if (round_no > TOTAL_ROUNDS) {
        season_no++;
        reset_season();
        set_status("NOVA TEMPORADA!");
        return;
    }

    for (u16 m = 0; m < MATCHES_PER_ROUND; m++) {
        u16 h, a, hg, ag;
        get_fixture(round_no - 1, m, &h, &a);
        hg = roll_goals(130 + ((s16)defs[h].attack - (s16)defs[a].defense) * 4 + 15);
        ag = roll_goals(130 + ((s16)defs[a].attack - (s16)defs[h].defense) * 4 - 15);
        apply_result(h, hg, ag);
        apply_result(a, ag, hg);
        last_home[m] = h; last_away[m] = a;
        last_hg[m] = hg;  last_ag[m] = ag;
    }
    last_round = round_no;

    /* financas de todos os clubes; o jogador acompanha o proprio */
    for (u16 m = 0; m < MATCHES_PER_ROUND; m++) {
        for (u16 side = 0; side < 2; side++) {
            u16 t  = side ? last_away[m] : last_home[m];
            u16 gf = side ? last_ag[m]   : last_hg[m];
            u16 ga = side ? last_hg[m]   : last_ag[m];
            s32 inc = (side == 0) ? GATE_INCOME : 0;
            if (gf > ga)       inc += BONUS_WIN;
            else if (gf == ga) inc += BONUS_DRAW;
            net = inc - WAGES;
            stats[t].cash += net;
            if (t == user_team) {
                fin_income += inc;
                fin_expense += WAGES;
                fin_last_net = net;
            }
        }
    }

    round_no++;
    if (round_no > TOTAL_ROUNDS) {
        u16 order[TEAM_COUNT];
        char buf[40], *p;
        sorted_table(order);
        p = put_str(buf, "CAMPEAO: ", 0);
        p = put_str(p, defs[order[0]].name, 0);
        *p = '\0';
        set_status(buf);
    } else {
        set_status("RODADA SIMULADA!");
    }
}

/* ---------- telas ---------- */

static void draw_frame(const char *subtitle) {
    VDP_clearPlane(BG_A, TRUE);
    VDP_drawText("================================", 4, 2);
    VDP_drawText("          ELIFOOT 16            ", 4, 3);
    VDP_drawText("================================", 4, 4);
    VDP_drawText(subtitle, 4, 6);
}

static void draw_club_select(void) {
    char line[40], *p;
    draw_frame("SELECIONE SEU CLUBE");
    for (u16 i = 0; i < TEAM_COUNT; i++) {
        p = put_char(line, i == selected_club ? '>' : ' ');
        p = put_char(p, ' ');
        p = put_str(p, defs[i].name, 14);
        p = put_str(p, "ATA ", 0);
        p = put_num(p, defs[i].attack, 2);
        p = put_str(p, " DEF ", 0);
        p = put_num(p, defs[i].defense, 2);
        put_line(p, line, 4, 9 + i);
    }
    VDP_drawText("A: CONFIRMAR   CIMA/BAIXO: ESCOLHER", 2, 21);
}

static void draw_main_menu(void) {
    char line[40], *p;
    draw_frame(defs[user_team].name);

    p = put_str(line, "TEMPORADA ", 0);
    p = put_num(p, season_no, 0);
    p = put_str(p, "  -  RODADA ", 0);
    p = put_num(p, round_no > TOTAL_ROUNDS ? TOTAL_ROUNDS : round_no, 0);
    p = put_char(p, '/');
    p = put_num(p, TOTAL_ROUNDS, 0);
    put_line(p, line, 7, 7);

    p = put_str(line, "CAIXA: $", 0);
    p = put_num(p, stats[user_team].cash, 0);
    put_line(p, line, 7, 8);

    for (u16 i = 0; i < MENU_COUNT; i++) {
        p = put_char(line, i == menu_pos ? '>' : ' ');
        p = put_char(p, ' ');
        if (i == 0 && round_no > TOTAL_ROUNDS) p = put_str(p, "NOVA TEMPORADA", 0);
        else p = put_str(p, menu_items[i], 0);
        put_line(p, line, 8, 11 + i * 2);
    }
    VDP_drawText(status_line, 4, 23);
    VDP_drawText("CIMA/BAIXO: MENU   A: OK", 8, 25);
}

static void draw_table(void) {
    u16 order[TEAM_COUNT];
    char line[40], *p;
    draw_frame("CLASSIFICACAO");
    sorted_table(order);

    p = put_str(line, "   CLUBE", 16);
    p = put_str(p, "  P  J  V  E  D  SG", 0);
    put_line(p, line, 2, 8);

    for (u16 i = 0; i < TEAM_COUNT; i++) {
        u16 t = order[i];
        p = put_char(line, t == user_team ? '>' : ' ');
        p = put_num(p, i + 1, 2);
        p = put_char(p, ' ');
        p = put_str(p, defs[t].name, 13);
        p = put_num(p, stats[t].points, 3);
        p = put_num(p, stats[t].played, 3);
        p = put_num(p, stats[t].wins, 3);
        p = put_num(p, stats[t].draws, 3);
        p = put_num(p, stats[t].losses, 3);
        p = put_num(p, (s16)stats[t].goals_for - (s16)stats[t].goals_against, 4);
        put_line(p, line, 2, 10 + i);
    }
    VDP_drawText("B: VOLTAR", 4, 22);
}

static void draw_squad(void) {
    char line[40], *p;
    draw_frame("ELENCO");

    p = put_str(line, defs[user_team].name, 0);
    p = put_str(p, "  ATA ", 0);
    p = put_num(p, defs[user_team].attack, 2);
    p = put_str(p, " DEF ", 0);
    p = put_num(p, defs[user_team].defense, 2);
    put_line(p, line, 4, 8);

    for (u16 i = 0; i < SQUAD_SIZE; i++) {
        p = put_str(line, pos_names[i], 12);
        p = put_str(p, player_names[user_team * SQUAD_SIZE + i], 10);
        p = put_str(p, "FOR ", 0);
        p = put_num(p, player_force(user_team, i), 2);
        put_line(p, line, 4, 11 + i * 2);
    }
    VDP_drawText("B: VOLTAR", 4, 22);
}

static void draw_results(void) {
    char line[40], *p;
    draw_frame("");
    p = put_str(line, "RESULTADOS - RODADA ", 0);
    p = put_num(p, last_round, 0);
    put_line(p, line, 4, 6);

    if (last_round == 0) {
        VDP_drawText("NENHUM JOGO DISPUTADO AINDA", 4, 10);
    } else {
        for (u16 m = 0; m < MATCHES_PER_ROUND; m++) {
            bool mine = (last_home[m] == user_team || last_away[m] == user_team);
            p = put_char(line, mine ? '*' : ' ');
            p = put_str(p, defs[last_home[m]].name, 13);
            p = put_num(p, last_hg[m], 2);
            p = put_str(p, " x ", 0);
            p = put_num(p, last_ag[m], 1);
            p = put_char(p, ' ');
            p = put_str(p, defs[last_away[m]].name, 0);
            put_line(p, line, 2, 9 + m * 2);
        }
    }
    VDP_drawText("B: VOLTAR", 4, 22);
}

static void draw_finance(void) {
    char line[40], *p;
    draw_frame("FINANCAS");

    p = put_str(line, "CAIXA ATUAL:    $", 0);
    p = put_num(p, stats[user_team].cash, 0);
    put_line(p, line, 4, 9);

    p = put_str(line, "RECEITA TEMP.:  $", 0);
    p = put_num(p, fin_income, 0);
    put_line(p, line, 4, 11);

    p = put_str(line, "SALARIOS TEMP.: $", 0);
    p = put_num(p, fin_expense, 0);
    put_line(p, line, 4, 13);

    p = put_str(line, "ULTIMA RODADA:  $", 0);
    p = put_num(p, fin_last_net, 0);
    put_line(p, line, 4, 15);

    VDP_drawText("B: VOLTAR", 4, 22);
}

static void redraw(void) {
    switch (screen) {
        case SCR_CLUB:    draw_club_select(); break;
        case SCR_MENU:    draw_main_menu();   break;
        case SCR_TABLE:   draw_table();       break;
        case SCR_SQUAD:   draw_squad();       break;
        case SCR_RESULTS: draw_results();     break;
        case SCR_FINANCE: draw_finance();     break;
    }
}

static void go(u16 s) {
    screen = s;
    dirty = TRUE;
}

/* ---------- principal ---------- */

int main(bool hardReset) {
    u16 previous = 0;

    JOY_init();
    VDP_setScreenWidth320();
    VDP_setTextPlane(BG_A);
    VDP_setTextPalette(PAL0);
    VDP_setTextPriority(FALSE);
    VDP_setBackgroundColor(0);

    reset_game();
    selected_club = 0;
    go(SCR_CLUB);

    while (TRUE) {
        u16 pad = JOY_readJoypad(JOY_1);
        u16 pressed = pad & ~previous;
        previous = pad;
        tick++;

        if (screen == SCR_CLUB) {
            if (pressed & BUTTON_UP)   { selected_club = (selected_club + TEAM_COUNT - 1) % TEAM_COUNT; dirty = TRUE; }
            if (pressed & BUTTON_DOWN) { selected_club = (selected_club + 1) % TEAM_COUNT; dirty = TRUE; }
            if (pressed & BUTTON_A) {
                setRandomSeed(tick);   /* tempo ate o jogador confirmar = semente */
                user_team = selected_club;
                go(SCR_MENU);
            }
        } else if (screen == SCR_MENU) {
            if (pressed & BUTTON_UP)   { menu_pos = (menu_pos + MENU_COUNT - 1) % MENU_COUNT; dirty = TRUE; }
            if (pressed & BUTTON_DOWN) { menu_pos = (menu_pos + 1) % MENU_COUNT; dirty = TRUE; }
            if (pressed & BUTTON_A) {
                switch (menu_pos) {
                    case 0:
                        play_round();
                        if (last_round != 0 && round_no != 1) go(SCR_RESULTS);
                        else dirty = TRUE;
                        break;
                    case 1: go(SCR_TABLE); break;
                    case 2: go(SCR_SQUAD); break;
                    case 3: set_status("MERCADO: EM DESENVOLVIMENTO"); dirty = TRUE; break;
                    case 4: go(SCR_FINANCE); break;
                    default:
                        reset_game();
                        go(SCR_CLUB);
                        break;
                }
            }
        } else {
            if (pressed & BUTTON_B) go(SCR_MENU);
        }

        if (dirty) {
            dirty = FALSE;
            redraw();
        }
        SYS_doVBlankProcess();
    }
    return 0;
}
