/*
 * NO MAN'S BOY - exploracao espacial para Game Boy
 * Compile com GBDK-2020:  lcc -o nomanboy.gb nomanboy.c
 *
 * Voce pousa em planetas gerados proceduralmente (cada um com nome,
 * bioma e mapa proprios). Colete cristais, cataloque a fauna e use
 * 3 cristais como combustivel para saltar ao proximo planeta.
 * Seu oxigenio (coracoes) acaba com o tempo!
 *
 * Controles:
 *   D-Pad = andar
 *   A     = interagir com o que esta na sua frente
 *           (nave = decolar, criatura = catalogar)
 *   START = comecar / recomecar
 *
 * Todos os graficos sao originais, no estilo top-down de 4 tons
 * dos RPGs classicos de Game Boy.
 */

#include <gb/gb.h>
#include <gbdk/console.h>
#include <gbdk/font.h>
#include <stdio.h>

#define W 20
#define H 18
#define SHIP_C 9
#define SHIP_R 8

/* tipos de celula do mapa */
#define T_GROUND 0
#define T_ROCK   1
#define T_WATER  2
#define T_GEM    3
#define T_SHIP   4

/* ---------- graficos (2bpp, 8x8) ---------- */
const unsigned char bkg_tiles[] = {
    /* 0: chao temperado (grama) */
    0x00,0x00, 0x00,0x00, 0x22,0x00, 0x00,0x00,
    0x00,0x00, 0x44,0x00, 0x00,0x00, 0x00,0x00,
    /* 1: chao deserto */
    0x00,0x00, 0x0E,0x00, 0x00,0x00, 0x00,0x00,
    0x70,0x00, 0x00,0x00, 0x00,0x00, 0x1C,0x00,
    /* 2: chao toxico */
    0x00,0x24, 0x00,0x00, 0x00,0x42, 0x00,0x00,
    0x00,0x24, 0x00,0x00, 0x00,0x42, 0x00,0x00,
    /* 3: chao gelado */
    0x01,0x00, 0x02,0x00, 0x04,0x00, 0x08,0x00,
    0x10,0x00, 0x20,0x00, 0x40,0x00, 0x80,0x00,
    /* 4: rocha */
    0x00,0x00, 0x3C,0x00, 0x7E,0x3C, 0xFF,0x7E,
    0xFF,0x7E, 0xFF,0xFF, 0x7E,0x7E, 0x00,0x00,
    /* 5: agua */
    0x00,0xFF, 0x00,0xFF, 0x66,0xFF, 0x99,0xFF,
    0x00,0xFF, 0x00,0xFF, 0x66,0xFF, 0x99,0xFF,
    /* 6: cristal */
    0x00,0x00, 0x18,0x18, 0x3C,0x3C, 0x7E,0x5A,
    0x3C,0x3C, 0x18,0x18, 0x00,0x00, 0x00,0x00,
    /* 7: nave */
    0x18,0x18, 0x3C,0x3C, 0x7E,0x7E, 0xFF,0xDB,
    0xFF,0xFF, 0xFF,0xFF, 0x66,0x66, 0x42,0x42
};

const unsigned char spr_tiles[] = {
    /* 0: astronauta */
    0x3C,0x3C, 0x7E,0x42, 0x7E,0x5A, 0x3C,0x3C,
    0x7E,0x7E, 0xBD,0xBD, 0x3C,0x3C, 0x66,0x66,
    /* 1: coracao (oxigenio) */
    0x00,0x00, 0x66,0x66, 0xFF,0xFF, 0xFF,0xFF,
    0x7E,0x7E, 0x3C,0x3C, 0x18,0x18, 0x00,0x00,
    /* 2: cristal (combustivel) */
    0x00,0x00, 0x18,0x18, 0x3C,0x3C, 0x7E,0x7E,
    0x3C,0x3C, 0x18,0x18, 0x00,0x00, 0x00,0x00,
    /* 3: criatura */
    0x00,0x00, 0x3C,0x3C, 0x7E,0x7E, 0xDB,0xFF,
    0xFF,0xFF, 0x7E,0x7E, 0x66,0x66, 0x00,0x00
};

/* sprites: 0 jogador | 1..5 coracoes | 6..8 cristais | 9 criatura */
#define SPR_HEART 1
#define SPR_GEM   6
#define SPR_CREAT 9

/* ---------- textos ---------- */
const char * const syl[8] = { "KA", "LO", "MI", "RA", "TE", "ZU", "NO", "VE" };
const char * const biome_name[4] = { "TEMPERADO", "DESERTO", "TOXICO", "GELADO" };
/* frames por coracao de oxigenio em cada bioma */
const UINT16 drain[4] = { 300, 260, 150, 200 };

