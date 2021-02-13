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
    NRFCHAN_A,       /* 101..102 */
    NRFCHAN_B,       /* 103..104 */
    NRFCHAN_C,       /* 105..106 */
    NRFCHAN_D,       /* 107..108 */
    NRFCHAN_E,       /* 109..110 */
    NRFCHAN_F,       /* 111..112 */
    NRFCHAN_G,       /* 113..114 */
    NRFCHAN_H,       /* 115..116 */
    NRFCHAN_I,       /* 117..118 */
};

class WnrfDriver {
 public:
    int begin(NrfBaud baud, NrfChan chanid,int size);
    int begin();
    void show();
    uint8_t* getData();


    /* Set channel value at address */
    inline void setValue(uint16_t address, uint8_t value) {
        if (num_channels == 32) {
	   if (address<32) _dmxdata[address] = value;
        } else {
           _dmxdata[1+((address/31)<<5)+(address%31)] = value;
        }
    }

    inline bool canRefresh() {
        if (num_channels == 32) {
            return (millis() - startTime) >= 22;
        } else {
            return (micros() - startTime) >= 865; //Practical vs Theoretical 1336;
        }
    }
 private:
    void        setBaud(NrfBaud baud);
    uint32_t    startTime;      // When the last frame TX started
    uint32_t    refreshTime;    // Time until we can refresh after starting a TX
    uint8_t*    _dmxdata;       // Full Universe 
    uint16_t	num_channels;   // Amount of DMX data to transmit
    uint8_t	next_packet;    // Packet index for next frame
 
    uint8_t     led_count;      // For LED based feedback
    uint8_t     led_state;      // Blink approx 1 per second
};

#endif /* WNRFDIVER_H_ */

