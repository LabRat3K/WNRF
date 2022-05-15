# WNRF Firmware - based on ESPixelStick Firmware

WNRF - a WiFi to nRF24L01 Gateway


[![Join the chat at https://gitter.im/forkineye/ESPixelStick](https://badges.gitter.im/forkineye/ESPixelStick.svg)](https://gitter.im/forkineye/ESPixelStick)
[![Donate](https://img.shields.io/badge/Donate-PayPal-green.svg)](https://paypal.me/ShelbyMerrick)
[![Build Status](https://travis-ci.org/forkineye/ESPixelStick.svg?branch=master)](https://travis-ci.org/forkineye/ESPixelStick)

[[https://github.com/LabRat3K/WNRF/tree/master/wiki/enclosure_1.jpg]]

This is the Arduino firmware for the ESP8266 based Wifi-to-nRF bridge (based on the ESPixelStick).  The bridge acts to convert wireless E1.31 sACN payloads to nRF packets. The gateway can handle a full 512 universe, with options to support multiple WNRF gateways in the same installation (different frequencies).

To keep things simple, many of the advances streaming options of the ESPixelStick have been stripped from the client (with more to go). New feature include integration with the PIC based nRFLoader bootloader, allowing for a target client firmware to be upgraded over the air (OTA). 

## Hardware

The WNRF hardware combines an ESP-07 (or 12) with a nRF24L01 radio, to create the WNRF device. The footprint was designed to go into the Hammon 1593K enclosure, in order to create a nice tidy (and inexpensive) end product. Any nRF24L01 radio using the two row 8-pin header, should work, but I would recommend selecing one with PA+LNA capabilities. An external antenna is preferable, to maximise your outgoing signal capabilities.

## Requirements

This section is direct from the ESPixelStick project. 
Along with the Arduino IDE, you'll need the following software to build this project:

- [Adruino for ESP8266](https://github.com/esp8266/Arduino) - Arduino core for ESP8266
- [Arduino ESP8266 Filesystem Uploader](https://github.com/esp8266/arduino-esp8266fs-plugin) - Arduino plugin for uploading files to SPIFFS
- [gulp](http://gulpjs.com/) - Build system required to process web sources.  Refer to the html [README](html/README.md) for more information.

The following libraries are required:

Extract the folder in each of these zip files and place it in the "library" folder under your arduino environment

- [ArduinoJson](https://github.com/bblanchon/ArduinoJson) - Arduino JSON Library
- [ESPAsyncE131](https://github.com/forkineye/ESPAsyncE131) - Asynchronous E1.31 (sACN) library
- [ESPAsyncTCP](https://github.com/me-no-dev/ESPAsyncTCP) - Asynchronous TCP Library
- [ESPAsyncUDP](https://github.com/me-no-dev/ESPAsyncUDP) - Asynchronous UDP Library
- [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer) - Asynchronous Web Server Library
- [async-mqtt-client](https://github.com/marvinroger/async-mqtt-client) - Asynchronous MQTT Client

## Important Notes on Compiling and Flashing

- In order to upload your code to the ESP8266 you must put it in flash mode and then take it out of flash mode to run the code. To place your ESP8266 in flash mode your GPIO-0 pin must be connected to ground.
- Device mode is now a compile time option to set your device type and is configured in the top of the main sketch file.  Current options are ```ESPS_MODE_PIXEL``` and ```ESPS_MODE_SERIAL```.  The default is ```ESPS_MODE_PIXEL``` for the ESPixelStick hardware.
- Web pages **must** be processed, placed into ```data/www```, and uploaded with the upload plugin. Gulp will process the pages and put them in ```data/www``` for you. Refer to the html [README](html/README.md) for more information.
- In order to use the upload plugin, the ESP8266 **must** be placed into programming mode and the Arduino serial monitor **must** be closed.
- ESP-01 modules **must** be configured for 1M flash and 128k SPIFFS within the Arduino IDE for OTA updates to work.
- For best performance, set the CPU frequency to 160MHz (Tools->CPU Frequency).  You may experience lag and other issues if running at 80MHz.
- The upload must be redone each time after you rebuild and upload the software

## Supported Outputs

The ESPixelStick firmware can generate the following outputs from incoming E1.31 streams, however your hardware must support the physical interface.

### Pixel Protocols

- None - not supporting direct output to pixels in the WNRF

### Serial Protocols

- None - stripped to reduce firmware footprint

## MQTT Support

MQTT can be configured via the web interface.  When enabled, a payload of "ON" will tell the ESPixelStick to override any incoming E1.31 data with MQTT data.  When a payload of "OFF" is received, E1.31 processing will resume.  The configured topic is used for state, and the command topic will be the state topic appended with ```/set```.

For example, if you enter ```porch/esps``` as the topic, the state can be queried from ```porch/esps``` and commands can be sent to ```porch/esps/set```

If using [Home Assistant](https://home-assistant.io/), it is recommended to enable Home Assistant Discovery in the MQTT configuration.  Your ESPixelStick along with all effects will be automatically imported as an entity within Home Assistant utilzing "Device ID" as the friendly name.  For manual configuration, you can use the following as an example.  When disabling Home Assistant Discovery, ESPixelStick will attempt to remove its configuration entry from your MQTTT broker.

```yaml
light:
  - platform: mqtt
    schema: json
    name: "Front Porch ESPixelStick"
    state_topic: "porch/esps"
    command_topic: "porch/esps/set"
    brightness: true
    rgb: true
    effect: true
    effect_list:
      - Solid
      - Blink
      - Flash
      - Rainbow
      - Chase
      - Fire flicker
      - Lightning
      - Breathe
```

Here's an example using the mosquitto_pub command line tool:

```bash
mosquitto_pub -t porch/esps/set -m '{"state":"ON","color":{"r":255,"g":128,"b":64},"brightness":255,"effect":"solid","reverse":false,"mirror":false}'
```

## Resources

- Firmware: [http://github.com/forkineye/ESPixelStick](http://github.com/forkineye/ESPixelStick)
- Hardware: [http://forkineye.com/ESPixelStick](http://forkineye.com/ESPixelStick)

## Credits

- The great people at [diychristmas.org](http://diychristmas.org) and [doityourselfchristmas.com](http://doityourselfchristmas.com) for inspiration and support.
- [Bill Porter](https://github.com/madsci1016) for initial Renard and SoftAP support.
- Bill Porter and [Grayson Lough](https://github.com/GraysonLough) for initial DMX support.
- [Rich Danby](https://github.com/cinoan) for fixes and helping polish the front-end.
- [penfold42](https://github.com/penfold42) for fixes, brightness, gamma support, and zig-zag / grouping.
  - penfold42 also maintains PWM support in their fork located [here](https://github.com/penfold42/ESPixelBoard).
- [Austin Hodges](https://github.com/ahodges9) for effects support and MQTT cleanup.
- [Matthias C. Hormann](https://github.com/Moonbase59) — some MQTT & effects cleanup.
