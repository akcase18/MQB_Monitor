"""MQB Monitor -- a passive UDS CAN monitor/decoder for the MQB dashboard
project. Reads the SLCAN stream from the Teensy sniffer (env:can_sniffer),
reassembles ISO-TP, and decodes UDS read-data-by-identifier traffic using the
same catalog the firmware uses (core/vehicle/data/signals.json)."""

__version__ = "0.1.0"
