#include "board/board.h"

// The profile with the capabilities found at boot (ADR 0010 section 3, ADR 0015 section 2).
// Built on first use, not as a static initializer: it reads board() of another translation
// unit
namespace {

bool s_coprocessor_answered = false;

BoardConfig& active() {
    static BoardConfig b = board();
    return b;
}

void refresh() {
    BoardConfig& b = active();
    b = board();
    b.has_wifi = b.has_wifi && (!b.wifi_coprocessor.present || s_coprocessor_answered);
}

}  // namespace

const BoardConfig& activeBoard() {
    static bool ready = (refresh(), true);
    (void)ready;
    return active();
}

void setWifiCoprocessorAnswered(bool answered) {
    s_coprocessor_answered = answered;
    refresh();
}
