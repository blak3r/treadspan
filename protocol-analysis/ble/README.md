# BLE Analysis

In order to figure out the protocol I used a BLE sniffer dongle made by Nordic Semiconductor [nRF52840-Dongle](https://www.mouser.com/ProductDetail/Nordic-Semiconductor/nRF52840-Dongle?qs=gTYE2QTfZfTbdrOaMHWEZg%3D%3D&utm_id=10726844835&gad_source=1&gclid=CjwKCAiA5Ka9BhB5EiwA1ZVtvL4PkodTo7ARTL3xDi8wmj3ibHzpW_tZkgvGv4OUMwz1zSe8oHUp5xoCyxQQAvD_BwE)
This device allowed me to view all traffic between the mobile app and the Omni Console.

In short, the protocol is 

1. Subscribe to notifications on the UUID FFF1 (Handle 0x2B)
2. Write to the device a payload like "A1 88 00 00 00" to the write characteristic at FFF2. (Handle 0x2D)
3. The peripheral will then send a response on the "Notify" characteristic (Handle 0x2B). 
4. There are 8 unique opcodes A1 88 = Steps for example.



## Command Opcode 
These are the payloads you write to the WRITE Characteristic to solicit a response.
`A1 81 00 00 00` - No idea, never seen it change, always returns `A1 AA FF 00 00 00`
`A1 88 00 00 00` - Gets Steps
`A1 86 00 00 00` - No idea, like `81`, doesn't change always returns `A1 AA 00 00 00 00`
`A1 87 00 00 00` - CALORIES, `A1 AA 00 0A 00 00` --> 000A = 10 calories.
`A1 85 00 00 00` - PROBABLY distance - increases slowly over course of session, `A1 AA 01 1F 00 00` I think is 1.3 miles.
`A1 89 00 00 00` - Elapsed Session Time! `A1 AA <HOURS> <MINS> <SECS> 00`
`A1 82 00 00 00` - Probably AVG Speed???
`A1 91 00 00 00` - System State: 03 = running, 05 = paused, 04 = at summary screen, 01 is standby.

^--- those are all i saw the iphone app request.

I was really hoping that there would be some means of getting the time from the console.  I thought the console would store
the starting time or the ending time of the session so that i wouldn't need to manage a RTC via NTP on the arduino cause that 
adds the complexity of needing to get a users wifi credentials. 

So, I did try brute forcing commands A1 00 --> A1 FF.
I found it will respond A1 FF 00 00 00 00 whenever you give it a command opcode it doesn't like.

This did result in finding two new commands that I never saw the mobile app right, but the values do not appear to be time as that 
is generally 4 bytes. These are the new mystery codes `A1 7A` and `A1 7B`... maybe one is the total run
time of the treadmill? 
```text
REQ 79: A1 79 
RESP 79: A1 FF 00 00 00 00 
REQ 7A: A1 7A 
RESP 7A: A1 AA 00 00 50 5F  <-- good?! 20575 in DEC
REQ 7B: A1 7B 
RESP 7B: A1 AA 00 02 67 FB  <-- good?!
REQ 7C: A1 7C 
RESP 7C: A1 FF 00 00 00 00 
REQ 7D: A1 7D 
```




[2025-02-10 17:28:32] RESP: A1 AA 01 05 18 00
[2025-02-10 17:28:40] RESP: A1 AA 01 05 20 00     0x18 + 8 = 0x20... implying it is a number of seconds...  

## Timestamped Events.

I manually recorded the timestamps of when I pressed various buttons on the console to start and stop the treadmill.
You can use this log here in coordination with <FEB10.pcap> to further evaluate if you want.

[2025-02-10 17:29:47] 1.2MPH 3950 steps, 1.3 miles, 235 kcal ~1:06:45 session.
[2025-02-10 17:30:40] 1.2MPH 4002 steps, 1.3 miles, 239 kcal, 1:07:33 session.
[2025-02-10 17:33:30] hard power down.
[2025-02-10 17:35:48] started at 0.4mph

[2025-02-10 17:37:45] Increase to MPH 1.2MPH,37 steps
[2025-02-10 17:38:29] Inc to 2.0MPH, 80 steps, 0.0 miles ~8 kcal.
[2025-02-10 17:39:30] Dec to 0.4MPH, 164 steps, 0.0 miles ~11 kcal.
[2025-02-10 17:40:20] stopped session, 0.0 miles, 181 steps, 4:39 duration, 14kcal.
[2025-02-10 17:41:00] pause again to get to summary screen
[2025-02-10 17:41:18] return to standby.


[2025-02-10 17:35:48] started at 0.4mph

[2025-02-10 17:36:56] RESP: A1 AA 00 28 00 00 
[2025-02-10 17:37:04] RESP: A1 AA 00 28 00 00
[2025-02-10 17:37:12] RESP: A1 AA 00 28 00 00
[2025-02-10 17:37:21] RESP: A1 AA 00 28 00 00
[2025-02-10 17:37:29] RESP: A1 AA 00 28 00 00
[2025-02-10 17:37:37] RESP: A1 AA 00 28 00 00
[2025-02-10 17:37:45] RESP: A1 AA 00 28 00 00 <-- increased the speed here... but if it's an average... maybe it takes a while?
[2025-02-10 17:37:54] RESP: A1 AA 00 28 00 00
[2025-02-10 17:38:02] RESP: A1 AA 00 28 00 00
[2025-02-10 17:38:10] RESP: A1 AA 00 32 00 00 <-- changes here
[2025-02-10 17:38:18] RESP: A1 AA 00 32 00 00
[2025-02-10 17:38:27] RESP: A1 AA 00 32 00 00
[2025-02-10 17:38:35] RESP: A1 AA 00 3C 00 00 <-- increase happened before here. to 2.0
[2025-02-10 17:38:43] RESP: A1 AA 00 46 00 00
[2025-02-10 17:38:51] RESP: A1 AA 00 46 00 00
[2025-02-10 17:39:00] RESP: A1 AA 00 50 00 00
[2025-02-10 17:39:08] RESP: A1 AA 00 50 00 00
[2025-02-10 17:39:16] RESP: A1 AA 00 5A 00 00
[2025-02-10 17:39:24] RESP: A1 AA 00 5A 00 00
[2025-02-10 17:39:33] RESP: A1 AA 00 5A 00 00
[2025-02-10 17:39:41] RESP: A1 AA 00 5A 00 00
[2025-02-10 17:39:49] RESP: A1 AA 00 5A 00 00
[2025-02-10 17:39:57] RESP: A1 AA 00 5A 00 00
[2025-02-10 17:40:06] RESP: A1 AA 00 5A 00 00
[2025-02-10 17:40:14] RESP: A1 AA 00 50 00 00
[2025-02-10 17:40:22] RESP: A1 AA 00 50 00 00
[2025-02-10 17:40:30] RESP: A1 AA 00 50 00 00
[2025-02-10 17:40:39] RESP: A1 AA 00 50 00 00
[2025-02-10 17:40:47] RESP: A1 AA 00 50 00 00
[2025-02-10 17:40:55] RESP: A1 AA 00 50 00 00
[2025-02-10 17:41:03] RESP: A1 AA 00 50 00 00
[2025-02-10 17:41:12] RESP: A1 AA 00 50 00 00
[2025-02-10 17:41:20] RESP: A1 AA 00 50 00 00
[2025-02-10 17:41:28] RESP: A1 AA 00 50 00 00
[2025-02-10 17:41:36] RESP: A1 AA 00 50 00 00
