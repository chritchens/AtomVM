/***************************************************************************
 *   Copyright 2017 by Davide Bettio <davide@uninstall.it>                 *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Lesser General Public License as        *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA .        *
 ***************************************************************************/

#include "gpio_driver.h"

#include <string.h>

#include <driver/gpio.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "atom.h"
#include "bif.h"
#include "context.h"
#include "debug.h"
#include "defaultatoms.h"
#include "globalcontext.h"
#include "mailbox.h"
#include "module.h"
#include "utils.h"
#include "term.h"

#include "trace.h"

#include "sys.h"
#include "esp32_sys.h"

static Context *gpio_ctx;

static void consume_gpio_mailbox(Context *ctx);
static void IRAM_ATTR gpio_isr_handler(void *arg);

static const char *const set_level_a = "\x9" "set_level";
static const char *const input_a = "\x5" "input";
static const char *const output_a = "\x6" "output";
static const char *const set_direction_a ="\xD" "set_direction";
static const char *const set_int_a = "\x7" "set_int";
static const char *const gpio_interrupt_a = "\xE" "gpio_interrupt";


void gpiodriver_init(Context *ctx)
{
    ctx->native_handler = consume_gpio_mailbox;
    ctx->platform_data = NULL;
}

void gpio_interrupt_callback(EventListener *listener)
{
    Context *listening_ctx = listener->data;
    int gpio_num = listener->fd;

    // 1 header + 2 elements
    if (UNLIKELY(memory_ensure_free(gpio_ctx, 3) != MEMORY_GC_OK)) {
        //TODO: it must not fail
        abort();
    }

    term int_msg = term_alloc_tuple(2, gpio_ctx);
    term_put_tuple_element(int_msg, 0, context_make_atom(gpio_ctx, gpio_interrupt_a));
    term_put_tuple_element(int_msg, 1, term_from_int32(gpio_num));

    mailbox_send(listening_ctx, int_msg);
}

static void consume_gpio_mailbox(Context *ctx)
{
    term ret;

    Message *message = mailbox_dequeue(ctx);
    term msg = message->message;
    term pid = term_get_tuple_element(msg, 0);
    term cmd = term_get_tuple_element(msg, 1);

    int local_process_id = term_to_local_process_id(pid);
    Context *target = globalcontext_get_process(ctx->global, local_process_id);

    if (cmd == context_make_atom(ctx, set_level_a)) {
        int32_t gpio_num = term_to_int32(term_get_tuple_element(msg, 2));
        int32_t level = term_to_int32(term_get_tuple_element(msg, 3));
        gpio_set_level(gpio_num, level != 0);
        TRACE("gpio: set_level: %i %i\n", gpio_num, level != 0);
        ret = OK_ATOM;

    } else if (cmd == context_make_atom(ctx, set_direction_a)) {
        int32_t gpio_num = term_to_int32(term_get_tuple_element(msg, 2));
        term direction = term_get_tuple_element(msg, 3);

        if (direction == context_make_atom(ctx, input_a)) {
            gpio_set_direction(gpio_num, GPIO_MODE_INPUT);
            TRACE("gpio: set_direction: %i INPUT\n", gpio_num);
            ret = OK_ATOM;

        } else if (direction == context_make_atom(ctx, output_a)) {
            gpio_set_direction(gpio_num, GPIO_MODE_OUTPUT);
            TRACE("gpio: set_direction: %i OUTPUT\n", gpio_num);
            ret = OK_ATOM;

        } else {
            TRACE("gpio: unrecognized direction\n");
            ret = ERROR_ATOM;
        }

    } else if (cmd == context_make_atom(ctx, set_int_a)) {
        int32_t gpio_num = term_to_int32(term_get_tuple_element(msg, 2));
        TRACE("going to install interrupt for %i.\n", gpio_num);

        //TODO: ugly workaround here, write a real implementation
        gpio_ctx = ctx;
        gpio_install_isr_service(0);
        TRACE("installed ISR service 0.\n");

        gpio_set_direction(gpio_num, GPIO_MODE_INPUT);
        //TODO: both posedge and negedge must be supproted
        gpio_set_intr_type(gpio_num, GPIO_PIN_INTR_POSEDGE);

        gpio_isr_handler_add(gpio_num, gpio_isr_handler, (void *) gpio_num);


        GlobalContext *global = ctx->global;

        EventListener *listener = malloc(sizeof(EventListener));
        if (IS_NULL_PTR(listener)) {
            fprintf(stderr, "Failed to allocate memory: %s:%i.\n", __FILE__, __LINE__);
            abort();
        }
        linkedlist_append(&global->listeners, &listener->listeners_list_head);
        listener->fd = gpio_num;

        listener->expires = 0;
        listener->expiral_timestamp.tv_sec = INT_MAX;
        listener->expiral_timestamp.tv_nsec = INT_MAX;
        listener->one_shot = 0;
        listener->data = target;
        listener->handler = gpio_interrupt_callback;

        ret = OK_ATOM;

    } else {
        TRACE("gpio: unrecognized command\n");
        ret = ERROR_ATOM;
    }

    free(message);

    mailbox_send(target, ret);
}

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t) arg;
    xQueueSendFromISR(event_queue, &gpio_num, NULL);
}
