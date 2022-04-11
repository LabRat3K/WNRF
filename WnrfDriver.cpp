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

void WnrfDriver::printIt(void) {
   radio.printDetails();
}

/* Set Data Rate based on nrfBaud option */
void WnrfDriver::setBaud(NrfBaud baud) {
   conf_baudrate = baud;

   if (baud == NrfBaud::BAUD_2Mbps) {
       radio.setDataRate(RF24_2MBPS);
   } else {
       radio.setDataRate(RF24_1MBPS);
   }
}

/* Set radio frequencey */
void WnrfDriver::setChan(NrfChan chanid) {
   conf_chanid = chanid;

   if ((uint8_t) chanid) {
      // Channel - should be 70 to 82
      radio.setChannel(68+(2* (uint8_t) chanid));
   } else {
      radio.setChannel(80);  // LabRat - Legacy devices used channel 80
   }
}


int WnrfDriver::begin() {
   return begin(NrfBaud::BAUD_2Mbps, NrfChan::NRFCHAN_LEGACY, 32);
}


// E1.31 addresses (Legacy and New)
byte addr_legacy[] = {0x81, 0xF0, 0xF0, 0xF0, 0xF0};
byte addr_wnrf_bcast[] = {0xde,0xfa,0xda};

// Address of the WNRF server
byte addr_wnrf_ctrl[]  = {0xC1,0xDE,0xC0}; // Was LEDCTL (1EDC71)
byte addr_device[]={0x00,0x00,0x1D};

int WnrfDriver::begin(NrfBaud baud, NrfChan chanid, int chan_size) {
    byte NrfRxAddress[] = {0xFF,0x3A,0x66,0x65,0x76};
    int alloc_size = 32;

    gnum_channels = chan_size;
    gnext_packet = 0;
    gadmin = false; //Never default to ADMIN mode

    gdevice_count = 0;

    /* Allocate the Buffer Space */
    if (_dmxdata) free(_dmxdata);
    if (chan_size >32) {
       alloc_size=529; // # space for 1 byte header on 31 byte payloads
       gstart_time = micros();
    } else {
       gstart_time = millis();
    }

    // Init all state timers
    gbeacon_timeout=gbeacon_bind_timeout=gbeacon_client_response_timeout= millis();


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

    setChan(chanid);

    radio.setPayloadSize(32);
    radio.setAutoAck(false); // Disable for broadcast

    // Baud Rate - to be added as CONFIG option
    setBaud(baud);
    radio.setCRCLength(RF24_CRC_16);

    //radio.enableDynamicAck();
    //radio.disableDynamicPayloads();

    // Power Level - to be added as CONFIG option
    radio.setPALevel(RF24_PA_MAX); 

    if (chan_size == 32) { // Legacy Mode
       radio.setAddressWidth(5);
       radio.openWritingPipe(addr_legacy);
    } else {
       radio.setAddressWidth(3);
       radio.openWritingPipe(addr_wnrf_bcast);
       radio.openReadingPipe(1,addr_wnrf_ctrl);
    }
   
    radio.startListening();
    printf_begin();
 

    // Default counters and state for the LED output
    gled_state = 1;
    gled_count = 1;
    return true;
}

void WnrfDriver::enableAdmin(void) {
    gadmin = true;
    Serial.println("Enable ADMIN");
    sendBeacon();
}

void WnrfDriver::disableAdmin(void) {
    gadmin = false;
    Serial.println("Disable ADMIN");
}

/*
 * Call from the MAIN loop to output a single payload/packet.
 * Passes a pointer to the next FRAME in the _dmxdata buffer,
 * to the RF24 pipeline
 */

void WnrfDriver::show() {
    if (!gadmin) {
	/* Send the packet */
        radio.stopListening();
        radio.write(&(_dmxdata[gnext_packet*32]),32,1);

        gled_count--;
        if (gnum_channels == 32)
            gstart_time = millis();
        else {
            gstart_time = micros();
            gnext_packet = (gnext_packet+1)%17;
        }
        if (gled_count == 0) {
            gled_state ^=1;
            digitalWrite(1, gled_state);
            if (gnum_channels ==32)
                gled_count =44;   // Legacy mode 44 single fps target
            else
                gled_count = 44*17; // Mode 1: 44 DMX universe fps target
        }
        radio.startListening();
    }
}

/* For the ESPixelStick visualation */

uint8_t* WnrfDriver::getData() {
    return _dmxdata;
}


/* Frequencey Scanner */

