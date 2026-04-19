// EmuHem debug.hpp shim
// Routes debug output to stderr instead of drawing on LCD.

#ifndef __DEBUG_H__
#define __DEBUG_H__

#include <cstdint>
#include <string>

// ARM-specific context struct -- defined in chcore.h
#include "chcore.h"

// Stack symbols (not meaningful in emulator)
extern uint32_t __process_stack_base__;
extern uint32_t __process_stack_end__;

constexpr uint32_t CRT0_STACKS_FILL_PATTERN = 0x55555555;

void __debug_log(const std::string& msg);
void draw_guru_meditation(uint8_t source, const char* hint);
void draw_guru_meditation(uint8_t source, const char* hint, struct extctx* ctx, uint32_t psp);
bool stack_dump();
bool memory_dump(uint32_t* addr_start, uint32_t num_words, bool stack_flag);

inline size_t get_free_stack_space() { return 4096; }

// Macro from original
#define __LOG1(x, y, ...) y
#define __LOG2(a, ...) __debug_log(a)
#define DEBUG_LOG(...) __LOG1(, ##__VA_ARGS__, __LOG2(__VA_ARGS__))

#endif // __DEBUG_H__
