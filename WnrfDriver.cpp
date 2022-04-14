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
#include <FS.h> // Defn of 'File'

// Some common board pin assignments
// WeMos R1
//RF24 radio(D4,D8);
// WNRF
RF24 radio(4,5);

// BootLoader related states
#define NRF_CTL_NONE          (0x00)
#define NRF_CTL_W4_BIND_ACK   (0x01)
#define NRF_CTL_BOUND         (0x02)
#define NRF_CTL_W4_SETUP_ACK  (0x03)
#define NRF_CTL_W4_WRITE_ACK  (0x04)
#define NRF_CTL_W4_COMMIT_ACK (0x05)
#define NRF_CTL_W4_AUDIT_ACK  (0x06)
#define NRF_CTL_W4_HEART_ACK  (0x07)
// Application related states
#define NRF_CTL_W4_CHAN_ACK   (0x08)
#define NRF_CTL_W4_DEVID_ACK  (0x09)

#define LED_NRF 15

// To do.. move into the Private Data and
// accommodate multiple requests?
//static File ota_file;
File ota_files[MAX_P2P_PIPES];

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

int WnrfDriver::storeContext(void * context) {
  int i;
  for (i=0;i<MAX_P2P_PIPES;i++) {
     if (gPipes[i].context==NULL) {
        gPipes[i].context = context;
        return i;
     }
  }
  return -1;
}

int WnrfDriver::clearContext(void * context ) {
  int i;
  for (i=0;i<MAX_P2P_PIPES;i++) {
     if (gPipes[i].context==context) {
        gPipes[i].context = NULL;
        return i;
     }
  }
  return -1;
}

int WnrfDriver::getContext(uint8_t pipeid, void **context) {
   if ((pipeid<0) || (pipeid>(MAX_P2P_PIPES-1))) {
      *context = gPipes[pipeid].context;
      return 1;
   }
   return 0; // False - not found
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

    // Async Functions - callback context
    nrf_async_otaflash=NULL;
    nrf_async_rfchan=NULL;
    nrf_async_devid=NULL;
    nrf_async_startaddr=NULL;
    nrf_async_devlist=NULL;


    gnum_channels = chan_size;
    gnext_packet = 0;
    gadmin = false; //Never default to ADMIN mode

    gdevice_count = 0;
    gbeacon_active = false;

    for (int i=0; i<MAX_P2P_PIPES;i++) {
       gPipes[i].state   = NRF_CTL_NONE;
       gPipes[i].context = NULL;
       gPipes[i].bind_reason = BIND_NONE;
       memcpy(gPipes[i].rxaddr,addr_wnrf_ctrl,3);
       gPipes[i].rxaddr[0]=i+2;
    }

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
    gbeacon_active = true;
    digitalWrite(LED_NRF,HIGH);
    sendBeacon();
}

void WnrfDriver::disableAdmin(void) {
    gadmin = false;
    gbeacon_active = false;
    digitalWrite(LED_NRF,LOW);
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
            digitalWrite(LED_NRF, gled_state); // Blink when transmitting
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

void WnrfDriver::sendDeviceList(void) {
  bool send_data = false;

   if (gadmin == true) {
      // Throttle to every 5 seconds
      if (millis()-gbeacon_client_response_timeout > 5000) {
         if (gdevice_count) {
           send_data = true;
         }
      } else {
         if (gdevice_count>8) {
            send_data = true;
         }
      }

      if (send_data) {
        if (nrf_async_devlist) {
           nrf_async_devlist(gdevice_list,gdevice_count);
           gbeacon_client_response_timeout = millis();
        }
        gdevice_count = 0;
      }
   } else {
     gdevice_count = 0;
   }
}

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

#ifdef DEBUG
      Serial.print("** Client Device detected [");
      for (int i=0;i<16;i++) {
         char hex[3];
         sprintf(hex,"%2.2x",data[i]);
         Serial.print(hex);
      }
      Serial.println("]");
#endif
   }
   sendDeviceList();
}

