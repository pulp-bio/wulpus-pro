
# Violations of reference circuits
- Power switch U16 should have at least 1 uF input capacitance. It is not placed in v1.0.0

# Bugs
- The MSP430 programming connector PJ1 has a swapped (symmetrically mirrored) pin mapping compared to the WULPUS v1.2.2 design.
 That make the corresponding adapter PCBs incompatible with WULPUS pro design (requires the FPC cable that also swaps the pins symmetrically).

- Since HV Multiplexer HV 2707 has bleed resistors of approximately 35 kOhm, the TX/RX line is pulled up to zero voltage with equivalent 1.1 kOhm resistor (16 channels TX/RX, 2 bleed resistors per channel). Then, the line is pulled low and 100 kOhm resistor pull up to high voltage is not enough. Solution could be to remove R23 and solder RGND to +HV with a wire. In this case the biasing is not the most stable but it works. Solutions for the future: low pass filter for + HV bias (the same as for indirect bias) routed directly to RGND.

- Transducer connector J1 and J2 uses the smallest available connector DF40C-40DS-0.4V(51) resulting in 1.5 mm stacking height. The height is the sam as HV2707 heigh which is located in betwenn the connectors on the PCB. The default mounted connectors should be higher to allow for a margin.


# Changes to the reference design
- WULPUS PRO 1 board has 220pf instead of 1 nF mounted on footprint of C12 to match 5 MHz Vermon transducer.

# Improvements

- Preferably OFFSN pin of the AD8338 should be tied to capacitor to provide additional high pass filter to mitigate MSP430 receive artifacts.