# MQB Monitor
A fully configurable data-monitoring device for MQB platform vehicles, capable of displaying up to 5 types of information that refresh at up to 50 times per second.

## State of the Project
I currently have a working PCB (v2). This version has the basics working, but there are a few issues that need to be addressed:
- Touch screen is not working
  - I am not sure whether this is an issue with my PDB design or with my assembly. If it is an issue with the design, I am not sure exactly what could be causing the issue.
- BOOT and RESET buttons need to be added
  - There are provisions for both, just need to order the buttons themselves.

## Next Steps:
- [X] Create an enclosure for the display to mount in my car
- [ ] Testing testing testing

## To Do List (In Order):
- [X] Touch screen support on PCB
  - Figured out the issue. The SPI SDI and SCL lines were switched. This is fixed on the schematic and PCB, so will need to order a new version for full confirmation.
- [ ] Brighness adjustment (ideally done automatically, may need to add a sensor to the PCB)
- [ ] Additional data support
  - [ ] Transmission data (temp, clamping pressure, etc)
  - [ ] Haldex data
  - [ ] Etc.
- [ ] EEPROM configuration for storing layout configuration settings
- [ ] Warning light configuration with the ability to set different thresholds
- [ ] Analog and digital information display options
- [ ] Permanent power and sleep mode
- [ ] Micro-SD card support
- [ ] Data logging (save into Micro-SD card)
- [ ] Wireless connectivity to phone for customization and setup
- [ ] Different screen layouts with different amounts of information (1 - 6)
- [ ] Multiple screen layouts with different configurations
- [ ] ISO-TP support for CAN
- [ ] Update strategy
- [ ] Expand to other vehicle platforms
- [ ] Overall appearance customization
