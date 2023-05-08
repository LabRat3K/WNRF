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
#include "HexParser.h"
#include "RatLights.h"

// Some common board pin assignments
#ifdef WEMOS_D1
   // WeMos R1
   RF24 radio(D4,D8);
#else
   // WNRF
   RF24 radio(4,5);
#endif

// BootLoader related states
#define NRF_CTL_NONE          (0x00)
#define NRF_CTL_W4_BIND_ACK   (0x01)
#define NRF_CTL_W4_SETUP_ACK  (0x02)
#define NRF_CTL_W4_WRITE_ACK  (0x03)
#define NRF_CTL_W4_COMMIT_ACK (0x04)
#define NRF_CTL_W4_AUDIT_ACK  (0x05)
// Application related states
#define NRF_CTL_W4_CHAN_ACK   (0x06)
#define NRF_CTL_W4_DEVID_ACK  (0x07)
#define NRF_CTL_W4_RF_ACK     (0x08)


// LabRat's Lights message
#define LLM_ACK   (0x01)
#define LLM_NACK  (0x00)

// Common BIND routines, so need to store
// the reason (and parameters) for the BIND
// request
#define BIND_FLASH  (0x00)
#define BIND_DEVID  (0x01)
#define BIND_START  (0x02)
#define BIND_RFCHAN (0x03)
#define BIND_NONE   (0xFF)

// To do.. move into the Private Data and
// accommodate multiple requests?
//static File ota_file;
File ota_files[MAX_P2P_PIPES];

// Device Id Conversion routines
uint32_t txt2id(const char* str){
  uint32_t temp=0x00;
  sscanf(str,"%lx",&temp);
  return (temp);
}

void id2txt(char * str, uint32_t id) {
  sprintf(str,"%6.6lX",id);
}

// Debugging Code
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
uint32_t addr_wnrf_bcast = 0xC0DE42;

// Address of the WNRF server
uint32_t addr_wnrf_ctrl = 0xC0DEC1;

int WnrfDriver::begin(NrfBaud baud, NrfChan chanid, int chan_size) {
    byte frfRxAddress[] = {0xFF,0x3A,0x66,0x65,0x76};
    int alloc_size = 32;

    // Async Functions - callback context
    nrf_async_otaflash=NULL;
    nrf_async_devlist=NULL;

    // These aren't done yet
    nrf_async_rfchan=NULL;
    nrf_async_devid=NULL;
    nrf_async_startaddr=NULL;


    gnum_channels = chan_size;
    gnext_packet = 0;
    gadmin = false; //Never default to ADMIN mode

    gdevice_count = 0;
    gbeacon_active = false;

    for (int i=0; i<MAX_P2P_PIPES;i++) {
       gPipes[i].state   = NRF_CTL_NONE;
       gPipes[i].context = NULL;
       gPipes[i].bind_reason = BIND_NONE;
       gPipes[i].rxaddr = addr_wnrf_ctrl&0xFFFF00 | (i+2)&0xFF;
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
       radio.setAutoAck(1,false); // Disable broadcast Rx
    }

    radio.setAutoAck(0,false); // Disable for E1.31 broadcast

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
      // Throttle to once a second
      if (millis()-gbeacon_client_response_timeout > 1000) {
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
           Serial.print("Sending list of ");
           Serial.print(gdevice_count);
           Serial.println(" devices.");
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
      tConfigMessage *config = (tConfigMessage *) &(data[1]);

      temp->dev_id = config->deviceId[2]<<16 | config->deviceId[1]<<8 | config->deviceId[0]; // Device Id

      temp->pcb_type    = config->pcb_type;   // Device Type
      temp->pcb_version = config->pcb_version;
      temp->processor   = config->processor;   // Device Type
      temp->numchan     = config->num_channels[1]<<8|config->num_channels[0];
      temp->blv         = config->bl_version;  // Boot Loader Version
      temp->apm         = config->bl_appmagic; // App Magic Number
      temp->apv         = config->app_version; // Boot Loader Version
      temp->rfchan      = config->app_rfchan; // nrf RF channel
      temp->rfrate      = config->app_rfrate; // nrf RF data rate (250K/1M/2M)
      temp->start       = config->start_chan[1]<<8|config->start_chan[0];   // Start Channel
      temp->cap	        = config->admin_cap;  // Administration Capabilities

      Serial.print("Nrf(1) Rx88:[");
      Serial.print(temp->dev_id,HEX);
      Serial.print("] CAP:0x");
      Serial.println(temp->cap,HEX);
   }
   sendDeviceList();
}

