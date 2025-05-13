
# Violations of reference circuits
- Power switch U16 should have at least 1 uF input capacitance. It is not placed in v1.0.0

# Bugs
- The MSP430 programming connector PJ1 has a swapped (symmetrically mirrored) pin mapping compared to the WULPUS v1.2.2 design.
 That make the corresponding adapter PCBs incompatible with WULPUS pro design (requires the FPC cable that also swaps the pins symmetrically).