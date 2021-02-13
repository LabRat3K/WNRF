/*
* WnrfDriver.cpp - Wnrf driver code for ESPixelStick
*
* based on Project: ESPixelStick by Shelby Merrick
* author: Andrew Williams (LabRat)
*
*  This program is provided free for you to use in any way that you wish,
*  subject to the laws and regulations where you are using it.  Due diligence
*  is strongly suggested before using this code.  Please give credit where due.
*
*  The Author makes no warranty of any kind, express or implied, with regard
*  to this program or the documentation contained in this document.  The
*  Author shall not be liable in any event for incidental or consequential
*  damages in connection with, or arising out of, the furnishing, performance
*  or use of these programs.
*
*
* This drive has two modes of operation: 
*     Mode 1: LEGACY: Allow for working with pre-existing DMX2NRF based product.
*        These devices expect a single 32 byte payload, pulled from an offset
*        in the E.131 Universe (or DMX stream for the DMX2NRF adapter).
*        All payloads are broadcast via a 2Mbps stream to channel 80, address 
*        0x0F0F0F0F81.
*     Mode 2: Device will broadcast a full 512 byte Univese at 2MBps, to the 
*        selected channel, address 0x0F0F0F0F82. Not only is this using a
*        different channel frequency, but altering the broadcast target address
*        will ensure that the new mode does not interfere with legacy systems.
* 
* The plan is to suppot a second communications channel from the device(s) back
* to the WNRF controller. This channel will be used for control paylods, not
* dimmer values. 
*
* Mode 2 enhancements (To Do List):
*     a. Ability to query what NRF devices are in the network.
*     b. Ability to configure NRF devices (start address) remotely.
*     c. Firmware Upgrade(s) to remote devices.
*     d. Serial Numbers - so that we can support multiple devices and know
*        which is which.
*
*/

#include <SPI.h>
#include "RF24.h"
#include "WnrfDriver.h"


// Some common board pin assignments
// WeMos R1
//RF24 radio(D4,D8); 
// WNRF
RF24 radio(D4,D3);

/* Set Data Rate based on nrfBaud option */
void WnrfDriver::setBaud(NrfBaud baud) {
   if (baud == NrfBaud::BAUD_2Mbps) {
       radio.setDataRate(RF24_2MBPS);
   } else {
       radio.setDataRate(RF24_1MBPS);
   }
}


int WnrfDriver::begin() {
   return begin(NrfBaud::BAUD_2Mbps, NrfChan::NRFCHAN_LEGACY, 32);
}

int WnrfDriver::begin(NrfBaud baud, NrfChan chanid, int chan_size) {
    byte NrfTxAddress[] = {0x81,0xF0,0xF0,0xF0,0xF0};
    byte NrfRxAddress[] = {0xFF,0x3A,0x66,0x65,0x76};
    int alloc_size = 32;

    num_channels = chan_size;
    next_packet = 0;

    /* Allocate the Buffer Space */
    if (_dmxdata) free(_dmxdata);
    if (chan_size >32) {
       alloc_size=529; // # space for 1 byte header on 31 byte payloads
       startTime = micros();
       //NrfTxAddress[0] = 0x82;
    } else {
       startTime = millis();
    }
    
    if (_dmxdata = static_cast<uint8_t *>(malloc(alloc_size))) {
        memset(_dmxdata, 0, alloc_size);
    } else {
        return false;
    }

    // Prepopulate the Payload # index packets
    for (int i=0; i<chan_size;i+=32) {
        _dmxdata[i] = i/32;
    }

    /* NRF Init */
    radio.begin();
    if ((uint8_t) chanid) {
        // Channel - should be 101 to 119
        radio.setChannel(99+(2* (uint8_t) chanid));
    } else {
        radio.setChannel(80);  // LabRat - Legacy devices used channel 80
    }
    radio.setPayloadSize(32);
    radio.setAddressWidth(5);
    radio.setAutoAck(false);
    // Baud Rate - to be added as CONFIG option
    setBaud(baud);
    radio.setCRCLength(RF24_CRC_16);
    //radio.disableDynamicPayloads();
    // Power Level - to be added as CONFIG option
    radio.setPALevel(RF24_PA_LOW); // LabRat - crank this up RF24_PA_MAX
  
    // TxAddress - to be a CONFIG option
    radio.openWritingPipe(NrfTxAddress); 
    radio.openReadingPipe(0,NrfRxAddress); 
    NrfRxAddress[0] = radio.getChannel();
    radio.openReadingPipe(1,NrfRxAddress); 

    radio.stopListening();
    //radio.printDetails();

    // Default counters and state for the LED output
    led_state = 1; 
    led_count = 1;
    return true;
}


/* 
 * Call from the MAIN loop to output a single payload/packet.
 * Passes a pointer to the next FRAME in the _dmxdata buffer, 
 * to the RF24 pipeline
 */

void WnrfDriver::show() {
	/* Send the packet */
    bool result = radio.write(&(_dmxdata[next_packet*32]),32);

    led_count--;
    if (num_channels == 32)
        startTime = millis();
    else {
        startTime = micros();
        next_packet = (next_packet+1)%17;
    }
    if (led_count == 0) {
        led_state ^=1;
        digitalWrite(D10, led_state);
        if (num_channels ==32) 
            led_count =44;   // Legacy mode 44 single fps target
        else
            led_count = 44*17; // Mode 1: 44 DMX universe fps target
    }
}

/* For the ESPixelStick visualation */

uint8_t* WnrfDriver::getData() {
    return _dmxdata;
}

