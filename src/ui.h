#pragma once

#include "rsvp_settings.h"

void ui_run(void);
int ui_choose(const char *title, const char *subtitle,
              const char *const *items, int count, int *selected);
int ui_edit_settings(rsvp_settings_t *settings);
void ui_show_message(const char *title, const char *line1,
                     const char *line2, const char *footer);

/* Vendor-full white cycle. Mixed mode is what ghosts; this
 * rewrites every pixel, then the caller redraws. */
void ui_clear_ghosting(void);

/* Two vendor fulls: a white pass, then frame. Used for the Finished
 * screen so ghosting from fast reading is cleared before Home. */
void ui_present_full(const uint8_t *frame);
