// EmuHem debug stub implementations

#include "debug.hpp"
#include <cstdio>

uint32_t __process_stack_base__ = 0;
uint32_t __process_stack_end__ = 0;

void __debug_log(const std::string& msg) {
    std::fprintf(stderr, "[EmuHem DEBUG] %s\n", msg.c_str());
}

void draw_guru_meditation(uint8_t source, const char* hint) {
    std::fprintf(stderr, "[EmuHem PANIC] Guru Meditation (source=%u): %s\n",
                 source, hint ? hint : "unknown");
}

void draw_guru_meditation(uint8_t source, const char* hint, struct extctx*, uint32_t) {
    draw_guru_meditation(source, hint);
}

bool stack_dump() { return false; }
bool memory_dump(uint32_t*, uint32_t, bool) { return false; }
