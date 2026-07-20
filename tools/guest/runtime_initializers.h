#pragma once

// Completes the Darwin crt1 contract after Mach and cthread setup by asking
// the target dyld to invoke delayed module initializers for loaded dylibs.
void ilegacysim_guest_run_runtime_initializers(void);
