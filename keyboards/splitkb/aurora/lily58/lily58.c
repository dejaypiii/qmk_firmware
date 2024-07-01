/* Copyright 2022 splitkb.com <support@splitkb.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "quantum.h"
#include "transactions.h"

// The first four layers gets a name for readability, which is then used in the OLED below.
enum layers { _DEFAULT, _LOWER, _RAISE, _ADJUST };

#ifdef OLED_ENABLE

// NOTE: Most of the OLED code was originally written by Soundmonster for the Corne,
// and has been copied directly from `crkbd/soundmonster/keymap.c`

#    include <memory.h>
#    include <stdbool.h>
#    include <stddef.h>
#    include <stdlib.h>
#    include <time.h>

#    define GOLWIDTH 64
#    define GOLHEIGHT 128
#    define LIFE_INIT_RATE (1.0 - 0.5)
#    define GolCellDead 0
#    define GolCellAlive 1

bool gol_field[GOLWIDTH][GOLHEIGHT]      = {0};
bool gol_next_field[GOLWIDTH][GOLHEIGHT] = {0};

#    define GOL_FIELD_SIZE (sizeof(bool) * GOLWIDTH * GOLHEIGHT)

void gol_init(void) {
    memset(&gol_field, 0, GOL_FIELD_SIZE);

    time_t t;
    if (is_keyboard_master())
        srand((unsigned)time(&t));
    else
        srand((unsigned)time(&t) + 42);

    for (size_t x = GOLWIDTH * 0.2; x <= GOLWIDTH * 0.8; ++x) {
        for (size_t y = GOLHEIGHT * 0.2; y <= GOLHEIGHT * 0.8; ++y) {
            if (((double)rand() / RAND_MAX) > LIFE_INIT_RATE) {
                gol_field[x][y] = true;
            }
        }
    }
}

int eval_next_cell_state(size_t x, size_t y) {
    int neighbors_alive = 0;

    if (gol_field[(x - 1) % GOLWIDTH][(y - 1) % GOLHEIGHT] == 1) neighbors_alive += 1;
    if (gol_field[x][(y - 1) % GOLHEIGHT] == 1) neighbors_alive += 1;
    if (gol_field[(x + 1) % GOLWIDTH][(y - 1) % GOLHEIGHT] == 1) neighbors_alive += 1;
    if (gol_field[(x - 1) % GOLWIDTH][y] == 1) neighbors_alive += 1;
    if (gol_field[(x + 1) % GOLWIDTH][y] == 1) neighbors_alive += 1;
    if (gol_field[(x - 1) % GOLWIDTH][(y + 1) % GOLHEIGHT] == 1) neighbors_alive += 1;
    if (gol_field[x][(y + 1) % GOLHEIGHT] == 1) neighbors_alive += 1;
    if (gol_field[(x + 1) % GOLWIDTH][(y + 1) % GOLHEIGHT] == 1) neighbors_alive += 1;

    /* Rules
        Any live cell with fewer than two live neighbours dies, as if by
      underpopulation. Any live cell with two or three live neighbours lives on to
      the next generation. Any live cell with more than three live neighbours
      dies, as if by overpopulation. Any dead cell with exactly three live
      neighbours becomes a live cell, as if by reproduction.
    */
    if (gol_field[x][y] == 1) {
        if (neighbors_alive < 2) return 0;
        if (neighbors_alive > 3) return 0;
        return 1;
    } else {
        if (neighbors_alive == 3)
            return 1;
        else
            return 0;
    }
}

void gol_tick(void) {
    for (size_t x = 0; x < GOLWIDTH; ++x) {
        for (size_t y = 0; y < GOLHEIGHT; ++y) {
            int next_state       = eval_next_cell_state(x, y);
            gol_next_field[x][y] = next_state;
        }
    }

    memcpy(&gol_field, &gol_next_field, GOL_FIELD_SIZE);
}

bool get_gol_cell(size_t x, size_t y) {
    return gol_field[x][y];
}

oled_rotation_t oled_init_kb(oled_rotation_t rotation) {
    return OLED_ROTATION_270;
}

void keyboard_pre_init_user(void) {
    // Set our LED pin as output
    setPinOutput(24);
    // Turn the LED off
    // (Due to technical reasons, high is off and low is on)
    writePinHigh(24);

    gol_init();
}

void reset_slave_gol(uint8_t in_buflen, const void* in_data, uint8_t out_buflen, void* out_data) {
    gol_init();
}

void keyboard_post_init_user(void) {
    transaction_register_rpc(RESET_SLAVE_GOL, reset_slave_gol);
}

bool oled_task_kb(void) {
    static uint32_t anim_timer = 0;
    if (timer_elapsed32(anim_timer) > 80) {
        anim_timer = timer_read32();
        gol_tick();
    }

    oled_clear();
    for (int x = 0; x < GOLWIDTH; x++)
        for (int y = 0; y < GOLHEIGHT; y++)
            if (get_gol_cell(x, y) == 1) oled_write_pixel(x, y, true);

    return false;
}

static bool should_reset_slave_gol = false;
bool        process_record_user(uint16_t keycode, keyrecord_t* record) {
    if (record->event.pressed && keycode == KC_DEL) {
        should_reset_slave_gol = true;
        gol_init();
    }

    return true;
}

void housekeeping_task_user(void) {
    if (is_keyboard_master()) {
        if (should_reset_slave_gol) {
            should_reset_slave_gol = false;
            if (transaction_rpc_send(RESET_SLAVE_GOL, 0, NULL)) {
                dprintf("Slave value: %d\n", s2m.s2m_data); // this will now be 11, as the slave adds 5
            } else {
                dprint("Slave sync failed!\n");
            }
        }
    }
}

#endif

#ifdef ENCODER_ENABLE
bool encoder_update_kb(uint8_t index, bool clockwise) {
    if (!encoder_update_user(index, clockwise)) {
        return false;
    }
    // 0 is left-half encoder,
    // 1 is right-half encoder
    if (index == 0) {
        // Volume control
        if (clockwise) {
            tap_code(KC_VOLU);
        } else {
            tap_code(KC_VOLD);
        }
    } else if (index == 1) {
        // Page up/Page down
        if (clockwise) {
            tap_code(KC_PGDN);
        } else {
            tap_code(KC_PGUP);
        }
    }
    return true;
}
#endif
