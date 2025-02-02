# Better LifeSpan Fit

This is a solution that makes 

Lifespan makes great hardware (Treadmills) but their software seems like it was contracted out to the lowest bidder
and didn't bother to QA it. It's terrible.

This solution solves the biggest limitation of the Lifespan OEM solution.  It enables multiple session logging.  Meaning, 
you can start and stop your treadmill as much as you want and when you want to sync it to AppleHealth you open up the app and 
it'll sync all the sessions done prior.  (You don't have to sync each session individually).

Prior to getting my lifespan, I used to have to remember to put my iphone in my pocket whenever i was on the treadmill so that my steps would get 
counted.  A watch solution doesn't work since your arms are stationary. 

### How it works

OMNI Console ----(BLE) ---> Arduino ----(BLE)----> Mobile App

First, we program a chip called an Arduino.  This connects to the Omni Console using a Bluetooth Low
Energy (BLE) protocol.  This allows us to know the status of each workout session in realtime and importantly save EACH
session to the arduino's internal memory.  

Next, we  replacement application for the Lifespan Fit app gets installed on your iPhone.  This connects to the Arduino
which is also running a BLE Server and has a custom BLE service that allows the iphone to download each session that was
saved on the device.  


### Mobile App Features
- It's a barebones application that fetches all logged sessions from the Arduino and adds them to HealthKit.  It does this in one step, no need to do the sync to apple health separately. 
- It doesn't require you to login and create an account.  No information is shared.
- It also has some visualizations that are very similar to the step graphs in Apple Health but it'll break out steps taken on your treadmill  
vs. steps logged organically through a watch or iphone.

### Hardware
1. Buy one of these: https://www.amazon.com/gp/product/B07QCP2451/ref=ppx_yo_dt_b_search_asin_title?ie=UTF8&th=1 (link sells you two of them, buying 1 is 9.99 so for $1 you get two which is just a good idea to have a spare)
2. Setup the Arduino IDE for ESP32 support. See this guide: <https://randomnerdtutorials.com/installing-the-esp32-board-in-arduino-ide-windows-instructions/>
3. Open the .ino file in the arduino folder.
4. Upload to your device.

The chip is powered by a USB Micro connector.  So, you can find an old charger you have laying around and put the chip somewhere out of the way.  It needs to be close to the Omni console but can basically just be in the same room.  
You can wrap the chip in electric tape if you're worried about it touching other metal.

### Mobile App
You'll need to have a Mac, install XCode, do things like create a developer certificate, and put your iphone into developer mode.
It's a fair amount of steps.  If I get enough interest i'll bite the bullet and go through the AppStore process so you don't have to do this.


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
treadmill consoles or multiple arduinos programed in range is gonna cause unpredicatable/unhandled results. 