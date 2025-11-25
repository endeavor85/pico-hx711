#include <iostream>
#include <iomanip>

#include "hardware/pio.h"

#include "pico/stdlib.h"

#include "pio_hx711.h"

using std::cout;
using std::endl;

int main() {
    stdio_init_all();

    sleep_ms(1000);

    cout << endl
         << "*********************" << endl
         << "*** HX711 Example ***" << endl
         << "*********************" << endl << endl;

    HX711_Config hx711_config {
        pio0,     // pio         - PIO identifier
        0u,       // pio_sm      - PIO State Machine
        0u,       // dma_channel - DMA channel
        4u,       // pin_sclk    - GPIO for serial clock
        5u,       // pin_data    - GPIO for data
        -290500,  // offset      - offset value (applied prior to scale)
        -11114.0  // scale       - scale value scalar (applied after offset)
    };

    cout << "SCLK on GPIO" << hx711_config.pin_sclk << endl;
    cout << "DATA on GPIO" << hx711_config.pin_data << endl;

    HX711 hx711(hx711_config);

    cout << "Scaled,Raw(A_128)" << endl;

    while (true) {
        ScaleReading scaleReading = {};
        hx711.read_average(scaleReading, 5);

        cout << std::fixed << std::setw(8) << std::setprecision(2) << scaleReading.scaled_average
             << std::fixed << std::setw(8) << std::setprecision(0) << scaleReading.raw_average
             << endl;

        sleep_ms(100);
    }
}
