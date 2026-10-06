# Hardware and power

The tested target is Waveshare ESP32-S3-CAM-OV5640 V1.1. Other revisions need their own pin and power validation.

## Serial connection

9600 baud, 8 data bits, no parity, one stop bit. ESP32 UART transmit is GPIO43; receive is GPIO44. Connect UART TX/RX through a MAX3232/SP3232 converter to the corresponding Portfolio RS-232 receive/transmit lines, with a common ground and the original Serial Interface. Preserve the original connector pin numbering and the converter's TTL/RS-232 sides. The chip sockets and board UART plug are not direct RS-232 connectors.

The client uses the Portfolio BIOS COM base address. Serial errors should be diagnosed with `/test` before trying internet requests. USB on the ESP32 is a separate diagnostics/upload interface and is not required for normal use.

## Camera, speaker and microphone

Use the board's camera flat connector, speaker connector and onboard microphones. The firmware uses the board ES7210 microphone ADC and ES8311 output codec. No host-PC audio relay is needed. Seat flat cables fully with the latch closed. Preview latency is limited by the Portfolio serial connection.

## Battery and PWR

Check battery polarity against the actual BAT socket; do not assume cable colour or connector orientation alone is sufficient. Use a battery appropriate for the board. The tested firmware enables the battery latch after startup. Hold the physical PWR button for about two seconds, then release, to request shutdown. With USB supplying power the firmware enters deep sleep; PWR is the wake source. Verify battery operation on your own assembled hardware.

Remove the ESP32 microSD before running this build, because GPIO43/44 are used by the serial link.

## Portfolio card

apfRAM USB is used only while the card is OUTSIDE the Portfolio. Safely finish transfers, disconnect USB, then insert the card. Do not reformat or overwrite a working card image just to update AI.COM.

The 3D enclosure is maintained separately. This repository is the software release and does not claim the enclosure's latest physical fit has been verified.
