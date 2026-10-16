/*
 * FORTNITE GB - mini battle royale para Game Boy
 * Compile com GBDK-2020:  lcc -o fortnitegb.gb fortnitegb.c
 *
 * Controles:
 *   D-Pad = mover (e mirar)
 *   A     = atirar
 *   B     = construir parede na frente
 *   START = comecar / jogar de novo
 *
 * Objetivo: elimine os 3 inimigos antes que a tempestade
 * (area escura) ou eles acabem com voce. VICTORY ROYALE!
 */

#include <gb/gb.h>
#include <gbdk/console.h>
#include <gbdk/font.h>
#include <stdio.h>

#define W 20
#define H 18
#define NE 3
#define MAXX 152
#define MAXY 136

/* ---------- graficos ---------- */
const unsigned char bkg_tiles[] = {
    /* 0: grama */
    0x00,0x00, 0x00,0x00, 0x08,0x00, 0x00,0x00,
    0x00,0x00, 0x00,0x00, 0x40,0x00, 0x00,0x00,
    /* 1: tempestade */
    0xAA,0xFF, 0x55,0xFF, 0xAA,0xFF, 0x55,0xFF,
    0xAA,0xFF, 0x55,0xFF, 0xAA,0xFF, 0x55,0xFF,
    /* 2: parede */
    0xFF,0xFF, 0x80,0xFF, 0x80,0xFF, 0xFF,0xFF,
    0x08,0xFF, 0x08,0xFF, 0xFF,0xFF, 0x80,0xFF
};

const unsigned char spr_tiles[] = {
    /* 0: jogador */
    0x18,0x18, 0x3C,0x3C, 0x3C,0x24, 0x7E,0x7E,
    0xBD,0xA5, 0x3C,0x3C, 0x24,0x24, 0x66,0x66,
    /* 1: inimigo */
    0x42,0x42, 0x7E,0x7E, 0xDB,0xFF, 0xFF,0xFF,
    0x7E,0x7E, 0x3C,0x3C, 0x66,0x66, 0xC3,0xC3,
    /* 2: bala */
    0x00,0x00, 0x00,0x00, 0x18,0x18, 0x3C,0x3C,
    0x3C,0x3C, 0x18,0x18, 0x00,0x00, 0x00,0x00,
    /* 3: coracao (vida) */
    0x00,0x00, 0x66,0x66, 0xFF,0xFF, 0xFF,0xFF,
    0x7E,0x7E, 0x3C,0x3C, 0x18,0x18, 0x00,0x00
};

/* sprites: 0 jogador | 1..3 inimigos | 4 bala | 5..9 coracoes */
#define SPR_ENEMY  1
#define SPR_BULLET 4
#define SPR_HEART  5

/* ---------- estado do jogo ---------- */
UBYTE wall[H][W];
UBYTE zx0, zy0, zx1, zy1;          /* zona segura (em tiles) */
UBYTE px, py;                      /* jogador (pixels de tela) */
INT8  fdx, fdy;                    /* direcao que o jogador olha */
UBYTE hp;
UBYTE kills;
UINT16 timer;
UBYTE cool;                        /* recarga da construcao */
UBYTE ex[NE], ey[NE], ehp[NE];
UBYTE bactive, bx, by;
INT8  bdx, bdy;

/* ---------- mapa ---------- */
UBYTE tile_at(UBYTE c, UBYTE r) {
    if (wall[r][c]) return 2;
    if (c < zx0 || c > zx1 || r < zy0 || r > zy1) return 1;
    return 0;
}

void draw_map(void) {
    static UBYTE buf[W * H];
    UBYTE r, c;
    for (r = 0; r < H; r++)
        for (c = 0; c < W; c++)
            buf[r * W + c] = tile_at(c, r);
    set_bkg_tiles(0, 0, W, H, buf);
}

void draw_cell(UBYTE c, UBYTE r) {
    UBYTE t = tile_at(c, r);
    set_bkg_tiles(c, r, 1, 1, &t);
}

