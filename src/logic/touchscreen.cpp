#include "touchscreen.hpp"

static Touchscreen *ts_pointer;
void touch_interrupt();

/****************
 * Constructors *
 ****************/

Touchscreen::Touchscreen(uint8_t cs_pin)
    : _cs_pin(cs_pin)
{
    pinMode(cs_pin, OUTPUT);
    digitalWrite(cs_pin, HIGH);
}

Touchscreen::Touchscreen(uint8_t cs_pin, uint8_t tirq_pin)
    : _cs_pin(cs_pin),
      _tirq_pin(tirq_pin)
{
    pinMode(cs_pin, OUTPUT);
    digitalWrite(cs_pin, HIGH);
    pinMode(tirq_pin, INPUT);
    attachInterrupt(digitalPinToInterrupt(tirq_pin), touch_interrupt, FALLING);
}

/***************************
 * Public Helper Functions *
 ***************************/

void Touchscreen::start(SPIClass &spi)
{
    _spi = &spi;
    _spi->begin();
    if (_tirq_pin != 255) // If touch interrupt is being used
    {
        ts_pointer = this;
    }
}

void Touchscreen::get_data()
{
    /* Don't request data if using touch interrupt and it isn't ready */
    if (!touch_wake)
    {
        return;
    }

    uint32_t curr_tick = millis();
    /* Don't request data too fast */
    if ((curr_tick - _last_req_tick) < MIN_CYCLE_TIME)
    {
        return;
    }

    _spi->beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    digitalWrite(_cs_pin, LOW);

    int16_t z1 = get_single_axis(Z1_CMD);
    int16_t z2 = get_single_axis(Z2_CMD);

    int16_t z = z1 + 4095 - z2;
    int16_t x = 0;
    int16_t y = 0;

    if (z > Z_THRESHOLD)
    {
        get_single_axis(X_CMD); // Initial query to reduce noise
        x = get_single_axis(X_CMD, 3);
        y = get_single_axis(Y_CMD, 3);
    }

    digitalWrite(_cs_pin, HIGH);
    _spi->endTransaction();

    if (z < 0)
    {
        _z_val = 0;
        _x_val = 0;
        _y_val = 0;
        return;
    }
    if (z < Z_THRESHOLD)
    {
        _z_val = 0;
        if (z < Z_THRESHOLD_IRQ)
        {
            if (_tirq_pin != 255)
            {
                touch_wake = false;
            }
        }
        _x_val = 0;
        _y_val = 0;
        return;
    }
    
    _z_val = z;

    switch (_rotation)
    {
    case 0:
        _x_val = 4095 - y;
        _y_val = x;
        break;

    case 1:
        _x_val = x;
        _y_val = y;
        break;

    case 2:
        _x_val = y;
        _y_val = 4095 - x;
        break;

    case 3:
    default:
        _x_val = 4095 - x;
        _y_val = 4095 - y;
        break;
    }

    _last_req_tick = curr_tick;
}

bool Touchscreen::touched()
{
    get_data();
    return (_z_val >= Z_THRESHOLD);
}

int16_t Touchscreen::get_x()
{
    return _x_val;
}

int16_t Touchscreen::get_y()
{
    return _y_val;
}

int16_t Touchscreen::get_z()
{
    return _z_val;
}

void Touchscreen::print_values()
{
    Serial.printf("X: %d\n", _x_val);
    Serial.printf("Y: %d\n", _y_val);
    Serial.printf("Z: %d\n", _z_val);
}

/****************************
 * Private Helper Functions *
 ****************************/

int16_t Touchscreen::get_single_axis(uint16_t cmd, uint8_t num_samples)
{
    int sum = 0;
    for (int i = 0; i < num_samples; i++)
    {
        sum += _spi->transfer16(cmd) >> 3;
    }
    return sum / num_samples;
}

/*************
 * Callbacks *
 *************/

void touch_interrupt(void)
{
    Touchscreen *ts = ts_pointer;
    ts->touch_wake = true;
}