#define CHIPS_IMPL
#include "z80pio.h"
#include "z80ctc.h"
#include "z80sio.h"
#include "z80dma.h"
#include "mm58167.h"

int main()
{
    z80pio_t pio;
    z80ctc_t ctc;
    mm58167a_t rtc;

    z80pio_init(&pio);
    z80ctc_init(&ctc);
    mm58167a_init(&rtc);

    // Simulate one tick...
    uint64_t pins = 0;
    pins = mm58167a_tick(&rtc, pins);

    return 0;
}
