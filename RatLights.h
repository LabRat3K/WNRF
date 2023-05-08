#ifndef _RATLIGHTS_
#define _RATLIGHTS_


#define CONFIG_VERSION (2)

#define PCB_NONE      0x00
#define PCB_NRF_3CHAN 0x01
#define PCB_NRF2DMX   0x02
#define PCB_LUXEONRGB 0x03    // PCB EAGLE files are missing
#define PCB_12CHAN    0x04
#define PCB_UNO       0x05
#define PCB_RFCOLOR   0x06

#define DEVICE_PROCESSOR_16F1823 0x00
#define DEVICE_PROCESSOR_16F1825 0x01
#define DEVICE_PROCESSOR_16F722  0x02
#define DEVICE_PROCESSOR_ATMEGA328P 0x80

// Capability map - which functions each FW image supports
#define ADMIN_CAP_OTA    0x01
#define ADMIN_CAP_DEVID  0x02
#define ADMIN_CAP_START  0x04
#define ADMIN_CAP_BAUD   0x08
#define ADMIN_CAP_RFCHAN 0x10
#define ADMIN_CAP_LEDMAP 0x20

#define RFRATE_250K   0x00
#define RFRATE_1MB    0x01
#define RFRATE_2MB    0x02


typedef struct sConfigBlock {  //msg offset[] (add 1 for 0x88 leading byte)
  // Bootloader & OTA  Config
  // ------------------------
      uint8_t config_version;  //[0]
      uint8_t deviceId[3];     //[1..3] R/W in FACTORY image
      // Hardware Info 8-bytes (non-mutable)
      uint8_t pcb_type;        //[4]
      uint8_t pcb_version;     //[5]
      uint8_t processor;       //[6]
      uint8_t num_channels[2]; //[7-8]
      // BootLoader Info - 6 bytes
      uint8_t bl_version;      //[9]
      uint8_t bl_appmagic;     //[10] 0x00 = NOT BL CAPABLE - MAGIC No for image compatability

  // Client Application Config
  // -------------------------
      // Application Info - 14 bytes (app_size & csum are not displayed)
      uint8_t app_size[2];     //[11-12] Configured based on upload image
      uint8_t app_csum[2];     //[13-14]

      uint8_t app_version;     //[15] Swapped to ensure start_chan is on an even boundary
      uint8_t start_chan[2];   //[16-17] Configured as part of client ADMIN
      uint8_t app_rfchan;      //[18] Client image
      uint8_t app_rfrate;      //[19] Client image
      uint8_t admin_cap;       //[20] Admin capabilies bitmap 
      uint8_t spare;
   // Reserved for future
   // -------------------
      uint8_t reserved[8];     //[21-29] Prevent need for new BL with app changes in the future 
} tConfigMessage;

#endif
