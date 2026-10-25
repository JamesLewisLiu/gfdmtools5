#ifndef CONFIG_ACTION_ORDER_H
#define CONFIG_ACTION_ORDER_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "config/schema.h"

/* Mapper files persist action indices; only the visible order changes. */
static const uint8_t gfdm_gf_display_actions[] = {
    0,  1,  3, 4, 5,  6,  7,  8,  11, 12, 13, 14,
    15, 16, 2, 9, 21, 10, 17, 22, 18, 19, 20,
};

static inline size_t
gfdm_action_for_display_row(const struct schema *schema, size_t row)
{
    if (strcmp(schema->name, "gf") == 0 &&
        row < sizeof(gfdm_gf_display_actions) /
                sizeof(gfdm_gf_display_actions[0])) {
        return gfdm_gf_display_actions[row];
    }
    return row;
}

static inline size_t
gfdm_display_row_for_action(const struct schema *schema, size_t action)
{
    for (size_t row = 0; row < schema->nactions; row++) {
        if (gfdm_action_for_display_row(schema, row) == action) {
            return row;
        }
    }
    return action;
}

#endif
