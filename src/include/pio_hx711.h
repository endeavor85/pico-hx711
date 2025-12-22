#ifndef PICO_PIO_HX711_PIO_HX711_H_
#define PICO_PIO_HX711_PIO_HX711_H_

#include <stdlib.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"

struct HX711_Config {
    PIO pio             = pio0;     // PIO identifier
    uint pio_sm         = 0u;       // PIO State Machine
    uint pin_sclk       = 4u;       // GPIO for serial clock
    uint pin_data       = 5u;       // GPIO for data
    int32_t offset      = 0;        // offset value (applied prior to scale)
    float scale         = 1.0;      // scale value scalar (applied after offset)
};

enum ChannelAndGainSelection {
    A_128 = 1,  // channel A, 128 gain, 1 extra pulse
    B_32  = 2,  // channel B, 32 gain, 2 extra pulses
    A_64  = 3,  // channel A, 64 gain, 3 extra pulses
};

struct ScaleReading {
    uint  num_samples;
    float raw_average;
    float scaled_average;
};

class HX711 {
 public:
    HX711(const HX711_Config &config);
    ~HX711();

    void read_average(ScaleReading &scale_reading, const uint num_readings);

    void set_channel_gain_selection(ChannelAndGainSelection _channel_gain_selection);
    ChannelAndGainSelection get_channel_gain_selection();
    void set_offset(int32_t _offset);
    int32_t get_offset();
    void set_scale(float _scale);
    float get_scale();

 private:
    const PIO pio;
    const uint pio_sm;
    const uint pin_sclk;
    const uint pin_data;

    // samples are between 24 and 26 bits long depending on hx711 channel
    // channel A, 24 bits, 25 pulses
    ChannelAndGainSelection channel_gain_selection = A_128;

    // unscaled value
    int32_t offset = 0;
    float scale = 1.0;

    // buffer must be preallocated with size |num_reads|
    void read_when_ready(uint32_t read_buffer[], const uint num_reads);

    // loads the hx711 program into pio instruction memory (if not already there), returns program offset address
    // the program should only be loaded once per pio, it can be shared among state machines within the same pio
    static uint get_program_offset(PIO pio);

    static uint pio0_offset;  // program offset address in pio0 instruction memory, 0x0000 if not loaded yet
    static uint pio1_offset;  // program offset address in pio1 instruction memory, 0x0000 if not loaded yet
};

#endif  // PICO_PIO_HX711_PIO_HX711_H_
