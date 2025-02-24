# TreadSpan (an alternative to LifeSpan Fit)

`2025-02-23: TreadSpan is under active development.  If you're a developer, you can use this immediately.  
We are still working on going through the AppStore review process and improving the install page.`

Lifespan makes great hardware (Treadmills) but their software is the achilles heel.  It's so close to being an incredible
platform for logging steps. 

This solution solves the biggest limitation of the Lifespan OEM solution.  It will seamlessly log each treadmill session
to a chip that allows you to sync all sessions in one go with a mobile app. Meaning, 
you can start and stop your treadmill as much as you want and when you want to sync it to AppleHealth you open up the app and 
it'll sync all the sessions done prior.  (You don't have to sync each session individually).

Prior to getting my lifespan, I used to have to remember to put my iphone in my pocket whenever i was on the treadmill so that my steps would get 
counted.  A watch solution doesn't work since your arms are stationary. 

## Instructions

1. Buy a ~$17 [supported chip](https://amzn.to/43eFNhn). (See Hardware section below for other options)
2. Install the [USB Serial drivers.](https://www.silabs.com/developer-tools/usb-to-uart-bridge-vcp-drivers?tab=downloads)
3. Connect the chip to your computer using a USB-C cable which has data.
4. Upload firmware to chip using [this web based installer](https://blak3r.github.io/treadspan-web-installer/).
5. Be sure to add wifi credentials.
6. Go to the Apple AppStore and install the "TreadSpan" app.
7. Open the app and follow instructions.

After the firmware is loaded, you can connect the chip to any old USB-C charger you have laying around.
It does not need to stay connected to your computer.

### Hardware

[![lilygo-ttgo-tdisplay.png](screenshots/lilygo-ttgo-tdisplay.png)](https://amzn.to/43eFNhn)

[(Has a Case) LILYGO ESP32 T-Display Module for Arduino CH9102F Chip TTGO Development Board with Shell Version](https://amzn.to/43eFNhn)
[(No Case Version) - LILYGO ESP32 T-Display Module for Arduino CH9102F Chip TTGO Development Board NO Case](https://amzn.to/43eFNhn)

Case version sometimes takes longer to ship and for reasons I don't understand is sold for less than the version without case ($20).
As of Feb 2025, the case version is $17 + 9.50 shipping and the no case version is $20 with free shipping.  

If you have a 3D printer you can print a case, [Files available here](https://github.com/Xinyuan-LilyGO/TTGO-T-Display/tree/master/3d_file). 

If the links provided have gone bad, here is what you're looking to buy.
* Brand: LilyGo
* Chipset: ESPRESSIF-ESP32    <-- Notice it does NOT end with S3
* Model: TTGO T-Display
* Flash: 16MB
* Display: IPS ST7789V 1.14 Inch

DO NOT BUY THESE SIMILAR OPTIONS:
* T-Display-S3 AMOLED ESP32-S3 with 1.91 Inch RM67162 Display TTGO Development Board Wireless Module
* T-Display S3 ESP32-S3 1.9-inch ST7789 LCD Display Touchable Screen TTGO Wireless Module Welding Pin Development Board
* T-Display-S3-Long 16MB Flash ESP32-S3 TTGO Development Board with 3.4-inch Touch Display TFT LCD Wireless Modules

Technically, any ESP32 based arduino should work.  The suggested hardware is nice since it has an LCD on it so it'll 
be helpful if you don't have wifi configured properly... or want to know how many sessions are on the device etc.

## How it Works

OMNI Console ----(BLE) ---> Arduino ----(BLE)----> Mobile App

In order to accomplish this, we need a two part solution: a new piece of hardware and a new mobile app.

First, we program a chip called an Arduino.  This connects to the Omni Console using a Bluetooth Low
Energy (BLE) protocol.  This allows us to know the status of each workout session in realtime and importantly save EACH
session to the arduino's internal memory.  

Next, we install an alternative application to mobile phone which replaces Lifespan Fit app gets installed on your iPhone.  This connects to the Arduino
which is also running a BLE Server and has a custom BLE service that allows the iphone to download each session that was
saved on the device.  

![arduino-serial-monitoring-method.png](screenshots/arduino-serial-monitoring-method.png)


## Development Environment Setups

This section is for developers that want to build the software / tweak things.
I hope that people will write custom firmware for other treadmills that leverage the same BLE protocol so they can leverage
the mobile app without modification.

The source code to the mobile app is available in the ios-app folder.

### Features
- It's a barebones application that fetches all logged sessions from the Arduino and adds them to HealthKit.  It does this in one step, no need to do the sync to apple health separately. 
- It doesn't require you to login and create an account.  No information is shared.
- It also has some visualizations that are very similar to the step graphs in Apple Health but it'll break out steps taken on your treadmill  
vs. steps logged organically through a watch or iphone.

### Setup Arduino Development Environment.

1. Setup the Arduino IDE for ESP32 support. See this guide: <https://randomnerdtutorials.com/installing-the-esp32-board-in-arduino-ide-windows-instructions/>
2. Open the .ino file in the arduino folder.
3. You'll need to install these libraries:install a few libraries such as NimBLE (v2.2.1)
   * Nimble (v2.2.1)
   * TFT_eSPI (2.5.43) - After you install this library, you'll have to edit User_Setup.h and User_Setup_Select.h as shown in [this image](/screenshots/TFT_eSPI_Setup.png).
4. Default Upload Speed of 921600 would not work for me.  I'd get a packet error.  Goto Tools->Upload Speed and select 460800

The chip is powered by USB.  So, you can find an old charger you have laying around and put the chip somewhere out of the way.  It needs to be close to the Omni console but can basically just be in the same room.  
You can wrap the chip in electric tape if you're worried about it touching other metal.

### Mobile App
You'll need to have a Mac, install XCode, do things like create a developer certificate, and put your iphone into developer mode.
It's a fair amount of steps.  If I get enough interest i'll bite the bullet and go through the AppStore process so you don't have to do this.

### Protocol Analysis
If you're interested in learning more about the reverse engineering of protocol attempts. Then, look in the [Protocol Analysis](/protocol-analysis/README.md) folder.
Here I include raw captures of the traffic over both the serial port and BLE as well as my notes from reversing the protocols.

## Contributors Welcome

- Would love some donations, spent at least $200 on an IOS Developer Account to publish to appstore, and evaluating hardware options. (Not to mention probably 50 hours of development)
- Port the iOS App to Android. (AI tools should be able to do most of it)
- The serial port version is likely out of reach for someone who has never used a breadboard before.  If someone wants to create a store and sell assembled hardware, i'll gladly link you! 
- Someone to document the full process of getting XCode setup, a developer certificate, how to put the phone into developer mode.


## FAQS

### I have an Android Phone, do you have a solution
I haven't tried to make an Android app.  That being said i'm fairly confident if you have any development skills and can use
ChatGPT.  You can get it to make you an app pretty easily by providing the SyncView.swift file and telling it to implement the 
BLE protocol described on this page.

### I have the Retro Console, Can I use this solution?
In theory, yes.  It is possible to get the steps through the serial port.  The downside of this approach is you need 
more complicated hardware and if you're not careful, there is a possibility that you could damage your treadmill if you 
don't connect things correctly.

The hardware is more complicated because you have to create small circuit board that will sniff the serial port traffic.

The approach of using BLE makes the hardware very simple (you just need a programmed arduino). 

### Can I use this in an Office Environment, where there are lots of treadmills?
You would need to modify both the arduino code and mobile app to limit the device it connects with.  This solution 
was designed to make it as easy as possible to get working so it scans for devices matching names... having multiple
treadmill consoles or multiple arduinos programed in range is going to cause unpredicatable/unhandled results. 

### Why does the device require WiFi?

The device needs WiFi solely to maintain an accurate clock via NTP (Network Time Protocol), which is essential for correctly timestamping multiple stored sessions. Believe me, I didn't want to introduce this extra setup step, but it’s necessary to ensure reliable session tracking.

I originally hoped to pull clock information from the Omni console (which does have a built-in clock), but unfortunately, that clock is only for display purposes. When the LifeSpan Fit app retrieves session data from the console, it only provides session durations, not actual start/end timestamps. To integrate with Apple HealthKit, the app estimates these times by setting the end time as the moment you press **"End and Sync"**, then subtracting the session duration to determine the start time.

#### Are there alternatives to using WiFi?

In theory, yes—but each alternative has significant drawbacks:

##### 1. Track elapsed time since startup
- The device could count seconds since power-on and store this in EEPROM. Upon syncing, the mobile app could then estimate session times.
- **Issues**: If power is lost, all stored session timestamps are effectively useless. Additionally, microcontrollers experience clock drift (minutes per day), leading to inaccurate timestamps. This could result in treadmill sessions overlapping with other tracked activities (e.g., walks recorded by your smartwatch).

##### 2. Sync time via the mobile app
- The app could periodically set the device’s clock.
- **Issues**: This would require users to open the app before logging sessions, adding unnecessary friction.

##### 3. Use a real-time clock (RTC) module with a battery
- This would keep time reliably, even if power is lost.
- **Issues**: It would require additional hardware which means novices need to solder or breadboard to make connections! Additionally, it requires an initial time sync to seed the time (similar to WiFi).

#### Why not implement one of these alternatives?

The main reason is accuracy and complexity. These alternatives introduce potential errors or require significant programming effort to make them stable and reliable. I'd rather focus my time on improving other aspects of the project.

That said, if someone finds a better approach and wants to contribute, I'd be happy to accept a pull request!

### Is it possible to control treadmill through mobile app?
I have not found anyway to control the treadmill through the BLE protocol.
It is theoretically possible to write commands to the treadmill via the serial protocol.
But, realistically you can't unless you were willing to sacrifice your existing console and only 
control it through the mobile app (you would also need the more complicated hardware setup and custom firmware).

## TODOs (Things actively working on)
HIGH
- Create a means to configure the WiFi (with a bluetooth)
- Make BLE code very robust.
- Make BLE session end detection more robust, had issues where it thought it was "unknown";
- Add means of detecting if EEPROM is initialized.
- Connecting to wifi screen on TFT.

MED
- Change the mobile app bundle id?
- MOBILE app, metrics page
  - 6M and year view don't display correct stuff. 
- Create the FAQ Page Swift View
- WIFI Reconnect code on Arduino.

LOW
- Splash screen for TREADSPAN (mobile app)
- What would happen if you just turned off the treadmill while a session was active... need something to timeout if no data serial commands or BLE commands come in for a while for a while.
- Maybe increase eeprom size to allow for more sessions?
- Change the Service UUIDs to be something less generic that could conflict.
- Could further optimize the serial code to prevent losing commands... but i'm not sure it going to make a difference.

## Get Help / Support

If you have a question go here: https://github.com/blak3r/treadspan/discussions
If you want to report a bug go here: https://github.com/blak3r/treadspan/issues


1305876 - with Improv andDebug on.