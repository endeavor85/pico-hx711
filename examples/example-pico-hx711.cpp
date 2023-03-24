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

    HX711_Config hx711_config;
    hx711_config.pio = pio0;        // PIO identifier
    hx711_config.pin_sclk = 4u;     // GPIO for serial clock
    hx711_config.pin_data = 5u;     // GPIO for data
    hx711_config.offset = -290500;
    hx711_config.scale = -11114.0;

    cout << "SCLK on GPIO" << hx711_config.pin_sclk << endl;
    cout << "DATA on GPIO" << hx711_config.pin_data << endl;

    HX711 hx711(hx711_config);

    while (true) {
        float avg_reading = hx711.read_average(5);

        cout << "Weight (A_128): "
             << std::fixed << std::setw(8) << std::setprecision(2)
             << avg_reading << endl;

        sleep_ms(500);
    }
}