/* colisao com paredes (hitbox 6x6 dentro do sprite 8x8) */
UBYTE blocked(UBYTE x, UBYTE y) {
    return wall[(y + 1) >> 3][(x + 1) >> 3] ||
           wall[(y + 1) >> 3][(x + 6) >> 3] ||
           wall[(y + 6) >> 3][(x + 1) >> 3] ||
           wall[(y + 6) >> 3][(x + 6) >> 3];
}

UBYTE absdiff(UBYTE a, UBYTE b) {
    return (a > b) ? (a - b) : (b - a);
}

/* ---------- telas de texto ---------- */
void text_screen(UBYTE kind) {
    HIDE_SPRITES;
    font_init();
    font_set(font_load(font_ibm));
    cls();
    if (kind == 0) {
        gotoxy(3, 2);  printf("FORTNITE GB");
        gotoxy(1, 5);  printf("D-PAD: mover");
        gotoxy(1, 6);  printf("A: atirar");
        gotoxy(1, 7);  printf("B: construir");
        gotoxy(1, 9);  printf("Fique fora da");
        gotoxy(1, 10); printf("tempestade!");
        gotoxy(3, 14); printf("Aperte START");
    } else if (kind == 1) {
        gotoxy(2, 5);  printf("VICTORY ROYALE!");
        gotoxy(2, 8);  printf("Elims: %u/%u", (unsigned int)kills, (unsigned int)NE);
        gotoxy(2, 12); printf("START: de novo");
    } else {
        gotoxy(2, 5);  printf("ELIMINADO!");
        gotoxy(2, 8);  printf("Elims: %u/%u", (unsigned int)kills, (unsigned int)NE);
        gotoxy(2, 12); printf("START: de novo");
    }
    waitpadup();
    waitpad(J_START);
    waitpadup();
}

/* ---------- nova partida ---------- */
void new_game(void) {
    UBYTE r, c, i;

    set_bkg_data(0, 3, bkg_tiles);
    set_sprite_data(0, 4, spr_tiles);

    for (r = 0; r < H; r++)
        for (c = 0; c < W; c++)
            wall[r][c] = 0;

    zx0 = 0; zy0 = 0; zx1 = W - 1; zy1 = H - 1;
    px = 76; py = 68;
    fdx = 0; fdy = -1;
    hp = 100; kills = 0; timer = 0; cool = 0; bactive = 0;

    ex[0] = 8;   ey[0] = 24;
    ex[1] = 144; ey[1] = 24;
    ex[2] = 76;  ey[2] = 128;
    for (i = 0; i < NE; i++) ehp[i] = 2;

    set_sprite_tile(0, 0);
    for (i = 0; i < NE; i++) set_sprite_tile(SPR_ENEMY + i, 1);
    set_sprite_tile(SPR_BULLET, 2);
    for (i = 0; i < 5; i++) set_sprite_tile(SPR_HEART + i, 3);

    draw_map();
    SHOW_BKG;
    SHOW_SPRITES;
}

/* ---------- desenho dos sprites ---------- */
void update_sprites(void) {
    UBYTE i, hearts;

    move_sprite(0, px + 8, py + 16);

    for (i = 0; i < NE; i++) {
        if (ehp[i]) move_sprite(SPR_ENEMY + i, ex[i] + 8, ey[i] + 16);
        else        move_sprite(SPR_ENEMY + i, 0, 0);
    }

    if (bactive) move_sprite(SPR_BULLET, bx + 8, by + 16);
    else         move_sprite(SPR_BULLET, 0, 0);

    hearts = (hp + 19) / 20;
    for (i = 0; i < 5; i++) {
        if (i < hearts) move_sprite(SPR_HEART + i, 8 + i * 9, 16);
        else            move_sprite(SPR_HEART + i, 0, 0);
    }
}

