// treadcap: receive-only logic capture of the treadmill DB9 harness.
//
// Every channel is a diode probe: Flipper pin (internal pull-up to 3.3 V) ->
// diode anode, diode cathode (stripe) -> DB9 pin. The Flipper never drives a
// pin. Edges are timestamped with the 64 MHz cycle counter in the EXTI
// interrupt; the host decodes UART from the timings.
//
// Channels (DB9 pin -> Flipper header pin):
//   1 -> 2 (PA7)   3 -> 6 (PB2)   4 -> 4 (PA4)
//   6 -> 15 (PC1)  7 -> 16 (PC0)  5 -> 7 (PC3)
// DB9 1 and 5 are polled every millisecond: their EXTI lines are taken by
// the expansion-module detector (line 7) and the OK button (line 3).
//
// Output, one line per event:
//   E<db9><level> <cycles hex>   level change (cycles: 64 MHz, 32-bit, wraps ~67 s)
//   S <cycles hex> <levels> <dropped>   once a second, levels for DB9 1 3 4 5 6 7
// Stop with Ctrl+C.

#include <furi.h>
#include <furi_hal.h>
#include <toolbox/cli/cli_command.h>
#include <toolbox/pipe.h>

typedef struct {
    uint32_t t;
    uint8_t db9;
    uint8_t level;
} Event;

#define RING_SIZE 8192
static Event ring[RING_SIZE];
static volatile uint32_t ring_head;
static volatile uint32_t ring_tail;
static volatile uint32_t ring_dropped;

typedef struct {
    const GpioPin* pin;
    uint8_t db9;
    bool interrupt;
} Probe;

static const Probe probes[] = {
    {&gpio_ext_pa7, 1, false},
    {&gpio_ext_pb2, 3, true},
    {&gpio_ext_pa4, 4, true},
    {&gpio_ext_pc3, 5, false},
    {&gpio_ext_pc1, 6, true},
    {&gpio_ext_pc0, 7, true},
};
#define PROBE_COUNT (sizeof(probes) / sizeof(probes[0]))

static void push(uint8_t db9, uint8_t level) {
    uint32_t t = DWT->CYCCNT;
    FURI_CRITICAL_ENTER();
    uint32_t next = (ring_head + 1) % RING_SIZE;
    if(next == ring_tail) {
        ring_dropped++;
    } else {
        ring[ring_head].t = t;
        ring[ring_head].db9 = db9;
        ring[ring_head].level = level;
        ring_head = next;
    }
    FURI_CRITICAL_EXIT();
}

static void edge_callback(void* context) {
    const Probe* probe = context;
    push(probe->db9, furi_hal_gpio_read(probe->pin));
}

static void treadcap_execute(PipeSide* pipe, FuriString* args, void* context) {
    UNUSED(args);
    UNUSED(context);
    ring_head = ring_tail = ring_dropped = 0;

    for(size_t i = 0; i < PROBE_COUNT; i++) {
        const Probe* probe = &probes[i];
        if(probe->interrupt) {
            // Announce each interrupt before arming it: if its EXTI line is taken,
            // the firmware halts and this is the last line printed.
            printf("# arming DB9 %u\r\n", probe->db9);
            furi_delay_ms(100);
            furi_hal_gpio_init(probe->pin, GpioModeInterruptRiseFall, GpioPullUp, GpioSpeedVeryHigh);
            furi_hal_gpio_add_int_callback(probe->pin, edge_callback, (void*)probe);
        } else {
            furi_hal_gpio_init(probe->pin, GpioModeInput, GpioPullUp, GpioSpeedLow);
        }
    }

    uint8_t polled_level[PROBE_COUNT];
    for(size_t i = 0; i < PROBE_COUNT; i++) {
        polled_level[i] = furi_hal_gpio_read(probes[i].pin);
    }
    printf("# treadcap 1 clock=64000000 channels=1,3,4,5,6,7\r\n");
    uint32_t status_at = 0;

    while(!cli_is_pipe_broken_or_is_etx_next_char(pipe)) {
        for(size_t i = 0; i < PROBE_COUNT; i++) {
            if(probes[i].interrupt) continue;
            uint8_t level = furi_hal_gpio_read(probes[i].pin);
            if(level != polled_level[i]) {
                polled_level[i] = level;
                push(probes[i].db9, level);
            }
        }
        while(ring_tail != ring_head) {
            Event event = ring[ring_tail];
            ring_tail = (ring_tail + 1) % RING_SIZE;
            printf("E%u%u %08lX\r\n", event.db9, event.level, event.t);
        }
        uint32_t now = furi_get_tick();
        if(now - status_at >= 1000) {
            status_at = now;
            printf("S %08lX ", DWT->CYCCNT);
            for(size_t i = 0; i < PROBE_COUNT; i++) {
                printf("%u", furi_hal_gpio_read(probes[i].pin));
            }
            printf(" %lu\r\n", ring_dropped);
        }
        furi_delay_ms(1);
    }

    for(size_t i = 0; i < PROBE_COUNT; i++) {
        if(probes[i].interrupt) furi_hal_gpio_remove_int_callback(probes[i].pin);
        furi_hal_gpio_init(probes[i].pin, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
    }
    printf("# treadcap stopped, dropped=%lu\r\n", ring_dropped);
}

CLI_COMMAND_INTERFACE(treadcap, treadcap_execute, CliCommandFlagDefault, 2048, "cli");
