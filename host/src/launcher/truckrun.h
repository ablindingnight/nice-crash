// The Home "truck run" (design 3.2, user idea 2026-11-15, mockup launcher_home v11): a truck silhouette drives the
// foothill, builds up speed, launches off the knoll into a backflip over the pines, flies off, and after a gap flies
// back in from the left. All in hills coordinates (1280x260 below canvas y 460).
#pragma once

namespace truckrun {

void Step(float dt);                        // advance the simulation (call at 30 fps steps)
void Draw(float L, float sx);               // inside Backdrop's mid layer
void Park();                                // animation off: stand still on the ridge at x ~300
// The canvas area the truck and its dust cover now (for repainting only what moves); false if nothing is shown.
bool Bounds(float L, float sx, float* x0, float* y0, float* x1, float* y1);
float Speed();                              // px/s while driving, for debugging
const char* Mode();

}  // namespace truckrun