void WnrfDriver::sendBeacon() {
   byte tempPacket[32];

   // ONLY send beacon if in ADMIN mode
   if (gbeacon_active == true) {
      // Throttle to every 2 seconds
      if (millis()-gbeacon_timeout > 5000) {
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

void  WnrfDriver::rx_ackbind(uint8_t pipe) {
    // Flag Pipe as Bound
    // Look at Cached Request to determine next State
    switch(gPipes[pipe].bind_reason) {
       case BIND_FLASH:
          Serial.print("BIND ACK success :");
          Serial.println(pipe);

// DEBUG - tell the WS we're good
          if (nrf_async_otaflash) {
            nrf_async_otaflash((tDevId *)gPipes[pipe].txaddr,gPipes[pipe].context, 0);
            // Close the file
            if (ota_files[pipe]) {
               ota_files[pipe].close();
               gPipes[pipe].state = NRF_CTL_NONE;
            }
          }
// END DEBUG
          // Hand control to the FILE parsing engine
          break;
       case BIND_DEVID:
          // Send the "New Device Id" message
          // Change state to W4_DEV ACK
          break;
       case BIND_START:
          // Send the "New Device Id" message
          // Change state to W4_START ACK
          break;
       default:
          {
           char errstr[80];
           sprintf(errstr,"Unknown BIND ack: pipe=%d state=%d\n",pipe, gPipes[pipe].bind_reason);
           // Send error
           Serial.println(errstr);
          }
          break;
    }
    //    (Start Flash, ChanUp, DevId Up)
}

bool WnrfDriver::tx_bind(uint8_t pipe) {
     uint8_t msg[32];
     bool retCode = false;

        msg[0] = 0x87;

        // Allocate a pipe and send that address to the client
        // (for now use the default)
        // Format <0x87><DevId0><DevId1><DevId2><P2P0><P2P1><P2P2>

        memcpy(&(msg[1]),gPipes[pipe].txaddr,3);
        memcpy(&(msg[4]),gPipes[pipe].rxaddr,3);

        msg[16]=millis()&0xff; // prevent issues of same payload being ignored

        radio.stopListening();   // Ready to write EN_RXADDR_P0 = 1
        radio.setAutoAck(0,false);// As a broadcast first ...

        radio.openWritingPipe(gPipes[pipe].txaddr); // Who I'm sending it to
        gPipes[pipe].waitTime=millis();

        retCode = radio.write(msg,32); // Want to get AA working here

        radio.openReadingPipe(pipe+2,gPipes[pipe].rxaddr);
        radio.setAutoAck(pipe+2,true);
        radio.startListening(); // EN_RXADDR_P0 = 0

        return retCode;
}


int WnrfDriver::nrf_bind(tDevId *devId, uint8_t reason, void * context) {
   // Scan pipe list to see if a pipe is available
   int pipe = storeContext(context);

   if (pipe == -1) {
      // Rejecting request - no Pipes available
      return -1;
   }

   gbeacon_active = false; // Timeout handler can re-enable

   gPipes[pipe].state = NRF_CTL_W4_BIND_ACK;
   memcpy(gPipes[pipe].txaddr,(char *) devId, 3);
   gPipes[pipe].bind_reason = reason;

   // Send the NRF BIND request
   tx_bind(pipe);

   //   Start Timer
   gPipes[pipe].waitTime = millis();
   gPipes[pipe].waitCount = 0;

   return pipe;
}

int WnrfDriver::sendGenericCmd(tDevId *devId, uint8_t cmd, uint16_t value) {
   byte tempPacket[32];

   // Address to the specific device
   memcpy(addr_device, devId, 3);

#ifdef DEBUG
   Serial.print("Sending CMD: ");
   Serial.print(cmd);
   Serial.print(" to ");
   Serial.print(devId->id[2],HEX);
   Serial.print(devId->id[1],HEX);
   Serial.println(devId->id[0],HEX);
#endif

   radio.stopListening();
   radio.openWritingPipe(addr_device);
   tempPacket[0] = cmd;
   tempPacket[1] = value>>8;
   tempPacket[2] = value&0xFF;
   radio.write(tempPacket,32); // P2P uses AA on the receiver
   radio.openWritingPipe(addr_wnrf_bcast);
   radio.startListening();
   return 0;
}

int WnrfDriver::nrf_devid_update(tDevId *devId, tDevId *newId, void * context) {
   byte tempPacket[32] = {0x00,0x00,0x00,0x00,'L','A','B','R','A','T'};

   memcpy(addr_device, devId, 3);
   Serial.print("MTC CMD: PROG DEVICE: ");
   Serial.print(devId->id[2],HEX);
   Serial.print(devId->id[1],HEX);
   Serial.print(devId->id[0],HEX);
   Serial.print(" to ");
   Serial.print(newId->id[2],HEX);
   Serial.print(newId->id[1],HEX);
   Serial.println(newId->id[0],HEX);

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
      memcpy(tempPacket,newId,3);
/*
      tempPacket[1] = value>>8;
      tempPacket[2] = value&0xFF;
      tempPacket[3] = (((0x82^(value>>8)) ^ (0x65^(value&0xFF)))+0x84)^0xFF;
*/

   radio.write(tempPacket,32); // P2P uses AA on the receiver
   radio.openWritingPipe(addr_wnrf_bcast);
   radio.startListening();
   return 0;
}

int WnrfDriver::nrf_rfchan_update(tDevId *devId, uint8_t chanId, void * context) {
  return sendGenericCmd(devId, 0x02 /* cmd */, chanId<<8);
}

int WnrfDriver::nrf_startaddr_update(tDevId *devId, uint16_t start, void * context) {
  return  sendGenericCmd(devId, 0x01 /* cmd */, start);
}


int WnrfDriver::nrf_flash(tDevId *devId, char *fname, void * context) {
   int retCode = -15;
   // Check we can access the file?
   if (fname) {
      // Attempt to open the file
      Serial.print("Opening ");
      Serial.println(fname);

      // Send the BIND and enter wait for BIND timeout
      int pipe = nrf_bind(devId, BIND_FLASH, context);
      if (pipe>=0) {
         ota_files[pipe]= SPIFFS.open(fname,"r");
         if (ota_files[pipe]) {
            retCode = 0;
         } else {
            Serial.println("Failed to save context");
         }
      }
   }
   return retCode;
}


void WnrfDriver::checkRx() {
    /* Was there a received packet? */
    uint8_t pipe;
    if (radio.available(&pipe)) {
        uint8_t payload[32];

        radio.read(payload,32);

        if (pipe == 1) { // RX on the broadcast address
           if (payload[0] ==0x88){ // Beacon response from client devices
              parseNrf_x88(payload);
           } else {
                if (payload[0] == 0x85) { // BEACON message
                  Serial.println("** WNRF Beacon detected!!");
                  Serial.println("** A second WNRF is in the area!!");
                }
           }
        
        } else {

          if ((pipe<2) || (pipe>5)) {
             Serial.print("ERROR: INVALID PIPE INDEX (");
             Serial.print(pipe);
             Serial.println(")");
             return;
          }

          pipe -=2;  //Index into gPipes array

          tPipeInfo * pid = &(gPipes[pipe]);

          tDevId tempId;

          // Deal with it
          switch (pid->state) {
             case NRF_CTL_W4_BIND_ACK:
               if (payload[0] == 0x87) {
                 rx_ackbind(pipe);
               }
               Serial.println("Ack completed");
               break;
             case NRF_CTL_W4_SETUP_ACK:
               if (payload[0] == 0x81) {
                 pid->state = NRF_CTL_W4_WRITE_ACK;
                 // callback to inform the webSocket client
                 // Blink LED to show BIND status?
               }
               break;
             case NRF_CTL_W4_WRITE_ACK:
               if (payload[0] == 0x82) {
                 pid->state = NRF_CTL_W4_COMMIT_ACK;
                 // callback to inform the webSocket client
                 // Blink LED to show BIND status?
               }
               break;
             case NRF_CTL_W4_COMMIT_ACK:
               if (payload[0] == 0x83) {
                 // callback to inform the webSocket client
                 // Blink LED to show BIND status?
                 // Send a REBOOT request
               }
               break;
             case NRF_CTL_W4_AUDIT_ACK:
               if (payload[0] == 0x84) {
                 pid->state = NRF_CTL_BOUND;
                 // callback to inform the webSocket client
                 // Blink LED to show BIND status?
               }
               break;
             case NRF_CTL_NONE: // Do nothing - warn the console?
             default:
               Serial.print("Unknown Rx packet: (");
               Serial.print(pipe+2);
               Serial.print(") ");
               for (int i=0;i<8;i++) {
                 char hex[3];
                 sprintf(hex,"%2.2x",payload[i]);
                 Serial.print(hex);
               }
               Serial.println(".");
               break;
          } // End Switch STATE
       } // Pipe 2-5
    } // End handling of radio packet

    // -- Check for Timeouts
    uint32_t now = millis();
    for (int i=0;i<MAX_P2P_PIPES;i++) {
       tPipeInfo * pid = &gPipes[i];
       switch (pid->state) {
           case NRF_CTL_W4_BIND_ACK:
              if (now - pid->waitTime > 1000) {
                 if (pid->waitCount>10) {
                    Serial.println("TIMEOUT waiting for BIND");
                    // 10 second failure to bind
                    nrf_async_otaflash((tDevId *)pid->txaddr,pid->context, -1);
                    pid->state = NRF_CTL_NONE; 
                    pid->context = NULL;
                 } else {
                    pid->waitTime = now;
                    pid->waitCount++;
                    Serial.println("Re-bind request");
                    tx_bind(i);
                 }
              }
              break;
           default:
              break;
       }
    }
    //  If in ADMIN mode - Timeouts
    sendBeacon();
    sendDeviceList();
}
