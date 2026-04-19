/*
 * Copyright (C) 2014 Jared Boone, ShareBrained Technology, Inc.
 *
 * This file is part of PortaPack.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#include "ch.h"
#include "debug.hpp"
#include "event_m4.hpp"
#include "lpc43xx_cpp.hpp"
#include "message_queue.hpp"
#include "portapack_shared_memory.hpp"

#include <cstdint>
#include <array>

using namespace lpc43xx;

extern "C" {

CH_IRQ_HANDLER(MAPP_IRQHandler) {
    CH_IRQ_PROLOGUE();

    chSysLockFromIsr();
    BasebandEventDispatcher::events_flag_isr(EVT_MASK_BASEBAND);
    chSysUnlockFromIsr();

    creg::m0apptxevent::clear();

    CH_IRQ_EPILOGUE();
}
}

Thread* BasebandEventDispatcher::thread_event_loop = nullptr;

BasebandEventDispatcher::BasebandEventDispatcher(
    std::unique_ptr<BasebandProcessor> baseband_processor)
    : baseband_processor{std::move(baseband_processor)} {
}

void BasebandEventDispatcher::run() {
    thread_event_loop = chThdSelf();
    extern Thread* g_m4_event_thread;
    g_m4_event_thread = thread_event_loop;

    lpc43xx::creg::m0apptxevent::enable();

    // Indicate to the M0 thread that
    // M4 is ready to receive message events.
    shared_memory.set_baseband_ready();

    while (is_running) {
        const auto events = wait();
        dispatch(events);
    }

    lpc43xx::creg::m0apptxevent::disable();
}

void BasebandEventDispatcher::request_stop() {
    is_running = false;
}

eventmask_t BasebandEventDispatcher::wait() {
    return chEvtWaitAny(ALL_EVENTS);
}

void BasebandEventDispatcher::dispatch(const eventmask_t events) {
    if (events & EVT_MASK_BASEBAND) {
        handle_baseband_queue();
    }

    if (events & EVT_MASK_SPECTRUM) {
        handle_spectrum();
    }
}

void BasebandEventDispatcher::handle_baseband_queue() {
    const auto message = shared_memory.baseband_message;
    if (message) {
        on_message(message);
    }
}

void BasebandEventDispatcher::on_message(const Message* const message) {
    switch (message->id) {
        case Message::ID::Shutdown:
            on_message_shutdown(*reinterpret_cast<const ShutdownMessage*>(message));
            shared_memory.baseband_message = nullptr;
#ifdef PRALINE
            shared_memory.baseband_message = nullptr;  // Must clear before M4 exits!
#endif
            break;

        default:
            on_message_default(message);
            shared_memory.baseband_message = nullptr;
            break;
    }
}

void BasebandEventDispatcher::on_message_shutdown(const ShutdownMessage&) {
    request_stop();
}

void BasebandEventDispatcher::on_message_default(const Message* const message) {
    baseband_processor->on_message(message);
}

void BasebandEventDispatcher::handle_spectrum() {
    const UpdateSpectrumMessage message;
    baseband_processor->on_message(&message);
}
