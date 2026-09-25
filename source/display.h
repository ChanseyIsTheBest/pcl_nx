/* display.h -- offscreen game target and scaled presentation
 *
 * The runner is handed an offscreen framebuffer as "the screen" (it asks Java
 * for it through RunnerJNILib -> GetDefaultFrameBuffer, and imports.c also
 * redirects explicit binds of FBO 0 to it). At the end of each frame the target
 * is drawn onto the real EGL surface, scaled according to config.scale:
 *
 *   fit      target == panel size, presented 1:1
 *   sharp    target == smallest integer multiple of 512x288 covering the
 *            panel, filtered down (even pixel sizes, no shimmer)
 *   integer  target == largest integer multiple fitting the panel, centred
 *
 * Owning the target also gives the cursor overlay a place to draw in the same
 * coordinate space the runner receives mouse events in.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#ifndef __DISPLAY_H__
#define __DISPLAY_H__

#include <GLES2/gl2.h>

// Pick render_width/render_height for the current panel size and scale mode.
void display_choose_size(void);

// Create the offscreen target and the present shader. Requires a current GL
// context. Returns 0 on failure; the runner then draws straight to the panel.
int display_init(void);
void display_shutdown(void);

// The FBO the runner should treat as "the screen"; 0 in the fallback path.
GLuint display_game_fbo(void);
int display_is_offscreen(void);

// Draw the game target onto the panel. Call after Process(), before swap.
void display_present(void);

// Panel pixels (top-left origin, EGL surface space) <-> game window pixels.
void display_screen_to_game(float sx, float sy, float *gx, float *gy);
void display_game_to_screen(float gx, float gy, float *sx, float *sy);

#endif