/* ---------- loop principal ---------- */
void main(void) {
    UBYTE j, i, c, r;
    INT16 nx, ny;

    DISPLAY_ON;
    text_screen(0);
    new_game();

    while (1) {
        wait_vbl_done();
        j = joypad();
        timer++;
        if (cool) cool--;

        /* --- movimento --- */
        {
            INT8 dx = 0, dy = 0;
            if (j & J_LEFT)  dx = -1;
            if (j & J_RIGHT) dx = 1;
            if (j & J_UP)    dy = -1;
            if (j & J_DOWN)  dy = 1;
            if (dx || dy) { fdx = dx; fdy = dy; }
            nx = (INT16)px + dx;
            if (dx && nx >= 0 && nx <= MAXX && !blocked((UBYTE)nx, py)) px = (UBYTE)nx;
            ny = (INT16)py + dy;
            if (dy && ny >= 0 && ny <= MAXY && !blocked(px, (UBYTE)ny)) py = (UBYTE)ny;
        }

        /* --- atirar (A) --- */
        if ((j & J_A) && !bactive) {
            bactive = 1;
            bx = px; by = py;
            bdx = fdx * 3;
            bdy = fdy * 3;
        }

        /* --- construir (B) --- */
        if ((j & J_B) && cool == 0) {
            INT16 cx = ((px + 4) >> 3) + fdx;
            INT16 cy = ((py + 4) >> 3) + fdy;
            if (cx >= 0 && cx < W && cy >= 0 && cy < H && !wall[cy][cx]) {
                wall[cy][cx] = 1;
                draw_cell((UBYTE)cx, (UBYTE)cy);
                cool = 20;
            }
        }

        /* --- bala --- */
        if (bactive) {
            nx = (INT16)bx + bdx;
            ny = (INT16)by + bdy;
            if (nx < 0 || nx > MAXX || ny < 0 || ny > MAXY) {
                bactive = 0;
            } else {
                bx = (UBYTE)nx; by = (UBYTE)ny;
                c = (bx + 4) >> 3;
                r = (by + 4) >> 3;
                if (wall[r][c]) {            /* bala quebra a parede */
                    wall[r][c] = 0;
                    draw_cell(c, r);
                    bactive = 0;
                } else {
                    for (i = 0; i < NE; i++) {
                        if (ehp[i] && absdiff(bx, ex[i]) < 8 && absdiff(by, ey[i]) < 8) {
                            ehp[i]--;
                            if (ehp[i] == 0) kills++;
                            bactive = 0;
                            break;
                        }
                    }
                }
            }
        }

        /* --- inimigos --- */
        for (i = 0; i < NE; i++) {
            if (!ehp[i]) continue;
            if (!(timer & 1)) {
                if (ex[i] < px && !blocked(ex[i] + 1, ey[i])) ex[i]++;
                else if (ex[i] > px && !blocked(ex[i] - 1, ey[i])) ex[i]--;
                if (ey[i] < py && !blocked(ex[i], ey[i] + 1)) ey[i]++;
                else if (ey[i] > py && !blocked(ex[i], ey[i] - 1)) ey[i]--;
            }
            /* dano de contato */
            if (absdiff(px, ex[i]) < 6 && absdiff(py, ey[i]) < 6 && !(timer & 15)) {
                hp = (hp > 5) ? hp - 5 : 0;
            }
        }

        /* --- tempestade fecha a cada ~7 segundos --- */
        if (timer % 400 == 0) {
            if (zx1 - zx0 > 5) { zx0++; zx1--; }
            if (zy1 - zy0 > 5) { zy0++; zy1--; }
            draw_map();
        }

        /* dano da tempestade no jogador */
        c = (px + 4) >> 3;
        r = (py + 4) >> 3;
        if ((c < zx0 || c > zx1 || r < zy0 || r > zy1) && !(timer & 31)) {
            hp = (hp > 4) ? hp - 4 : 0;
        }

        update_sprites();

        /* --- fim de jogo --- */
        if (kills >= NE) {
            text_screen(1);
            new_game();
        } else if (hp == 0) {
            text_screen(2);
            new_game();
        }
    }
}