void WnrfDriver::sendBeacon() {
   byte tempPacket[32];

   // ONLY send beacon if in ADMIN mode
   if (gbeacon_active == true) {
      // Throttle to every 2.5 seconds
      if (millis()-gbeacon_timeout > 2500) {
         Serial.print("**TX BEACON to 0x");
         Serial.print(addr_wnrf_ctrl, HEX);
         Serial.println("**");
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

void WnrfDriver:: rx_ackaudit(uint8_t pipe, char result) {
     tPipeInfo * pid = &gPipes[pipe];
 Serial.println("Rx audit ACK");
     pid->state = NRF_CTL_NONE;
     if (nrf_async_otaflash) {
        nrf_async_otaflash(pid->txaddr,pid->context, result);
     }

     // Send a reboot request to the client device
     tx_reset(pipe);
     pid->context = NULL;

}

void WnrfDriver:: rx_ackcommit(uint8_t pipe) {
   tPipeInfo * pid = &gPipes[pipe];

   if (ota_files[pipe]) {
      if (ota_files[pipe].available()) {
         tx_setup(pipe,false); // Continue
      } else {
         // When End of File close the connection
         ota_files[pipe].close();
         pid->state = NRF_CTL_W4_AUDIT_ACK;
         tx_audit(pipe);
      }
      // Start Timeout for ACK failure
      pid->waitTime = millis();
      pid->waitCount = 0;
   } else {
      Serial.println("rx ACK COMMIT - file error");
   }
}

void WnrfDriver:: rx_ackwrite(uint8_t pipe) {
     tPipeInfo * pid = &gPipes[pipe];
     pid->state = NRF_CTL_W4_COMMIT_ACK;
     tx_commit(pipe);
     // Start Timeout for ACK failure
     pid->waitTime = millis();
     pid->waitCount = 0;
}

void WnrfDriver:: rx_acksetup(uint8_t pipe) {
     tPipeInfo * pid = &gPipes[pipe];
     pid->state = NRF_CTL_W4_WRITE_ACK;
     tx_write(pipe);
     // Start Timeout for ACK failure
     pid->waitTime = millis();
     pid->waitCount = 0;
}

void  WnrfDriver::rx_ackbind(uint8_t pipe) {
    tPipeInfo * pid = &gPipes[pipe];

    Serial.print("BIND ACK success :");
    Serial.println(pipe);
    // Flag Pipe as Bound
    // Look at Cached Request to determine next State
    switch(pid->bind_reason) {
       case BIND_FLASH:
          if (ota_files[pipe]) {
             tx_setup(pipe,false);
          } else {
             pid->state = NRF_CTL_NONE;
          }
          // Hand control to the FILE parsing engine
          break;
       case BIND_DEVID:
          // Send the "New Device Id" message
          // Change state to W4_DEVID ACK
          tx_newid(pipe,pid->newid);
          pid->state = NRF_CTL_W4_DEVID_ACK;
          break;
       case BIND_START:
          // Send the "New Device Id" message
          // Change state to W4_START ACK
          sendGenericCmd(pipe, 0x01 /* cmd */, pid->e131_start);
          pid->state = NRF_CTL_W4_CHAN_ACK;

          break;
       case BIND_RFCHAN:
          // Send the "New Device Id" message
          // Change state to W4_RF ACK
          sendGenericCmd(pipe, 0x02 /* cmd */, pid->rf_chan);
          pid->state = NRF_CTL_W4_RF_ACK;
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
    // Start Timeout for ACK failure
    pid->waitTime = millis();
    pid->waitCount = 0;
}

bool WnrfDriver::tx_reset(uint8_t pipe) {
  Serial.println("Tx Reset");
  return sendGenericCmd(pipe, 0x86, 0x00);
}

//0x83,<StartAddrL>,<StartAddrH>,<ImageSizeL>,<ImageSizeH>,<CSUML>,<CSUMH>,<WRITE_REQUEST>
bool WnrfDriver::tx_audit(uint8_t pipe) {
  tPipeInfo *pid = &(gPipes[pipe]);

  char msg[32];
  bool retCode = false;
  uint8_t size = 0;
  uint16_t addr;


  // Hack.. use the msg buffer initially as a string
   sprintf(msg,"Tx audit ADDR:%4.4x  Size:%4.4X CSUM:%4.4X",pid->fw.start, (pid->fw.size)/2, pid->fw.csum);
   Serial.println(msg);

   msg[0] = 0x83; // AUDIT
   msg[1] = pid->fw.start&0xff;
   msg[2] = pid->fw.start>>8&0xff;
   msg[3] = pid->fw.size>>1&0xff; // Note: /2 as it's number of WORDS
   msg[4] = pid->fw.size>>9&0xff;
   msg[5] = pid->fw.csum&0xff;
   msg[6] = pid->fw.csum>>8&0xff;
   msg[7] = 0x01;

   radio.stopListening(); // Ready to Write - EN_RXADDRP0 = 1
   radio.openWritingPipe(pid->txaddr);
   radio.setAutoAck(0,true);

   retCode =  radio.write(msg,32); // Want to get AA working here

   radio.setAutoAck(0,false);  // Allow Broadcasting
   radio.startListening();     // EN_RXADDRP0 = 0

   Serial.println("Tx Audit Completed");
  return retCode;
 }

bool WnrfDriver::tx_commit(uint8_t pipe) {
  tPipeInfo *pid = &(gPipes[pipe]);

  char msg[32];
  bool retCode = false;
  uint8_t size = 0;
  uint16_t addr;
  int csum = 0;

   memset(msg, 0xff, sizeof(msg));
   if (ota_files[pipe]) {
      File *file = &(ota_files[pipe]);

      size = lhe_read_record_at(&(ota_files[pipe]),pid->fw.offset,&addr,&(msg[0]));

      for (int i=0;i<sizeof(msg);i++){
         csum-=msg[i];
      }

      csum &= 0xFF;

      msg[0] = 0x82; // COMMIT
      msg[1] = 0x01;
      msg[2] = (csum&0xFF);
      msg[3] = msg[30]; // Last Word
      msg[4] = msg[31];

      radio.stopListening(); // Ready to Write - EN_RXADDRP0 = 1
      radio.openWritingPipe(pid->txaddr);
      radio.setAutoAck(0,true);
  
      Serial.print("COMMIT  csum=0x");
      Serial.print(csum&0xFF,HEX);
      Serial.println(".");

      retCode =  radio.write(msg,32); // Want to get AA working here

      radio.setAutoAck(0,false);  // Allow Broadcasting
      radio.startListening();     // EN_RXADDRP0 = 0
   } else {
      Serial.println("tx_commit - invalid file handle");
   }
   return retCode;

}

bool WnrfDriver::tx_write(uint8_t pipe) {
  tPipeInfo *pid = &(gPipes[pipe]);

  char msg[34];  // Note - oversized
  bool retCode = false;
  uint8_t size = 0;
  uint16_t addr;

  // Restructure so we only read once .. vs 3 times
  //  Read Header..
  //  Read Payload (store word (or 2))
  //  Commit + Word
  memset(msg,0xff,sizeof(msg));

   if (ota_files[pipe]) {

      size = lhe_read_record_at(&(ota_files[pipe]),pid->fw.offset,&addr,&(msg[1]));
      // *NOTE* - offset 1 so that we have room for the command

      Serial.print("WRITE ");
      Serial.print(size);
      Serial.println(" bytes");
      msg[0] = 0x81;
      pid->state = NRF_CTL_W4_WRITE_ACK;

      radio.stopListening(); // Ready to Write - EN_RXADDRP0 = 1
      radio.openWritingPipe(pid->txaddr);
      radio.setAutoAck(0,true);

      retCode =  radio.write(msg,32); // Want to get AA working here

      radio.setAutoAck(0,false);  // Allow Broadcasting
      radio.startListening();     // EN_RXADDRP0 = 0
   } else {
      Serial.println("tx_write - invalid file handle");
   }
   return retCode;

}

bool WnrfDriver::tx_setup(uint8_t pipe, bool resend) {
  tPipeInfo *pid = &(gPipes[pipe]);

  char msg[32];
  bool retCode = false;
  uint8_t size = 0;
  uint16_t addr;

   // Ugly - can we optimize to avoid needing to wipe this every loop
   memset(msg,0xff,sizeof(msg));

   if (ota_files[pipe]) {
      if (resend) {
         size = lhe_read_record_at(&(ota_files[pipe]),pid->fw.offset,&addr,&(msg[0]));
      } else {
         pid->fw.offset = ota_files[pipe].position(); // Ensure I can come back here

         size = lhe_read_record(&(ota_files[pipe]),&addr,&(msg[0]));
         if (size>0) { // There is data to send

            pid->fw.size+= size;

            if (pid->fw.start == 0) {
               pid->fw.start = addr;
            }

            // Ugly.. WORD checksum
            for (int i=0;i<size;i+=2){
               pid->fw.csum-=(msg[i+1]<<8|msg[i]);
            }
         } else {
            // No additional data..
            // Jump to Audit
            ota_files[pipe].close();
            pid->state = NRF_CTL_W4_AUDIT_ACK;
            Serial.println('.');
            tx_audit(pipe);
            return retCode;
         }
      }
      pid->state = NRF_CTL_W4_SETUP_ACK;

      Serial.print("SETUP:0x");
      Serial.print(addr&0xffff,HEX);
      Serial.print(" ");
      Serial.print(pid->fw.size,HEX);
      Serial.println();
      msg[0] = 0x80; // flash write SETUP
      msg[1] = addr&0xff;
      msg[2] = addr>>8&0xff;
      msg[3] = 0x01; // Erase Flash

      radio.stopListening(); // Ready to Write - EN_RXADDRP0 = 1
      radio.openWritingPipe(pid->txaddr);
      radio.setAutoAck(0,true);

      retCode =  radio.write(msg,32); // Want to get AA working here

      radio.setAutoAck(0,false);  // Allow Broadcasting
      radio.startListening();     // EN_RXADDRP0 = 0
   } else {
      Serial.println("tx_setup - invalid file handle");
   }
   return retCode;
}

bool WnrfDriver::tx_bind(uint8_t pipe) {
   uint8_t msg[32];
   bool retCode = false;

      msg[0] = 0x87;

      // Allocate a pipe and send that address to the client
      // (for now use the default)
      // Format <0x87><DevId0><DevId1><DevId2><P2P0><P2P1><P2P2>
  Serial.print("Tx Bind request to pipe:");
  Serial.print(pipe);
  Serial.print(" 0x");
  Serial.print(gPipes[pipe].txaddr,HEX);
  Serial.print(":");

   // Will this work to copy lower 3 bytes from the uint32_t?
      memcpy(&(msg[1]),&gPipes[pipe].txaddr,3);
      memcpy(&(msg[4]),&gPipes[pipe].rxaddr,3);

   Serial.print("MSG:");
   for (int i=0;i<6;i++) {
      Serial.print(msg[i],HEX);
      Serial.print(",");
   }
   Serial.println(msg[6],HEX);

      radio.openReadingPipe(pipe+2,gPipes[pipe].rxaddr);
      radio.setAutoAck(pipe+2,true);

      msg[16]=millis()&0xff; // prevent issues of same payload being ignored

      radio.stopListening();   // Ready to write EN_RXADDR_P0 = 1
      // Using the broadcast
      radio.setAutoAck(0,false);// BIND is sent as a broadcast first ...

      radio.openWritingPipe(gPipes[pipe].txaddr); // Who I'm sending it to

      gPipes[pipe].waitTime=millis();

      retCode = radio.write(msg,32); // Want to get AA working here

      radio.startListening(); // EN_RXADDR_P0 = 0


   return retCode;
}


int WnrfDriver::nrf_bind(tDevId devId, uint8_t reason, void * context) {
   // Scan pipe list to see if a pipe is available
Serial.print("DEBUG BIND:");
Serial.print(devId,HEX);
Serial.print(" reason:");
Serial.print(reason);

   int pipe = storeContext(context);

   if (pipe == -1) {
      // Rejecting request - no Pipes available
      return -1;
   }

   gbeacon_active = false; // Timeout handler can re-enable

   gPipes[pipe].state = NRF_CTL_W4_BIND_ACK;
   gPipes[pipe].txaddr = devId;
   gPipes[pipe].bind_reason = reason;

   // Send the NRF BIND request
   tx_bind(pipe);

   //   Start Timer
   gPipes[pipe].waitTime = millis();
   gPipes[pipe].waitCount = 0;

   return pipe;
}

bool WnrfDriver::tx_newid(uint8_t pipe, tDevId newid) {
   tPipeInfo * pid = &(gPipes[pipe]);
   byte tempPacket[32];
   bool retCode = false;

   Serial.print("Sending NEWID: ");
   Serial.print(newid,HEX);
   Serial.print(" to (");
   Serial.print(pipe);
   Serial.println(")");

   radio.stopListening();
   radio.openWritingPipe(pid->txaddr);
   radio.setAutoAck(0,true);// P2P uses AA on the receiver
   tempPacket[0] = 0x03;
   tempPacket[1] = newid&0xFF;
   tempPacket[2] = newid>>8&0xFF;
   tempPacket[3] = newid>>16&0xFF;
   retCode = radio.write(tempPacket,32);
   radio.setAutoAck(0,false);
   radio.startListening();
   return retCode;
}

bool WnrfDriver::sendGenericCmd(uint8_t pipe, uint8_t cmd, uint16_t value) {
   tPipeInfo * pid = &(gPipes[pipe]);
   byte tempPacket[32];
   bool retCode = false;

   Serial.print("Sending CMD: ");
   Serial.print(cmd);
   Serial.print(" to (");
   Serial.print(pipe);
   Serial.println(")");

   radio.stopListening();
   radio.openWritingPipe(pid->txaddr);
   radio.setAutoAck(0,true);// P2P uses AA on the receiver
   tempPacket[0] = cmd;
   tempPacket[1] = value&0xFF;
   tempPacket[2] = value>>8;
   retCode = radio.write(tempPacket,32);
   radio.setAutoAck(0,false);
   radio.startListening();
   return retCode;
}

int WnrfDriver::nrf_devid_update(tDevId devId, tDevId newid, void * context) {
  int retCode = -1;
  byte msg[32];

  // Add some chanId validation?


      Serial.print("MTC CMD: PROG DEVICE: ");
      Serial.print(devId,HEX);
      Serial.print(" to ");
      Serial.println(newid,HEX);

      // Send the BIND and enter wait for BIND timeout
      int pipe = nrf_bind(devId, BIND_DEVID, context);
      if (pipe>=0) {
         tPipeInfo * pid = &(gPipes[pipe]);
         pid->newid = newid;
         retCode =0 ;
      }

  return retCode;
}

int WnrfDriver::nrf_rfchan_update(tDevId devId, uint8_t chanId, void * context) {
  int retCode = -1;

  // Add some chanId validation?

      // Send the BIND and enter wait for BIND timeout
      int pipe = nrf_bind(devId, BIND_RFCHAN, context);
      if (pipe>=0) {
         gPipes[pipe].rf_chan = chanId;
         retCode = 0;
      } else {
        retCode = -1;
      }
  return retCode;
}

int WnrfDriver::nrf_startaddr_update(tDevId devId, uint16_t start, void * context) {
  int retCode = -1;
  // Check we can access the file?
  if (start<=512){
      // Send the BIND and enter wait for BIND timeout
      int pipe = nrf_bind(devId, BIND_START, context);
      if (pipe>=0) {
         gPipes[pipe].e131_start = start-1; // NOTE: Channel 1-512, stored as 0-511

        retCode = 0;
      } else {
        retCode = -1;
      }
  } else {
     retCode = -2; // Replace MAGIC numbers with ERROR codes that can be searched
  }

  return retCode;
}


int WnrfDriver::nrf_flash(tDevId devId, char *fname, void * context) {
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
         gPipes[pipe].fw.offset = 0; // Start of File
         gPipes[pipe].fw.start = 0;
         gPipes[pipe].fw.size = 0;
         gPipes[pipe].fw.csum = 0;

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

          Serial.print("Rx Nrf(");
          Serial.print(pipe);
          Serial.print(") ");
          Serial.print(payload[0],HEX);
          Serial.print(", ");
          Serial.println(payload[1]);

          pipe -=2;  //Index into gPipes array

          tPipeInfo * pid = &(gPipes[pipe]);
          boolean check_beacon = false;

          // Deal with it
          switch (pid->state) {
             case NRF_CTL_W4_BIND_ACK:
               if (payload[0] == 0x87) {
                 if (payload[1] == LLM_ACK) {
                    rx_ackbind(pipe); 
                 } else {
                    Serial.println("BIND failed");
                 }
               }
               break;
             case NRF_CTL_W4_SETUP_ACK:
               if (payload[0] == 0x80) {
                 if (payload[1] == LLM_ACK) {
                    rx_acksetup(pipe);
                 } else {
                    Serial.println("Setup failed");
                    tx_setup(pipe, true);
                 }
               }
               break;
             case NRF_CTL_W4_WRITE_ACK:
               if (payload[0] == 0x81) {
                 if (payload[1] == LLM_ACK) {
                   rx_ackwrite(pipe);
                 } else {
                   Serial.println("WRITE failed");
                   tx_write(pipe);
                 }
               }
               break;
             case NRF_CTL_W4_COMMIT_ACK:
               if (payload[0] == 0x82) {
                 if (payload[1] == LLM_ACK) {
                   rx_ackcommit(pipe);
                 } else {
                   Serial.println("Commit failed");
                   tx_commit(pipe);
                 }
               }
               break;
             case NRF_CTL_W4_AUDIT_ACK:
               if (payload[0] == 0x83) {
                 rx_ackaudit(pipe,(bool) payload[1]);
               }
               check_beacon = true;
               break;
             case NRF_CTL_W4_CHAN_ACK:
               if (payload[0] == 0x01) {
                  // To Do .. add some error handling here..
                  Serial.print("Receive CHAN_ACK : (");
                  Serial.print(pipe+2);
                  Serial.print("):");
                  Serial.println(payload[1]);

                  if (nrf_async_startaddr)
                    nrf_async_startaddr(pid->txaddr,pid->context, payload[1]);
                  pid->state = NRF_CTL_NONE;
                  pid->context = NULL;
                  check_beacon = true;
               } else {
                  Serial.print("Unexpected Rx message: (");
                  Serial.print(pipe+2);
                  Serial.print(") ");
                  for (int i=0;i<8;i++) {
                    char hex[3];
                    sprintf(hex,"%2.2x",payload[i]);
                    Serial.print(hex);
                  }
               }
               break;
             case NRF_CTL_W4_DEVID_ACK:
               if (payload[0] == 0x03) {
                  Serial.print("Receive DEVID_ACK: (");
                  Serial.print(pipe+2);
                  Serial.print("):");
                  Serial.println(payload[1]);

                  if (nrf_async_devid)
                    nrf_async_devid(pid->txaddr,pid->context, payload[1]);
                  pid->state = NRF_CTL_NONE;
                  pid->context = NULL;
                  check_beacon = true;
               } else {
                  Serial.print("Unexpected Rx message: (");
                  Serial.print(pipe+2);
                  Serial.print(") ");
                  for (int i=0;i<8;i++) {
                    char hex[3];
                    sprintf(hex,"%2.2x",payload[i]);
                    Serial.print(hex);
                  }
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
               check_beacon = true;
               break;
          } // End Switch STATE

          if (check_beacon) {
 	    if (gadmin==true) { // In Admin and at least 1 pipe available
               gbeacon_active = true;
            }
           }
       } // Pipe 2-5
    } // End handling of radio packet

    // -- Check for Timeouts
    uint32_t now = millis();
    uint8_t  num_waiting = MAX_P2P_PIPES;
    for (int i=0;i<MAX_P2P_PIPES;i++) {
       tPipeInfo * pid = &gPipes[i];
       if (pid->state != NRF_CTL_NONE) {
          num_waiting--;
          if (now - pid->waitTime > 1000) {
             if (pid->waitCount>10) {
                // >10 second failure to ACK - drop the attempt
                switch(pid->bind_reason) {
                   case BIND_FLASH:
                      Serial.println("TIMEOUT waiting for ACK");
                      nrf_async_otaflash(pid->txaddr,pid->context, -1);
                      break;

                   case BIND_DEVID:
                      Serial.println("TIMEOUT waiting for DEVICE ID ACK");
                      nrf_async_devid(pid->txaddr,pid->context, -1);
                      break;

                   case BIND_START:
                      Serial.println("TIMEOUT waiting for START ADDRESS ACK");
                      // Attempt to recover device - tell it to reset using P2P
                      // Hail Mary as PIPE needs to match - but as we aren't using
                      // concurrent PIPES yet.. this should work
                      tx_reset(i);
                      nrf_async_startaddr(pid->txaddr,pid->context, -1);
                      break;

                   case BIND_RFCHAN:
                      Serial.println("TIMEOUT waiting for START ADDRES ACK");
                      nrf_async_rfchan(pid->txaddr,pid->context, -1);
                      break;

                   case BIND_NONE:
                   default:
                      Serial.println("Race condition, nothing to worry about");
                      break;
                }
                pid->state = NRF_CTL_NONE;
                pid->context = NULL;
 		if (gadmin==true) { // In Admin and at least 1 pipe available
                   gbeacon_active = true;
                }
             } else {
                pid->waitTime = now;
                pid->waitCount++;
                switch(pid->state) {
                   case NRF_CTL_W4_BIND_ACK:
                      Serial.print("Re-bind request::");
                      tx_bind(i);
                      break;
                   case NRF_CTL_W4_SETUP_ACK:
                      Serial.print("Re-Setup request::");
                      tx_setup(i,true);
                      break;
                   case NRF_CTL_W4_WRITE_ACK:
                      Serial.println("Re-Write request");
                      tx_write(i);
                      break;
                   case NRF_CTL_W4_COMMIT_ACK:
                      Serial.println("Re-Commit request");
                      tx_commit(i);
                      break;
                   case NRF_CTL_W4_AUDIT_ACK:
                      Serial.println("Re-Audit request");
                      tx_audit(i);
                      break;
                   case NRF_CTL_W4_CHAN_ACK:
                      Serial.println("Re-send set-chan request");
                      sendGenericCmd(i, 0x01 /* cmd */, pid->e131_start);
                      break;
                    case NRF_CTL_W4_DEVID_ACK:
                      Serial.println("Re-send DEVID request");
                      tx_newid(i,pid->newid);
                      break;
                   default:
                      Serial.println("Nothing Pending Timeout");
                      break;
                 } // case
              } // else not 10s yet
          } // 1s timeout
       } // in W4 ack state
    } // for each pipe

    // Timeout handler can re-enable
    if (num_waiting==0) {
       // Turn off beacon if no P2P channels available
       gbeacon_active = false;
    }
    //  If in ADMIN mode - Timeouts
    sendBeacon();
    sendDeviceList();
}
