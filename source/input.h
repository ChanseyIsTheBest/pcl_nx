/* input.h -- Switch controls -> GameMaker Android input for Pocket Crystal League
 *
 * The game is mouse-driven (left/right click, wheel, hover tooltips) with a few
 * keyboard shortcuts, so:
 *
 *   touchscreen        -> RunnerJNILib.TouchEvent        (fingers)
 *   virtual cursor     -> RunnerJNILib.MouseMoveEvent    (nx_pointer: stick,
 *                         gyro, USB mouse)
 *   mapped buttons     -> RunnerJNILib.MouseButtonEvent / MouseWheelEvent /
 *                         KeyEvent, per config.txt
 *   software keyboard  -> KeyEvent, one key per game step (name entry)
 *
 * Touch id 0 and the mouse share the runner's device-0 state, so a real finger
 * always wins: cursor buttons are released when a finger lands and stay
 * blocked until they are physically let go.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#ifndef __INPUT_H__
#define __INPUT_H__

typedef struct {
  void (*touch)(int action, int id, float x, float y);      // Android ACTION_*
  void (*key)(int action, int keycode, int unicode);         // 0 down, 1 up
  void (*mouse_move)(float x, float y);
  void (*mouse_button)(int button, int down);                // 0 L, 1 R, 2 M
  void (*mouse_wheel)(float delta);                          // + = up
  // Optional: force a GameMaker vk_* key state directly. The runner maps
  // Android Shift/Ctrl/Alt only to vk_lshift/vk_lcontrol/vk_lalt (160-165) and
  // never sets the generic vk_shift/vk_control/vk_alt (16-18) that GML checks.
  void (*vk_state)(int vk, int down);
} InputHooks;

// After nxp_init().
void input_init(const InputHooks *hooks);

// Once per frame, before RunnerJNILib.Process().
void input_update(void);

// Once per frame, OUTSIDE Process(): shows the software keyboard if a button
// asked for it. Blocking (system applet).
void input_service_keyboard(void);

// Release every held key and mouse button (on suspend / exit).
void input_release_all(void);

#endif
