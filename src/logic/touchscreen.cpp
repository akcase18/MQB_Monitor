#include "touchscreen.hpp"

static Touchscreen *ts_pointer;
void                touch_interrupt();

/*==============================================================================
 Constructors
 =============================================================================*/

Touchscreen::Touchscreen(uint8_t cs_pin)
    : _cs_pin(cs_pin)
{
    pinMode(cs_pin, OUTPUT);
    digitalWrite(cs_pin, HIGH);
}

Touchscreen::Touchscreen(uint8_t cs_pin, uint8_t tirq_pin)
    : _cs_pin(cs_pin)
    , _tirq_pin(tirq_pin)
{
    pinMode(cs_pin, OUTPUT);
    digitalWrite(cs_pin, HIGH);
    pinMode(tirq_pin, INPUT);
    attachInterrupt(digitalPinToInterrupt(tirq_pin), touch_interrupt, FALLING);
}

/*==============================================================================
 Public Helper Functions
 =============================================================================*/

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
    z1         = get_single_axis(Z1_CMD);
    int16_t z2 = get_single_axis(Z2_CMD);
    z2         = get_single_axis(Z2_CMD);

    int16_t z  = z1 + 4095 - z2;
    int16_t x1 = 0;
    int16_t x2 = 0;
    int16_t x3 = 0;
    int16_t y1 = 0;
    int16_t y2 = 0;
    int16_t y3 = 0;

    if (z > Z_THRESHOLD)
    {
        get_single_axis(X_CMD); // Initial query to reduce noise
        x1 = get_single_axis(X_CMD);
        y1 = get_single_axis(Y_CMD);
        x2 = get_single_axis(X_CMD);
        y2 = get_single_axis(Y_CMD);
        x3 = get_single_axis(X_CMD);
        y3 = get_single_axis(Y_CMD);
    }

    digitalWrite(_cs_pin, HIGH);
    _spi->endTransaction();

    int16_t x = get_best_avg(x1, x2, x3);
    int16_t y = get_best_avg(y1, y2, y3);

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

/*==============================================================================
 Private Helper Functions
 =============================================================================*/

int16_t Touchscreen::get_single_axis(uint16_t cmd)
{
    return _spi->transfer16(cmd) >> 3;
}

int16_t Touchscreen::get_best_avg(int16_t x, int16_t y, int16_t z)
{
    int16_t diff_a;
    int16_t diff_b;
    int16_t diff_c;
    int16_t result;
    if (x > y)
    {
        diff_a = x - y;
    }
    else
    {
        diff_a = y - x;
    }
    if (x > z)
    {
        diff_b = x - z;
    }
    else
    {
        diff_b = z - x;
    }
    if (y > z)
    {
        diff_c = y - z;
    }
    else
    {
        diff_c = z - y;
    }

    if (diff_a <= diff_b && diff_a <= diff_c)
    {
        result = (x + y) >> 1;
    }
    else if (diff_b <= diff_a && diff_b <= diff_c)
    {
        result = (x + z) >> 1;
    }
    else
    {
        result = (y + z) >> 1;
    }

    return result;
}

/*==============================================================================
 Callbacks
 =============================================================================*/

void touch_interrupt(void)
{
    Touchscreen *ts = ts_pointer;
    ts->touch_wake  = true;
}