// Zoom fix: Camera_SetPositionClamped assumes zoom 1.0 always fits on the map. At 3440x1440 the view
// at zoom 1.0 is wider than the map, so the map wraps into misaligned bands and zoom 0.5 can crash
// the game. The camera hooks raise the zoom until the view fits.
#pragma once

namespace rhw::camera {

void Install();

}  // namespace rhw::camera
