#include "pio_hx711.h"

#include <stdlib.h>
#include <stdio.h>

#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"

#include "pico/stdlib.h"

#include "hx711.pio.h"

HX711::HX711(const HX711_Config &config) :
        pio(config.pio),
        pio_sm(config.pio_sm),
        dma_channel(config.dma_channel),
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

    // add program to pio program memory, get resulting memory offset
    uint pio_offset = pio_add_program(pio, &hx711_program);

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

float HX711::read_average(const uint num_readings) {
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
    int32_t offset_value_sum = 0;

    for (uint i = 0; i < kNumReadings; i++) {
        // result is 24-bit signed (2's complement) integer (stored as uint32)
        // use 'sign extension' to get signed 32-bit form
        raw_values[i] = read_buffer[i];
        if (raw_values[i] & 0x800000) {   // if sign bit is set (in 24-bit representation)
            raw_values[i] |= 0xFF000000;  // sign extension to 32-bit representation
        }

        offset_value_sum += (raw_values[i] - offset);

        // print debug info
        // printf("hx711 read[%d]: %08X\t%d\t%6.02f\n",
        //     i, read_buffer[i], raw_values[i], ((raw_values[i] - offset) / scale));
    }

    // average values, apply offset, then scale
    return (offset_value_sum / static_cast<float>(num_readings)) / scale;
}

void HX711::read_when_ready(uint32_t read_buffer[], const uint num_readings) {
// void HX711::read_when_ready(char read_buffer[], const uint num_readings) {
    // stop the state machine (it should already be stopped though)
    pio_sm_set_enabled(pio, pio_sm, false);

    // Need to clear _input shift counter_, as well as FIFO, because there may be
    // partial ISR contents left over from a previous run. sm_restart does this.
    pio_sm_clear_fifos(pio, pio_sm);
    pio_sm_restart(pio, pio_sm);

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
