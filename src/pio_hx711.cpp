#include "pio_hx711.h"

#include <stdlib.h>
#include <stdio.h>

#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"

#include "pico/stdlib.h"

#include "hx711.pio.h"

// initialize static members
uint HX711::pio0_offset = 0x0000;  // 0x0000 indicates the program has not been loaded into pio program memory
uint HX711::pio1_offset = 0x0000;  // 0x0000 indicates the program has not been loaded into pio program memory

HX711::HX711(const HX711_Config &config) :
        pio(config.pio),
        pio_sm(config.pio_sm),
        pin_sclk(config.pin_sclk),
        pin_data(config.pin_data),
        offset(config.offset),
        scale(config.scale) {
    // configure GPIO pins used by PIO
    pio_sm_set_consecutive_pindirs(pio, pio_sm, pin_data, 1, false);  // set DOUT as input
    pio_sm_set_consecutive_pindirs(pio, pio_sm, pin_sclk, 1, true);  // set PD_SCK as output
    pio_gpio_init(pio, pin_data);  // assign DOUT pin to this PIO
    pio_gpio_init(pio, pin_sclk);  // assign PD_SCK pin to this PIO
    gpio_pull_up(pin_data);  // hold DOUT high, let the slave device ground it to pull it low

    // TODO: creating multiple HX711 instances will cause hx711_program to be loaded multiple times, but we only need it loaded once per pio
    // TODO: create a single class that initializes the pio program memory (once) and provides functions to create HX711 instances
    // add program to pio program memory, get resulting memory offset
    uint pio_offset = HX711::get_program_offset(pio);

    pio_sm_config c = hx711_program_get_default_config(pio_offset);
    sm_config_set_in_pins(&c, pin_data);  // use DOUT pin for pio 'in' (for waits and data reads)
    sm_config_set_set_pins(&c, pin_sclk, 1);  // use PD_SCK pin for pio 'set' (for clock output)

    // during each pulse, pio_sm reads pin_data pin and shifts into ISR
    // setup RX FIFO to shift the ISR value into

    // shift left (MSB first), autopush after receiving 24 bits
    sm_config_set_in_shift(&c, false, true, 24);

    // divide clock to achieve 2MHz PIO execution rate
    // Pico default system clock is 125MHz
    float div = static_cast<float>(clock_get_hz(clk_sys)) / 2000000;
    sm_config_set_clkdiv(&c, div);

    pio_sm_init(pio, pio_sm, pio_offset, &c);
}

HX711::~HX711() {
}

uint HX711::get_program_offset(PIO pio) {
    uint program_offset = 0x0000;

    if (pio0 == pio) {
        if (0x0000 == pio0_offset) {
            // load the program into pio0 instruction memory if it is not already present there, store resulting program offset
            pio0_offset = pio_add_program(pio, &hx711_program);
        }
        // return program offset
        program_offset = pio0_offset;
    } else if (pio1 == pio) {
        if (0x0000 == pio1_offset) {
            // load the program into pio1 instruction memory if it is not already present there, store resulting program offset
            pio1_offset = pio_add_program(pio, &hx711_program);
        }
        // return program offset
        program_offset = pio1_offset;
    }

    return program_offset;
}

void HX711::read_average(ScaleReading &scale_reading, const uint num_readings) {
    // allocate buffer for DMA
    // we'll just use the full 32-bit slot per sample (FIFO has 4 slots)
    const uint kNumReadings = num_readings;  // 24 bits per reading, just use a single 32-bit int for each
    uint32_t read_buffer[kNumReadings];
    for (uint i = 0; i < kNumReadings; i++) {
        // initialize buffer to 0
        read_buffer[i] = 0;
    }

    read_when_ready(read_buffer, kNumReadings);

    int32_t raw_values[kNumReadings];
    int32_t raw_sum = 0;

    for (uint i = 0; i < kNumReadings; i++) {
        // result is 24-bit signed (2's complement) integer (stored as uint32)
        // use 'sign extension' to get signed 32-bit form
        raw_values[i] = read_buffer[i];
        if (raw_values[i] & 0x800000) {   // if sign bit is set (in 24-bit representation)
            raw_values[i] |= 0xFF000000;  // sign extension to 32-bit representation
        }

        raw_sum += raw_values[i];

        // print debug info
        // printf("hx711 read[%d]: %08X\t%d\t%6.02f\n",
        //     i, read_buffer[i], raw_values[i], ((raw_values[i] - offset) / scale));
    }
    
    // divide raw_sum by number of readings to compute average reading
    scale_reading.raw_average = raw_sum / static_cast<float>(num_readings); 
    // apply offset, then scale
    scale_reading.scaled_average = (scale_reading.raw_average - offset) / scale;
}

void HX711::read_when_ready(uint32_t read_buffer[], const uint num_readings) {
    // stop the state machine (it should already be stopped though)
    pio_sm_set_enabled(pio, pio_sm, false);

    // Need to clear _input shift counter_, as well as FIFO, because there may be
    // partial ISR contents left over from a previous run. sm_restart does this.
    pio_sm_clear_fifos(pio, pio_sm);
    pio_sm_restart(pio, pio_sm);

    int dma_channel = dma_claim_unused_channel(true);
    dma_channel_config dma_config = dma_channel_get_default_config(dma_channel);
    channel_config_set_read_increment(&dma_config, false);
    channel_config_set_write_increment(&dma_config, true);
    channel_config_set_dreq(&dma_config, pio_get_dreq(pio, pio_sm, false));
    // use ISR to shift bit into RX FIFO (pin --> ISR --> RX FIFO)
    dma_channel_configure(dma_channel, &dma_config,
        read_buffer,  // destination pointer
        &pio->rxf[pio_sm],  // source: pio sm rx fifo
        num_readings,  // number of readings to take
        true);  // start immediately

    // enable the sm
    pio_sm_set_enabled(pio, pio_sm, true);

    // write to the TX FIFO (informing sm how many readings to take)
    // use num_readings-1 since the first time through the loop is free ;-)
    pio_sm_put_blocking(pio, pio_sm, num_readings - 1);

    // TODO: this doesn't work because the PIO needs it for every reading
    //       there are not enough scratch registers available for this method
    // TODO: another method is to modify the PIO sm to just take a single reading, then the outer loop
    //       scratch register can be used for the channel selection.  the caller will just need to
    //       read one reading at a time from the DMA
    // // write to the TX FIFO (informing sm the channel and gain to apply for the next reading)
    // // use channel_gain_selection-1 since the first time through the loop is free ;-)
    // pio_sm_put_blocking(pio, pio_sm, channel_gain_selection - 1);

    // wait for reading to come into DMA buffer (taken from PIO RX FIFO)
    dma_channel_wait_for_finish_blocking(dma_channel);
    dma_channel_unclaim(dma_channel);
}

void HX711::set_channel_gain_selection(ChannelAndGainSelection _channel_gain_selection) {
    channel_gain_selection = _channel_gain_selection;
}

ChannelAndGainSelection HX711::get_channel_gain_selection() {
    return channel_gain_selection;
}

void HX711::set_offset(int32_t _offset) {
    offset = _offset;
}

int32_t HX711::get_offset() {
    return offset;
}

void HX711::set_scale(float _scale) {
    scale = _scale;
}

float HX711::get_scale() {
    return scale;
}