#define num_nrfchannels 84
uint8_t values[num_nrfchannels];
uint8_t* WnrfDriver::getNrfHistogram() {
   int chanId, loopcount;
   memset(values, 0, sizeof(values));
   radio.stopListening();

   loopcount = 2;

   while (loopcount--) {
      for (chanId=0;chanId<num_nrfchannels;chanId++) {
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
   setChan(conf_chanid);

   radio.startListening();
   return values;
}

int WnrfDriver::getDeviceList(tDeviceInfo **devices) {
  bool send_data = false;

   *devices =NULL;

   if (gadmin == true) {
      // Throttle to every 5 seconds
      if (millis()-gbeacon_client_response_timeout > 5000) {
         send_data = true;
      } else {
         if (gdevice_count==10) {
            send_data = true;
         }
      }

      if (send_data) {
         *devices = gdevice_list;
         return gdevice_count;
      }
   } else {
      // Throw away any pending data
      gdevice_count = 0;
   }
   return 0; // No data in the list
}

void WnrfDriver::clearDeviceList() {
   gdevice_count = 0;
};

void WnrfDriver::parseNrf_x88(uint8_t *data) {
   if (gdevice_count<10) { // Prevent Buffer Overrun
      char tempid[8];
      tDeviceInfo *temp = &(gdevice_list[gdevice_count++]);

      memcpy(&(temp->dev_id),&data[1],3); // Device Id
      temp->type  = data[4];           // Device Type
      temp->blv   = data[5];           // Boot Loader Version */
      temp->apm   = data[6];           // App Magic Number */
      temp->apv   = data[7];           // Boot Loader Version */
      temp->start = data[8]|(data[9]<<8); 

      Serial.print("** Client Device detected [");
      for (int i=0;i<16;i++) {
         char hex[3];
         sprintf(hex,"%2.2x",data[i]);
         Serial.print(hex);
      }
      Serial.println("]");
   }
}

void WnrfDriver::sendBeacon() {
   byte tempPacket[32];

   // ONLY send beacon if in ADMIN mode
   if (gadmin == true) {  
      // Throttle to every 2 seconds
      if (millis()-gbeacon_timeout > 2000) {
         Serial.println("Sending QRY_DEVID");
         radio.stopListening();
         radio.openWritingPipe(addr_wnrf_ctrl);
         tempPacket[0]=0x85;
         radio.write(tempPacket,32,1); // BROADCAST packet
      
         gbeacon_timeout = millis();
         radio.openWritingPipe(addr_wnrf_bcast);
         radio.startListening();
      }
   }
}

void WnrfDriver::sendGenericCmd(tDevId devId, uint8_t cmd, uint16_t value) {
   byte tempPacket[32];

   // Address to the specific device
   memcpy(addr_device, devId.id, 3);

   Serial.print("Sending CMD: ");
   Serial.print(cmd);
   Serial.print(" to ");
   Serial.print(devId.id[2],HEX);
   Serial.print(devId.id[1],HEX);
   Serial.println(devId.id[0],HEX);

   radio.stopListening();
   radio.openWritingPipe(addr_device);
   tempPacket[0] = cmd;
   tempPacket[1] = value>>8;
   tempPacket[2] = value&0xFF;
   radio.write(tempPacket,32); // P2P uses AA on the receiver
   radio.openWritingPipe(addr_wnrf_bcast);
   radio.startListening();
}

void WnrfDriver::sendNewDevId(tDevId devId, tDevId newId) {
   byte tempPacket[32] = {0x00,0x00,0x00,0x00,'L','A','B','R','A','T'};

   memcpy(addr_device, devId.id, 3);
   Serial.print("MTC CMD: PROG DEVICE: ");
   Serial.print(devId.id[2],HEX);
   Serial.print(devId.id[1],HEX);
   Serial.print(devId.id[0],HEX);
   Serial.print(" to ");
   Serial.print(newId.id[2],HEX);
   Serial.print(newId.id[1],HEX);
   Serial.println(newId.id[0],HEX);

   radio.stopListening();
   radio.openWritingPipe(addr_device);
   // Update the payload packet
      // Protocol: 48-bit "string" to avoid accidental false positives.
      // byte 0 = 0x03 Write DeviceId command
      // byte 1 = New DevId MSB
      // byte 2 = New DevId ...
      // byte 3 = New DevId LSB
      // byte 4..9 "LABRAT"
      // LabRat's Light Weight CSUM 
      // 48-bit "LABRAT" string
      // REMOVED:  + a computational hash of sorts.
      // Receiver can check the 40 bits *AND* calculate
      //  (((0x82^(value>>8)) ^ (0x65^(value&0xFF)))+0x84))
      // then add it to the CSUM total and *should* see 0xFF
      tempPacket[0] = 0x03;
      memcpy(tempPacket,newId.id,3); 
/*
      tempPacket[1] = value>>8;
      tempPacket[2] = value&0xFF;
      tempPacket[3] = (((0x82^(value>>8)) ^ (0x65^(value&0xFF)))+0x84)^0xFF;
*/

   radio.write(tempPacket,32); // P2P uses AA on the receiver
   radio.openWritingPipe(addr_wnrf_bcast);
   radio.startListening();
}

void WnrfDriver::sendNewChan(tDevId devId, uint8_t chanId) {
   sendGenericCmd(devId, 0x02 /* cmd */, chanId<<8);
}

void WnrfDriver::sendNewStart(tDevId devId, uint16_t start) {
   sendGenericCmd(devId, 0x01 /* cmd */, start);
}

void WnrfDriver::checkRx() {
    /* Was there a received packet? */
    uint8_t pipe;
    if (radio.available(&pipe)) {
        uint8_t payload[32];

        radio.read(payload,32);

        // Was it on the COMMAND interface?
        // Look at pipe?
        switch (payload[0]) {
           case 0x88: // Beacon response from client devices
              parseNrf_x88(payload);
              break;
           case 0x85:
              Serial.println("** WNRF Beacon detected!!");
              Serial.println("** A second WNRF is in the area!!");
              break;
           default:
            // Deal with it
            Serial.print("Rx CTL packet: (");
            Serial.print(pipe);
            Serial.print(") ");
            for (int i=0;i<8;i++) {
              char hex[3];
              sprintf(hex,"%2.2x",payload[i]);
              Serial.print(hex);
            }
            Serial.println(".");
            break;
       }
    }

    //  If in ADMIN mode - Timeouts
    sendBeacon();
}
