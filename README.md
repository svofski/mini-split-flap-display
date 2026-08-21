# Mini Split-Flap Display

## Design goals

 * as compact as reasonably possible
 * full English alphabet and numbers, space and one extra character (38 flaps)
 * as cheap as reasonably possible
 * 3D printable with minimal extra parts

## Approximate BOM

 * black and white PLA, mandatory for optical sensor contrast
 * QRE1113 sensor module (1 per module, ￥1.89/pc) ![qre1113](doc/qre1113.jpg)
 * V1113 CH32V003F4P6 (1 per module, ￥2.86/pc) ![v1772](doc/V1772.jpg)
 * MG90S 360 degree servo (1 per module, ￥6.48/pc), SG90 360 degree can be used as well

## Design

The motor is hidden inside the stator drum. Flappy drum sits on the stator driven by a little cog.
![split-flap](doc/sf1.jpg)

Because there are so many flaps in such a small place, precision positioning is important. The feedback is optical.
One of the sides of the flaps drum has embedded codewheel. Zero position is marked by a wider tick.
![codewheel](doc/codewheel.jpg)

Tracking the optical marks is not so trivial as I expected. Here's a plot:
![sensor](doc/sensor-plot.jpg)
The algorithm tracks min and max on raw data(green) independently (blue and red lines), then detects transitions over median line with added hysteresis.

## Communications

Each wheel has independent controller (CH32V003). All together they are connected via I2C bus.

The master controller is Arduino Nano. In addition to requesting a letter display, the protocol supports setting I2C address and homing offset for every module.
