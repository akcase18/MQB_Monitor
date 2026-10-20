#include "simulator_can.hpp"

SimulatorCanBus &SimulatorCanBus::instance()
{
    static SimulatorCanBus s_instance;
    return s_instance;
}
