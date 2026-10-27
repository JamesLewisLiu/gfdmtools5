#ifndef GFDMHOOK1_DISPLAY_MODE_H
#define GFDMHOOK1_DISPLAY_MODE_H

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

enum gfdm_display_mode {
    GFDM_DISPLAY_WINDOWED = 0,
    GFDM_DISPLAY_FULLSCREEN_FIT = 1,
    GFDM_DISPLAY_FULLSCREEN_STRETCH = 2,
    GFDM_DISPLAY_BORDERLESS_FIT = 3,
    GFDM_DISPLAY_BORDERLESS_STRETCH = 4,
};

/* Keep the old boolean setting readable in both the hook and configuration UI.
 */
static inline bool gfdm_display_parse_mode(const char *value, int *mode)
{
    char word[8];
    size_t n = 0;
    if (value == NULL || mode == NULL) {
        return false;
    }
    while (isspace((unsigned char) *value)) {
        value++;
    }
    while (*value != '\0' && !isspace((unsigned char) *value)) {
        if (n + 1 >= sizeof(word)) {
            return false;
        }
        word[n++] = (char) tolower((unsigned char) *value++);
    }
    word[n] = '\0';
    while (isspace((unsigned char) *value)) {
        value++;
    }
    if (*value != '\0') {
        return false;
    }
    if (strcmp(word, "true") == 0) {
        *mode = GFDM_DISPLAY_FULLSCREEN_FIT;
    } else if (strcmp(word, "false") == 0) {
        *mode = GFDM_DISPLAY_WINDOWED;
    } else if (n == 1 && word[0] >= '0' && word[0] <= '4') {
        *mode = word[0] - '0';
    } else {
        return false;
    }
    return true;
}

#endif
