# MQB Monitor
A fully configurable data-monitoring device for MQB platform vehicles, capable of displaying up to 5 types of information that refresh at up to 50 times per second.

## State of the Project
At this point, I have a working prototype that is crudely set up but functions well. This is running on a Teensy MM in an ATP Carrier Board, with jumper wires connecting it to a 3.5" display. This display is touch-capable, but that has not been set up yet, and also has a MicroSD card slot that will be utilized for logging. 

## Next Steps:
- [ ] Figure out wiring for all peripherals I'd like to use (touchscreen, MicroSD card slot, etc)
- [ ] Create PCB to replace the ATP Carrier Board so I can more directly interface with the display. Use pin headers to connect directly to the display instead of with jumper wires. This PCB should also have power management on it, so I can pull power directly from the OBD-II port (12v -> 5/3.3v)
- [ ] Create an enclosure for the display to mount in my car
- [ ] Testing testing testing

## To Do List:
- [ ] Touch screen support
- [ ] MicroSD card support
- [ ] EEPROM configuration for storing layout configuration settings
- [ ] Warning light configuration with the ability to set different thresholds
- [ ] Analog and digital information display options
- [ ] ISO-TP support for CAN
- [ ] Combinational data (ex: absolute intake pressure - ambient pressure = net intake pressure)
- [ ] Data logging (save into MicroSD card)
- [ ] Different screen layouts with different amounts of information (1 - 6)
- [ ] Multiple screen layouts with different configurations
- [ ] Update strategy
- [ ] Expand to other vehicle platforms
- [ ] Overall appearance customization
