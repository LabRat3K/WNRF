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
//#define WEMOS_D1
#ifdef WEMOS_D1
   #define LED_NRF D5
#else
   // WNRF
   #define LED_NRF 15
#endif
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


typedef uint32_t tDevId;

uint32_t txt2id(const char* str);
void id2txt(char * str, uint32_t id);

typedef struct sPipeState {
  tDevId txaddr;   // Address of the target device bound to this pipe
  tDevId rxaddr;   // Pipe Address for the target device to send to
  uint8_t state;
  uint8_t bind_reason;
  union {
         struct {
            uint32_t offset;// File offset for seek/re-read attempts
            uint16_t start; // Start Address in the PIC
            uint32_t size;  // Number of bytes
            uint16_t csum;  // Checksum over the entire upload space
         } fw;
         uint16_t e131_start;
         tDevId newId;
         uint8_t  rf_chan;
        };
  // CallBack context to the UI session
  void *  context;
  // Timeout counter
  uint8_t  waitCount;
  uint32_t waitTime;
} tPipeInfo;

#define MAX_P2P_PIPES (4)
// Pipes 0&1 are reserved for Transmiting and Broadcast receipt

typedef struct sDeviceInfo {
  tDevId   dev_id; //device_id
  uint8_t  type;   //device_type;
  uint8_t  blv;    //bootloader_version;
  // Application specific
  uint8_t  apm;    //Application magic_number;
  uint8_t  apv;    //Application version;
  uint16_t start;  //E1.31 channel_start;
} tDeviceInfo;

typedef void (* async_bind_handler)     (tDevId devId, void * context, int result);
typedef void (* async_flash_handler)    (tDevId devId, void * context, int result);
typedef void (* async_rfchan_handler)   (tDevId devId, void * context, int result);
typedef void (* async_devid_handler)    (tDevId devId, void * context, int result);
typedef void (* async_startaddr_handler)(tDevId devId, void * context, int result);

typedef void (* async_devlist_handler)  (tDeviceInfo * dev_ist, uint8_t count);

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

    void printIt(void);
    void enableAdmin(void);
    void disableAdmin(void);

    int  nrf_bind            (tDevId devId, uint8_t reason, void * context);
    int  nrf_flash           (tDevId devId, char *fname, void * context);
    int  nrf_rfchan_update   (tDevId devId, uint8_t chan,  void * context);
    int  nrf_devid_update    (tDevId devId, tDevId newId,void * context);
    int  nrf_startaddr_update(tDevId devId, uint16_t start, void * context);

    // Async Functions - callback context
    async_bind_handler      nrf_async_bind;
    async_flash_handler     nrf_async_otaflash;
    async_rfchan_handler    nrf_async_rfchan;
    async_devid_handler     nrf_async_devid;
    async_startaddr_handler nrf_async_startaddr;
    async_devlist_handler   nrf_async_devlist;

    void sendNewDevId (tDevId  devId, tDevId  newId);
    void sendNewRFChan(tDevId  devId, uint8_t  chanId);

    int  clearContext(void * context);

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

    tPipeInfo  gPipes[MAX_P2P_PIPES];

    // Some ADMIN timeout values
    uint32_t	gbeacon_timeout;
    uint32_t	gbeacon_bind_timeout;
    uint32_t	gbeacon_client_response_timeout;

    uint8_t*    _dmxdata;       // Full Universe

    uint8_t     gled_count;      // For LED based feedback
    uint8_t     gled_state;      // Blink approx 1 per second
    bool        gadmin;
    uint8_t     gctl_state;      // Tracking NRF client transaction

    tDeviceInfo gdevice_list[10];
    uint8_t     gdevice_count;
    bool	gbeacon_active;

    // Global Config
    NrfBaud  conf_baudrate;
    NrfChan  conf_chanid;

    // Functions
    void setBaud(NrfBaud baud);
    void setChan(NrfChan chanid);
    bool sendGenericCmd(uint8_t pipe, uint8_t cmd, uint16_t value);
    void parseNrf_x88(uint8_t *data);

    int  storeContext(void * context);
    int  getContext(uint8_t pipeid, void **context);

    bool tx_bind(uint8_t pipe);
    bool tx_setup(uint8_t pipe, bool resend);
    bool tx_write(uint8_t pipe);
    bool tx_commit(uint8_t pipe);
    bool tx_audit(uint8_t pipe);
    bool tx_reset(uint8_t pipe);

    void rx_ackbind(uint8_t pipe);
    void rx_acksetup(uint8_t pipe);
    void rx_ackwrite(uint8_t pipe);
    void rx_ackcommit(uint8_t pipe);
    void rx_ackaudit(uint8_t pipe, char result);

    void openBindPipe(uint8_t);
    void sendDeviceList(void);

};

#endif /* WNRFDIVER_H_ */
