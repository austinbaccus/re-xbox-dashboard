// boot_anim.h: the original Xbox boot animation, rendered live before the
// dashboard starts. Call once at app startup before the dashboard
// initializes. Blocks until the animation ends or the user skips it.

#pragma once

struct SDL_Window;

// Runs the reverse engineered boot animation (third-party/xboot) through
// bgfx, with its own synthesized sound. Blocks until the animation ends
// (t = 8 s) or a skip key / controller button. Returns true if anything
// was rendered; false if it could not start (caller should just move on).
bool BootAnim_PlayAndWait(SDL_Window* win);
