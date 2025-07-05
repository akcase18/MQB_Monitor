# MQB Monitor
A fully configurable data-monitoring device for MQB platform vehicles, capable of displaying up to 5 types of information that refresh at up to 50 times per second.

## State of the Project
I currently have a working PCB (v1). This version has the basics working, but there are a few issues that need to be addressed:
- Touch screen is not working
  - I am not sure whether this is an issue with my PDB design or with my assembly. If it is an issue with the design, I am not sure exactly what could be causing the issue.
- Lack of BOOT and RESET buttons
  - This makes flashing the board impossible/annoying/difficult. Sometimes the board does not flash and I have to take the MCU off, put it into the carrier board, then flash again.
- Overall board layout
  - I don't have any issue with where the components are placed, but they are currently sandwiched between the PCB and the display. This makes access difficult, requiring the devide to be disassembled in order to check things or access other stuff. Also, the USB-C port is located in a place that is difficult to get to, so that should be relocated.
 
I have ordered a new version of the PCB (v2) that should be arriving in 1-2 weeks. I then need to assemble it and test it to see if all of the above issues have been resolved. This will also require a new backing plate design in order to make testing easier and less fragile.

## Next Steps:
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
