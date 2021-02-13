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
#include <printf.h>

// Some common board pin assignments
// WeMos R1
//RF24 radio(D4,D8); 
// WNRF
//LabRat Debug RF24 radio(D4,D3);
//RF24 radio(D2,D1);
RF24 radio(4,5);

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


byte addr_legacy[] = {0x81, 0xF0, 0xF0, 0xF0, 0xF0};//LEgacy for now

byte addr_wnrf[]  = {0xC1,0xDE,0xC0}; // Was LEDCTL (1EDC71)
byte addr_e131_bcast[] = {0xde,0xfa,0xda};
byte addr_ctrl_id[]={0x00,0x00,0x1D};
byte addr_ctrl_bcast[]={0xFF,0x00,0x1D};

int WnrfDriver::begin(NrfBaud baud, NrfChan chanid, int chan_size) {
    byte NrfRxAddress[] = {0xFF,0x3A,0x66,0x65,0x76};
    int alloc_size = 32;

    num_channels = chan_size;
    next_packet = 0;

    /* Allocate the Buffer Space */
    if (_dmxdata) free(_dmxdata);
    if (chan_size >32) {
       alloc_size=529; // # space for 1 byte header on 31 byte payloads
       startTime = micros();
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
        radio.setChannel(68+(2* (uint8_t) chanid));
    } else {
        radio.setChannel(80);  // LabRat - Legacy devices used channel 80
    }
    radio.setPayloadSize(32);
    radio.setAutoAck(false);
    // Baud Rate - to be added as CONFIG option
    setBaud(baud);
    radio.setCRCLength(RF24_CRC_16);
    //radio.enableDynamicAck();
    //radio.disableDynamicPayloads();
    // Power Level - to be added as CONFIG option
    radio.setPALevel(RF24_PA_HIGH); // LabRat - crank this up RF24_PA_MAX
  
    if (chan_size == 32) { // Legacy Mode
       radio.setAddressWidth(5);
       radio.openWritingPipe(addr_legacy); 
    } else {
       radio.setAddressWidth(3);
       radio.openWritingPipe(addr_e131_bcast);
       radio.openReadingPipe(1,addr_wnrf);
    }

    radio.stopListening();

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

    radio.stopListening();
    radio.write(&(_dmxdata[next_packet*32]),32,1);

    led_count--;
    if (num_channels == 32)
        startTime = millis();
    else {
        startTime = micros();
        next_packet = (next_packet+1)%17;
    }
    if (led_count == 0) {
        led_state ^=1;
        digitalWrite(1, led_state);
        if (num_channels ==32) 
            led_count =44;   // Legacy mode 44 single fps target
        else
            led_count = 44*17; // Mode 1: 44 DMX universe fps target
    }

    radio.startListening();
}

/* For the ESPixelStick visualation */

uint8_t* WnrfDriver::getData() {
    return _dmxdata;
}

#define num_channels 84
uint8_t values[num_channels];
uint8_t* WnrfDriver::getHistogram() {
   int chanId, loopcount; 
   memset(values, 0, sizeof(values));
   radio.setAutoAck(false);
   radio.stopListening();

   loopcount = 2;

   while (loopcount--) {
      for (chanId=0;chanId<num_channels;chanId++) {
         radio.setChannel(chanId);
         radio.startListening();
         delayMicroseconds(128);
         radio.stopListening();

         if (radio.testCarrier()) {
            ++values[chanId];
         }
      }
   }

   // Restore the setup
   return values;
}

static uint8_t poll_cycle = 0x00;

void WnrfDriver::triggerPoll() {
   byte tempPacket[32];
   /* Starts a state machine to use CTRL channel to query what devices are out there */
   // Initially only send this out on sub-group 0x00 (find devices 0x0001 through 0x00FE)

   // Only using a single POLL CYCLE at this time
   addr_ctrl_bcast[1] = poll_cycle;

   Serial.println("Sending QRY_DEVID");
   radio.stopListening();
   radio.openWritingPipe(addr_ctrl_bcast); 
   tempPacket[0] = 0x00;
   radio.write(tempPacket,32,1); // BROADCAST packet
   radio.openWritingPipe(addr_e131_bcast);
   radio.startListening();
   //radio.printDetails();
}

