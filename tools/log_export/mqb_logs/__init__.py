"""MQB Log Export -- host-side offload of the dashboard firmware's black-box
signal log (see core/logging/README.md).

Talks the bespoke LOGSTATUS/LOGDUMP/LOGCLEAR line command set that
env:teensymm's own USB-CDC handler implements (src/main.cpp's
handle_log_command()) -- NOT the SLCAN protocol tools/monitor speaks to the
standalone env:can_sniffer board. Separate board, separate firmware,
separate protocol; the two tools share only this repo's signal registry and
tools/monitor's Qt theme.

  * logfile.py -- pure decode of the on-disk chunk/index formats. No serial,
                  no Qt: the CLI (dump_log.py), the GUI and the tests all
                  share this one implementation.
  * device.py  -- the serial command set, plus a background dump worker.
  * gui.py     -- the PySide6 app (run.py launches it).
"""

__version__ = "0.1.0"
