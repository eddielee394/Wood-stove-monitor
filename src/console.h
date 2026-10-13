// Serial console for inspecting state and changing settings over USB.
// Type "help" in `pio device monitor`.
#pragma once

namespace console {

void poll();  // call from the UI loop; never blocks

}  // namespace console