void WnrfDriver::sendGenericCmd(uint16_t devId, uint8_t cmd, uint16_t value) {
   byte tempPacket[32];

   // Address to the specific device
   addr_ctrl_id[1] = devId>>8;
   addr_ctrl_id[0] = devId&0xFF;

   Serial.print("Sending CMD: ");
   Serial.print(cmd);
   Serial.print(" to ");
   Serial.println(devId,HEX);

   radio.stopListening();
   radio.openWritingPipe(addr_ctrl_id); 
   tempPacket[0] = cmd;
   tempPacket[1] = value>>8;
   tempPacket[2] = value&0xFF;
   radio.write(tempPacket,32); // P2P uses AA on the receiver
   radio.openWritingPipe(addr_e131_bcast);
   radio.startListening();
}

void WnrfDriver::sendNewDevId(uint16_t devId, uint16_t value) {
   byte tempPacket[32] = {0x00,0x00,0x00,0x00,'L','A','B','R','A','T'};
   
   addr_ctrl_id[1] = devId>>8;
   addr_ctrl_id[0] = devId&0xFF;

   Serial.print("MTC CMD: PROG DEVICE: ");
   Serial.print(devId,HEX);
   Serial.print(" to ");
   Serial.println(value,HEX);

   radio.stopListening();
   radio.openWritingPipe(addr_ctrl_id); 
   // Update the payload packet
      // Protocol: 48-bit "string" to avoid accidental false positives.
      // byte 0 = 0x03 Write DeviceId command
      // byte 1 = New DevId MSB
      // byte 2 = New DevId LSB
      // byte 3 = CSUM 
      // byte 4..9 "LABRAT"
      // LabRat's Light Weight CSUM - try and avoid accidental re-programming
      // 48-bit "LABRAT" string + a computational hash of sorts.
      // Receiver can check the 40 bits *AND* calculate
      //  (((0x82^(value>>8)) ^ (0x65^(value&0xFF)))+0x84))
      // then add it to the CSUM total and *should* see 0xFF
      tempPacket[0] = 0x03;
      tempPacket[1] = value>>8;
      tempPacket[2] = value&0xFF;
      tempPacket[4] = (((0x82^(value>>8)) ^ (0x65^(value&0xFF)))+0x84)^0xFF;
 
   radio.write(tempPacket,32); // P2P uses AA on the receiver
   radio.openWritingPipe(addr_e131_bcast);
   radio.startListening();
}

/*
void WnrfDriver::sendNewDevId(uint16_t devId, uint16_t newId) {
   sendGenericCmd(devId, 0x03, newId);
}
*/

void WnrfDriver::sendNewChan(uint16_t devId, uint8_t chanId) {
   sendGenericCmd(devId, 0x02 /* cmd */, chanId<<8);
}

void WnrfDriver::sendNewStart(uint16_t devId, uint16_t start) {
   sendGenericCmd(devId, 0x01 /* cmd */, start);
}

void WnrfDriver::checkRx() {
    /* Was there a received packet? */
    uint8_t pipe;
    if (radio.available(&pipe)) {
        uint8_t payload[32];
        uint8_t psize=0x00;

        radio.read(payload,radio.getPayloadSize());
        // Was it on the COMMAND interface
        switch(payload[0]) {
            case 0: psize = 0x0B; break; // START
            case 1: psize = 0x04; break; // WRITE
            case 2: psize = 0x04; break; // COMMIT
            case 3: psize = 0x03; break; // AUDIT
            default: break;
        }

        // Deal with it
        Serial.print("Rx CTL packet: (");
        Serial.print(pipe);
        Serial.print(") ");
        for (int i=0;i<psize;i++) {
          char hex[3];
          sprintf(hex,"%2.2x",payload[i]);
          Serial.print(hex);
        }
        Serial.println(".");
    }
}
