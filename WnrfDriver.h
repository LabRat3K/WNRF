/*
* WnrfDriver.h - E1.31 to NRF24L01 bridge
*
* Based on: ESPixelStick by Shelby Merrick
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
*/

#ifndef WNRFDRIVER_H_
#define WNRFDRIVER_H_

enum class NrfBaud : uint8_t {
    BAUD_1Mbps,
    BAUD_2Mbps
};

/* Note: Selected ranges support multiple 2Mbps channels, 
   while not interfering with standard WIFI.  */

enum class NrfChan : uint8_t {
    NRFCHAN_LEGACY,  /* 80 */
    NRFCHAN_A,       /* 70..71 */
    NRFCHAN_B,       /* 72..73 */
    NRFCHAN_C,       /* 74..73 */
    NRFCHAN_D,       /* 76..73 */
    NRFCHAN_E,       /* 78..73 */
    NRFCHAN_F,       /* 80..81 */
    NRFCHAN_G        /* 82..83 */
};

typedef struct sDevId {
   unsigned char id[3];
} tDevId;


typedef struct sDeviceInfo {
  tDevId   dev_id; //device_id
  uint8_t  type;   //device_type;
  uint8_t  blv;    //bootloader_version;
  // Application specific 
  uint8_t  apm;    //Application magic_number; 
  uint8_t  apv;    //Application version;
  uint16_t start;  //E1.31 channel_start;
} tDeviceInfo;

class WnrfDriver {
 public:
    int begin(NrfBaud baud, NrfChan chanid,int size);
    int begin();
    void show();
    uint8_t* getData();
    uint8_t* getNrfHistogram();

    /* NRF Device Management */
    void sendBeacon();
    void checkRx();

    void sendNewDevId(tDevId  devId, tDevId  newId);
    void sendNewChan (tDevId  devId, uint8_t  chanId);
    void sendNewStart(tDevId  devId, uint16_t start);
    void printIt(void);
    void enableAdmin(void);
    void disableAdmin(void);
    int  getDeviceList(tDeviceInfo **devices);
    void clearDeviceList();

    /* Set channel value at address */
    inline void setValue(uint16_t address, uint8_t value) {
        if (gnum_channels == 32) {
	   if (address<32) _dmxdata[address] = value;
        } else {
           _dmxdata[1+((address/31)<<5)+(address%31)] = value;
        }
    }

    inline bool canRefresh() {
        if (gnum_channels == 32) {
            return (millis() - gstart_time) >= 22;
        } else {
            return (micros() - gstart_time) >= 665; //Practical vs Theoretical 1336;
        }
    }

 private:
    // Global Variables
    uint32_t    gstart_time;    // When the last frame TX started
    uint16_t	gnum_channels;  // Amount of DMX data to transmit
    uint8_t	gnext_packet;   // Packet index for next frame

    // Some ADMIN timeout values
    uint32_t	gbeacon_timeout;
    uint32_t	gbeacon_bind_timeout;
    uint32_t	gbeacon_client_response_timeout;
 
    uint8_t*    _dmxdata;       // Full Universe 

    uint8_t     gled_count;      // For LED based feedback
    uint8_t     gled_state;      // Blink approx 1 per second
    bool        gadmin; 

    tDeviceInfo gdevice_list[10]; 
    uint8_t     gdevice_count;

    // Global Config
    NrfBaud  conf_baudrate;
    NrfChan  conf_chanid;

    // Functions
    void setBaud(NrfBaud baud);
    void setChan(NrfChan chanid);
    void sendGenericCmd(tDevId devId, uint8_t cmd, uint16_t value);
    void parseNrf_x88(uint8_t *data);
};

#endif /* WNRFDIVER_H_ */
