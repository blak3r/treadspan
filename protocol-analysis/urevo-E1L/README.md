UREVO is using FTMS.

Observations:
- Power output does only go up when you're walking on the device (could be used to improve step accuracy if user leaves it running while they go to bathroom)
- Sessions are cummulative... it's a true "pause" instead of a stop like Lifespan is (and i thought Sperax was)..
- SPEED  (Device / FTMS received by ble)
  - 2.0 / 0.32 kph 0x0141 = 321 
  - 1.8 / 0x0121 = 289
  - 1.6 / 0101 = 257



0.006225680934 is the conversion factor... 



SPERAX:
- Similar issue, 
1.0 / 0064 = 100 --> 1.0 kph  (Units make sense, they're 0.01kph)
2.0 / 00C8 = 200 --> 2.0 kph


FTMS Data (len=14): 84 04 64 00 14 00 00 02 00 FF FF FF 3F 00

1.0, 1 cal, 0.03 distance
[13:41:00.578] FTMS Data (len=14): 84 04 64 00 1E 00 00 03 00 FF FF FF 5E 00 