const INT8 ddx[4] = { 1, -1, 0, 0 };
const INT8 ddy[4] = { 0, 0, 1, -1 };

/* ---------- estado ---------- */
UBYTE map[H][W];
UINT16 seed;
UBYTE biome, n0, n1, n2;
UBYTE pc, pr;            /* jogador (celula) */
INT8  fdx, fdy;          /* direcao que o jogador olha */
UBYTE life, crystals, planets, fauna;
UINT16 lifet;
UBYTE move_cd;
UBYTE cc, cr, ccd;       /* criatura */

/* ---------- utilidades ---------- */
UBYTE rnd(void) {
    seed = seed * 25173u + 13849u;
    return (UBYTE)(seed >> 8);
}

UBYTE absd(UBYTE a, UBYTE b) {
    return (a > b) ? (a - b) : (b - a);
}

UBYTE tile_id(UBYTE v) {
    if (v == T_GROUND) return biome;
    return v + 3;               /* 1->4, 2->5, 3->6, 4->7 */
}

void draw_map(void) {
    static UBYTE buf[W * H];
    UBYTE r, c;
    for (r = 0; r < H; r++)
        for (c = 0; c < W; c++)
            buf[r * W + c] = tile_id(map[r][c]);
    set_bkg_tiles(0, 0, W, H, buf);
}

void draw_cell(UBYTE c, UBYTE r) {
    UBYTE t = tile_id(map[r][c]);
    set_bkg_tiles(c, r, 1, 1, &t);
}

UBYTE walkable(UBYTE c, UBYTE r) {
    UBYTE v = map[r][c];
    if (c == cc && r == cr) return 0;
    return (v == T_GROUND || v == T_GEM);
}

/* ---------- geracao procedural do planeta ---------- */
void place_creature(void) {
    do {
        cc = rnd() % W;
        cr = rnd() % H;
    } while (map[cr][cc] != T_GROUND || (cc == pc && cr == pr));
}

void gen_planet(void) {
    UBYTE r, c, k, n, tries;

    n0 = rnd() & 7;
    n1 = rnd() & 7;
    n2 = rnd() & 7;
    biome = rnd() & 3;

    for (r = 0; r < H; r++) {
        for (c = 0; c < W; c++) {
            k = rnd() & 31;
            if (k < 3)      map[r][c] = T_ROCK;
            else if (k < 5) map[r][c] = T_WATER;
            else            map[r][c] = T_GROUND;
        }
    }

    /* area limpa ao redor da nave */
    for (r = SHIP_R - 1; r <= SHIP_R + 2; r++)
        for (c = SHIP_C - 1; c <= SHIP_C + 1; c++)
            map[r][c] = T_GROUND;
    map[SHIP_R][SHIP_C] = T_SHIP;

    pc = SHIP_C;
    pr = SHIP_R + 1;
    fdx = 0; fdy = -1;

    /* cristais */
    n = 0; tries = 0;
    while (n < 6 && tries < 200) {
        tries++;
        c = rnd() % W;
        r = rnd() % H;
        if (map[r][c] == T_GROUND &&
            (absd(c, SHIP_C) > 2 || absd(r, SHIP_R) > 2)) {
            map[r][c] = T_GEM;
            n++;
        }
    }

    place_creature();
    ccd = 0;
    lifet = 0;
    move_cd = 0;
}

/* ---------- telas de texto ---------- */
void text_begin(void) {
    HIDE_SPRITES;
    font_init();
    font_set(font_load(font_ibm));
    cls();
}

void wait_button(UBYTE btn) {
    waitpadup();
    waitpad(btn);
    waitpadup();
}

void info_screen(const char *a, const char *b, const char *c, UBYTE btn) {
    text_begin();
    gotoxy(1, 4); printf("%s", a);
    gotoxy(1, 6); printf("%s", b);
    gotoxy(1, 8); printf("%s", c);
    gotoxy(1, 14);
    if (btn == J_START) printf("START: continuar");
    else                printf("A: continuar");
    wait_button(btn);
}

void arrival_screen(void) {
    text_begin();
    gotoxy(1, 2);  printf("PLANETA:");
    gotoxy(1, 4);  printf("%s%s%s", syl[n0], syl[n1], syl[n2]);
    gotoxy(1, 6);  printf("BIOMA: %s", biome_name[biome]);
    gotoxy(1, 9);  printf("PLANETAS: %u", (unsigned int)planets);
    gotoxy(1, 10); printf("FAUNA:    %u", (unsigned int)fauna);
    gotoxy(1, 14); printf("A: descer");
    wait_button(J_A);
}

