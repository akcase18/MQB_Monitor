#ifndef TOUCHSCREEN_H
#define TOUCHSCREEN_H

#ifdef USE_ARDUINO

    #include <stdint.h>
    #include <SPI.h>

    /*==============================================================================
     Defines
     =============================================================================*/

    #define X_CMD           0x91
    #define Y_CMD           0xD1
    #define Z1_CMD          0xB1
    #define Z2_CMD          0xC1
    #define Z_THRESHOLD     800
    #define Z_THRESHOLD_IRQ 800
    #define MIN_CYCLE_TIME  3

class Touchscreen
{
    private:
        /*==== Private Variables ====*/

        uint8_t   _cs_pin;
        uint8_t   _tirq_pin = 255;
        uint8_t   _rotation = 2;
        int16_t   _x_val;
        int16_t   _y_val;
        int16_t   _z_val;
        uint32_t  _last_req_tick;
        SPIClass *_spi = nullptr;

        /*==== Private Functions ====*/

        int16_t get_single_axis(uint16_t cmd);

        int16_t get_best_avg(int16_t x, int16_t y, int16_t z);

    public:
        /*==== Public Variables ====*/

        volatile bool touch_wake = true;

        /*==== Constructors ====*/

        Touchscreen(uint8_t cs_pin);
        Touchscreen(uint8_t cs_pin, uint8_t tirq_pin);

        /*==== Public Functions ====*/

        void start(SPIClass &spi = SPI1);

        void get_data();

        bool touched();

        int16_t get_x();
        int16_t get_y();
        int16_t get_z();

        void print_values();
};

#endif // USE_ARDUINO
#endif // TOUCHSCREEN_H