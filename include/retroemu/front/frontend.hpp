#pragma once
// ===========================================================================
//  Frontend — sections V.3 and V.4 of the subject (p.7).
// ===========================================================================
//  V.3: "Your emulator will run at normal speed without adversely affect the
//        operations of the GUI."
//  V.4: the control pad and the four action buttons.
//
//  This is the only part of the project allowed to include SDL. Everything
//  under core/ produces a 160x144 buffer and consumes eight button states,
//  and knows nothing about how either reaches a human.
// ===========================================================================

#include <string>

#include "retroemu/core/types.hpp"

namespace retroemu {

struct FrontendOptions {
    std::string rom_path;          // may be empty: the window opens anyway
    int         scale      = 4;
    bool        force_cgb  = false;
    bool        start_paused = false;

    // Stop after this many frames instead of waiting to be closed. Used to
    // measure the pacing without a human, and to test without a display.
    u64 frame_limit = 0;           // 0 means run until closed
};

int run_frontend(const FrontendOptions &options);

}  // namespace retroemu