/* ---------- sprites ---------- */
void update_sprites(void) {
    UBYTE i, g;

    move_sprite(0, pc * 8 + 8, pr * 8 + 16);
    move_sprite(SPR_CREAT, cc * 8 + 8, cr * 8 + 16);

    for (i = 0; i < 5; i++) {
        if (i < life) move_sprite(SPR_HEART + i, 8 + i * 8, 16);
        else          move_sprite(SPR_HEART + i, 0, 0);
    }

    g = (crystals > 3) ? 3 : crystals;
    for (i = 0; i < 3; i++) {
        if (i < g) move_sprite(SPR_GEM + i, 8 + (17 + i) * 8, 16);
        else       move_sprite(SPR_GEM + i, 0, 0);
    }
}

void redraw_all(void) {
    UBYTE i;
    set_bkg_data(0, 8, bkg_tiles);
    set_sprite_data(0, 4, spr_tiles);
    set_sprite_tile(0, 0);
    for (i = 0; i < 5; i++) set_sprite_tile(SPR_HEART + i, 1);
    for (i = 0; i < 3; i++) set_sprite_tile(SPR_GEM + i, 2);
    set_sprite_tile(SPR_CREAT, 3);
    draw_map();
    SHOW_BKG;
    SHOW_SPRITES;
    update_sprites();
}

/* ---------- partida ---------- */
void start_game(void) {
    life = 5;
    crystals = 0;
    planets = 1;
    fauna = 0;
    gen_planet();
    arrival_screen();
    redraw_all();
}

void main(void) {
    UBYTE j, prev = 0, pressed, nc, nr, d;
    INT8 dx, dy;

    DISPLAY_ON;

    text_begin();
    gotoxy(2, 2);  printf("NO MAN'S BOY");
    gotoxy(1, 5);  printf("Explore planetas,");
    gotoxy(1, 6);  printf("colete cristais e");
    gotoxy(1, 7);  printf("catalogue a fauna.");
    gotoxy(1, 9);  printf("Use 3 cristais p/");
    gotoxy(1, 10); printf("saltar de planeta.");
    gotoxy(3, 14); printf("Aperte START");
    seed = 1;
    while (!(joypad() & J_START)) {   /* semente aleatoria pelo tempo */
        seed += 7;
        wait_vbl_done();
    }
    seed += DIV_REG;
    waitpadup();

    start_game();

    while (1) {
        wait_vbl_done();
        j = joypad();
        pressed = j & ~prev;
        prev = j;

        /* --- andar --- */
        if (move_cd) {
            move_cd--;
        } else {
            dx = 0; dy = 0;
            if (j & J_LEFT)       dx = -1;
            else if (j & J_RIGHT) dx = 1;
            else if (j & J_UP)    dy = -1;
            else if (j & J_DOWN)  dy = 1;

            if (dx || dy) {
                fdx = dx; fdy = dy;
                nc = pc + dx;
                nr = pr + dy;
                if (nc < W && nr < H && walkable(nc, nr)) {
                    pc = nc; pr = nr;
                    if (map[pr][pc] == T_GEM) {
                        map[pr][pc] = T_GROUND;
                        crystals++;
                        draw_cell(pc, pr);
                    }
                }
                move_cd = 7;
            }
        }

        /* --- interagir (A) --- */
        if (pressed & J_A) {
            nc = pc + fdx;
            nr = pr + fdy;
            if (nc < W && nr < H) {
                if (map[nr][nc] == T_SHIP) {
                    if (crystals >= 3) {
                        crystals -= 3;
                        planets++;
                        life = 5;
                        gen_planet();
                        arrival_screen();
                        redraw_all();
                        prev = 0;
                    } else {
                        info_screen("COMBUSTIVEL", "INSUFICIENTE!", "USE 3 CRISTAIS", J_A);
                        redraw_all();
                        prev = 0;
                    }
                } else if (nc == cc && nr == cr) {
                    fauna++;
                    if (life < 5) life++;
                    place_creature();
                    info_screen("FAUNA", "CATALOGADA!", "OXIGENIO +1", J_A);
                    redraw_all();
                    prev = 0;
                }
            }
        }

        /* --- criatura vagando --- */
        ccd++;
        if (ccd >= 40) {
            ccd = 0;
            d = rnd() & 3;
            nc = cc + ddx[d];
            nr = cr + ddy[d];
            if (nc < W && nr < H && map[nr][nc] == T_GROUND &&
                !(nc == pc && nr == pr)) {
                cc = nc; cr = nr;
            }
        }

        /* --- oxigenio --- */
        lifet++;
        if (lifet >= drain[biome]) {
            lifet = 0;
            if (life) life--;
        }

        update_sprites();

        /* --- fim de jogo --- */
        if (life == 0) {
            info_screen("SEM OXIGENIO!", "FIM DA JORNADA", "", J_START);
            start_game();
            prev = 0;
        }
    }
}
