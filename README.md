# Air Hockey Table GIF Scoreboard
Scoreboard for air hockey table using MatrixPortal M4

I've got this air hockey table at home (https://www.medalsports.com/product/md-sports-90-air-powered-hockey-table/) and I thought the scoring left a little to be desired with just the lights and pitiful sound effects.  I've been playing around with some other Pi RGB Matrix projects which I'll link to below, and when I saw Adafruit's MatrixPortal I thought it was a great method to incorporate the use of a RGB Matrix without the overhead and "upkeep" of a Pi.  I figured I should add some animated GIF's to it as well on score events because why not :)

## Parts List
  * LED RGB Matrix - There are two possibilities here, and the main differentiating factor is viewer distance from the panel.  The lower the panel pitch, the less "pixely" the panel appears.
    * 4mm pitch:  https://www.adafruit.com/product/2278
    * 3mm pitch:  https://www.adafruit.com/product/2279
    * If you've got a way to cut this reliably, a piece of diffuser acrylic is GREAT:  https://www.adafruit.com/product/4594
  * MatrixPortal M4:  https://www.adafruit.com/product/4745
  * VCNL4010 Proximity Sensors (x2):  https://www.adafruit.com/product/466
  * TCA9548A I2C Multiplexer:  https://www.adafruit.com/product/2717
  * Qwiic/STEMMA QT JST to breadboard cable (will extend later):  https://www.sparkfun.com/products/14425
  * USB-C power supply (standard 2.4A would do fine)
  * Breadboard and wiring, or skip all that and go right to soldering
  
## 11/17/2020 - Development Warning
This is in very active development and as of this writing unfinished.  Current known issues:
* Score counter does not stop at 10 like it will in the finished version, counter will increment forever
* GIF player is hardcoded to explicit files, final release will incorporate rotation for score and gameover events

## Assembly and Wiring

I assume that you are at least a little familiar with the MatrixPortal M4 and are able to get files on and off the device.  If you are not, then I suggest you read Adafruit's documentation on how that's done before you proceed: https://learn.adafruit.com/adafruit-matrixportal-m4  You will want to make sure you have the newest firmware, and copy the entire GIF folder from /images/gifs into the CIRCUITPY USB drive.

Wiring this up is pretty simple, I haven't tackled the whole "how to attach to table" yet, but the general function is pretty easy to get going.

First thing's first, [attach MatrixPortal, then attach the Qwiic/STEMMA QT cable to the header.](/images/readme/20201117_210644.jpg)

If you have a breadboard, [arrange the sensors and the multiplexer as shown.](/images/readme/20201117_210726.jpg)  The SCL and SDA pins from the Qwiic header are going into the right of the multiplexer.  Red Goal (left sensor) is going to position 0 (SD0 and SC0), Blue Goal (right sensor) is going to position 1 (SD1 and SC1).  Combine 3.3v power and GND from Qwiic header on breadboard power rail.

## Pi RGB Matrix projects worth exploring
https://github.com/riffnshred/nhl-led-scoreboard
