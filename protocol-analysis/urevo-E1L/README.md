UREVO is using FTMS.

Wireshark filter for my E1W
btcommon.addr == 47:a4:8f:21:ce:5c

Service: 0x1800
Discovering characteristics...
Characteristic: 0x2a00  Handle: 0x0003
Service: 0x180a
Discovering characteristics...
Characteristic: 0x2a29  Handle: 0x0006
Characteristic: 0x2a24  Handle: 0x0008
Characteristic: 0x2a25  Handle: 0x000A
Characteristic: 0x2a27  Handle: 0x000C
Characteristic: 0x2a26  Handle: 0x000E
Characteristic: 0x2a28  Handle: 0x0010
Characteristic: 0x2a23  Handle: 0x0012
Service: 0x1826
Discovering characteristics...
Characteristic: 0x2acc  Handle: 0x0015
Characteristic: 0x2acd  Handle: 0x0017  [NOTIFY] 
Characteristic: 0x2ad3  Handle: 0x001A  [NOTIFY]
Characteristic: 0x2ad4  Handle: 0x001D
Characteristic: 0x2ad5  Handle: 0x001F
Characteristic: 0x2ad6  Handle: 0x0021
Characteristic: 0x2ad7  Handle: 0x0023
Characteristic: 0x2ad8  Handle: 0x0025
Characteristic: 0x2ad9  Handle: 0x0027            <-- FTMS Control
Characteristic: 0x2ada  Handle: 0x002A  [NOTIFY]
Characteristic: a580d216-7087-5e6c-36b3-77c02f551c85  Handle: 0x002D
Characteristic: c4208999-8d92-bee1-4456-2068528eccf6  Handle: 0x002F  [NOTIFY]
Characteristic: 62b817cb-47db-c59e-b675-1a66e37a5196  Handle: 0x0032
Service: 0xfff0
Discovering characteristics...
Characteristic: 0xfff1  Handle: 0x0035  [NOTIFY]  <-- UREVO Mobile App Uses this.
Characteristic: 0xfff2  Handle: 0x0038            <-- 02510B03 seems to initiate data coming in on 0xfff1
Service: 0xfee0
Discovering characteristics...
Characteristic: 0xfee1  Handle: 0x003B  [NOTIFY]
Characteristic: 0xfee2  Handle: 0x003E



Observations:
- Power output does only go up when you're walking on the device (could be used to improve step accuracy if user leaves it running while they go to bathroom)
- Sessions are cummulative... it's a true "pause" instead of a stop like Lifespan is (and i thought Sperax was)..
- SPEED  (Device / FTMS received by ble)
  - 2.0 / 0.32 kph 0x0141 = 321 
  - 1.8 / 0x0121 = 289
  - 1.6 / 0101 = 257
- I think UREVO displays on it's display the MPH for speed.  


0.006225680934 is the conversion factor...




SPERAX:
- Similar issue, 
1.0 / 0064 = 100 --> 1.0 kph  (Units make sense, they're 0.01kph)
2.0 / 00C8 = 200 --> 2.0 kph


FTMS Data (len=14): 84 04 64 00 14 00 00 02 00 FF FF FF 3F 00

1.0, 1 cal, 0.03 distance
[13:41:00.578] FTMS Data (len=14): 84 04 64 00 1E 00 00 03 00 FF FF FF 5E 00 