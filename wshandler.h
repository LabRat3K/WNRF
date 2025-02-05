/*
* wshandler.h
*
* Project: ESPixelStick - An ESP8266 and E1.31 based pixel driver
* Copyright (c) 2016 Shelby Merrick
* http://www.forkineye.com
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

#ifndef WSHANDLER_H_
#define WSHANDLER_H_

#if defined(ESPS_MODE_WNRF)
#include "WnrfDriver.h"
extern WnrfDriver out_driver;       // Wnrf object
#endif

extern EffectEngine effects;    // EffectEngine for test modes
extern char fw_name[40];
extern ESPAsyncE131 e131;       // ESPAsyncE131 with X buffers
extern ESPAsyncDDP  ddp;        // ESPAsyncDDP with X buffers
extern config_t     config;     // Current configuration
extern uint32_t     *seqError;  // Sequence error tracking for each universe
extern uint16_t     uniLast;    // Last Universe to listen for
extern bool         reboot;     // Reboot flag

extern const char CONFIG_FILE[];

#if defined(ESPS_MODE_WNRF)
#define MAX_WS 5
AsyncWebSocketClient * connections[MAX_WS]={NULL,NULL,NULL,NULL,NULL};
AsyncWebSocketClient * ws_edit_client;
#endif

// Forward Declaration - code clean-up to fix
  void cb_flash (tDevId id, void * context, int result);
  void cb_startaddr(tDevId id, void * context, int result);
/*
  Packet Commands
    E1 - Get Elements

    G1 - Get Config
    G2 - Get Config Status
    G3 - Get Current Effect and Effect Config Options

    T0 - Disable Testing
    T1 - Static Testing
    T2 - Blink Test
    T3 - Flash Test
    T4 - Chase Test
    T5 - Rainbow Test
    T6 - Fire flicker
    T7 - Lightning
    T8 - Breathe

    V1 - View Stream

    NRF Device Editing/Auditing
    D1 - List of NRF client devices
    D2 - Update Channel Request
    D3 - WS File Upload Return Code
    Da/A - enable.disable Device Admin

    S1 - Set Network Config
    S2 - Set Device Config
    S3 - Set Effect Startup Config

    XJ - Get RSSI,heap,uptime, e131 stats in json

    X6 - Reboot
*/

EFUpdate efupdate;
uint8_t * WSframetemp;
uint8_t * confuploadtemp;

void procX(uint8_t *data, AsyncWebSocketClient *client) {
    switch (data[1]) {
        case 'J': {

            DynamicJsonDocument json(1024);

            // system statistics
            JsonObject system = json.createNestedObject("system");
            system["rssi"] = (String)WiFi.RSSI();
            system["freeheap"] = (String)ESP.getFreeHeap();
            system["uptime"] = (String)millis();

            // E131 statistics
            JsonObject e131J = json.createNestedObject("e131");
            uint32_t seqErrors = 0;
            for (int i = 0; i < ((uniLast + 1) - config.universe); i++)
                seqErrors =+ seqError[i];

            e131J["universe"] = (String)config.universe;
            e131J["uniLast"] = (String)uniLast;
            e131J["num_packets"] = (String)e131.stats.num_packets;
            e131J["seq_errors"] = (String)seqErrors;
            e131J["packet_errors"] = (String)e131.stats.packet_errors;
            e131J["last_clientIP"] = e131.stats.last_clientIP.toString();

            JsonObject ddpJ = json.createNestedObject("ddp");
            ddpJ["num_packets"] = (String)ddp.stats.packetsReceived;
            ddpJ["seq_errors"] = (String)ddp.stats.errors;
            ddpJ["num_bytes"] = (String)ddp.stats.bytesReceived;
            ddpJ["max_channel"] = (String)ddp.stats.ddpMaxChannel;
            ddpJ["min_channel"] = (String)ddp.stats.ddpMinChannel;

            // WNRF stats
            JsonObject nrf = json.createNestedObject("nrf");
            if (config.nrf_chan==NrfChan::NRFCHAN_LEGACY) {
               //nrf["chan"] = static_cast<uint8_t>(config.nrf_chan);
               nrf["chan"] = "2.480 Mhz";
            } else {
               uint8_t tempChan = 70+((static_cast<uint8_t>(config.nrf_chan)-1)*2);
               nrf["chan"] = "2.4"+String(tempChan)+" Mhz";
            }
            if  (config.nrf_baud == NrfBaud::BAUD_2Mbps) {
               nrf["baud"] = "2 Mbps";
            } else {
               nrf["baud"] = "1 Mbps";
            }
            if (ws_edit_client == NULL) {
               nrf["mode"] = "BROADCAST";
            } else {
               nrf["mode"] = "ADMIN";
            }
 //
 // To Do: add count after we start probing devices
 //

            String response;
            serializeJson(json, response);
            client->text("XJ" + response);
            break;
        }

        case '6':  // Init 6 baby, reboot!
            reboot = true;
    }
}

void procE(uint8_t *data, AsyncWebSocketClient *client) {
    switch (data[1]) {
        case '1':
            // Create buffer and root object
            DynamicJsonDocument json(1024);

#if defined (ESPS_MODE_WNRF)
            JsonObject nrf_baud = json.createNestedObject("nrf_baud");
            nrf_baud["1 Mbps"] = static_cast<uint8_t>(NrfBaud::BAUD_1Mbps);
            nrf_baud["2 Mbps"] = static_cast<uint8_t>(NrfBaud::BAUD_2Mbps);

            JsonObject nrf_chan = json.createNestedObject("nrf_chan");
            nrf_chan["Legacy (2480)"] = static_cast<uint8_t>(NrfChan::NRFCHAN_LEGACY);
            nrf_chan["2470"] = static_cast<uint8_t>(NrfChan::NRFCHAN_A);
            nrf_chan["2472"] = static_cast<uint8_t>(NrfChan::NRFCHAN_B);
            nrf_chan["2474"] = static_cast<uint8_t>(NrfChan::NRFCHAN_C);
            nrf_chan["2476"] = static_cast<uint8_t>(NrfChan::NRFCHAN_D);
            nrf_chan["2478"] = static_cast<uint8_t>(NrfChan::NRFCHAN_E);
            nrf_chan["2480"] = static_cast<uint8_t>(NrfChan::NRFCHAN_F);
            nrf_chan["2482"] = static_cast<uint8_t>(NrfChan::NRFCHAN_G);

#endif

            String response;
            serializeJson(json, response);
            client->text("E1" + response);
            break;
    }
}

#ifdef ESPS_MODE_WNRF

// Helper Function to broadcast to all open
// ws connections
void broadcast(String message) {
   for (int i=0;i<MAX_WS;i++){
      if (connections[i]) {
         connections[i]->text(message);
      }
   }// for each connection
}


//
// 'D1' = Device List Push to client
//
void cb_devlist(tDeviceInfo * dev_list, uint8_t count) {
   // Count is the number of rows in the dev_list table
   // Dont' want to generate the JSON more than once.. so should we be
   // maintaining the context list here vs in the WnrfDriver??
   if (count) {
      DynamicJsonDocument json(1024);
      JsonObject devList = json.createNestedObject("deviceList");
      char tempid[8];

      while (count--) {
         tDeviceInfo *dev = &(dev_list[count]);

         id2txt(tempid, dev->dev_id);
         // Convert 3-byte address into a HEX string
         /*char *bytes = (char *)&(dev->dev_id);
         sprintf(tempid,"%2.2X%2.2X%2.2X",bytes[2],bytes[1],bytes[0]); */
         JsonObject device = devList.createNestedObject(tempid);
            device["dev_id"]= tempid;    // Device Id
            device["type"]  = dev->type; // Device Type
            device["blv"]   = dev->blv;  // Boot Loader Version
            device["apm"]   = dev->apm;  // App Magic Number
            device["apv"]   = dev->apv;  // Boot Loader Version
            device["start"] = dev->start;// E1.31 start Address
      } // While

      // Prepare JSON for transmission
      String message;
      serializeJson(devList, message);

      // Broadcast this to all web clients devices
      broadcast("D1"+message);
   } // if result>0
}


void sendDeviceStatus(AsyncWebSocketClient *client) {
   DynamicJsonDocument json(1024);
   JsonObject status = json.createNestedObject("status");
       status["admin"] = (ws_edit_client!=NULL);

   String message;
   serializeJson(status, message);

   broadcast("DS"+message);
}

void sendEditResponse(char cmd, bool result, AsyncWebSocketClient *client ) {
   DynamicJsonDocument json(1024);
   JsonObject admin = json.createNestedObject("admin");
       admin["result"] = result;

   String message;
   serializeJson(admin, message);
   if (cmd=='a')  {
      client->text("Da"+message);
   } else {
      client->text("DA"+message);
   }
}


void cb_upload_reply (uint16_t retcode, char * fname) {
   DynamicJsonDocument json(1024);
   JsonObject wnrfu = json.createNestedObject("wnrfu");
       wnrfu["retcode"] = retcode;
       wnrfu["nrf_fw"] = fname;

   String message;
   serializeJson(wnrfu, message);

   broadcast("D3"+message);
}

// Device Admin handler
void procD(uint8_t *data, AsyncWebSocketClient *client) {
    DynamicJsonDocument json(1024);
    DeserializationError error = deserializeJson(json, reinterpret_cast<char*>(data + 2));
    JsonObject params = json.as<JsonObject>();

    switch (data[1]) {
        case 'A':
           // Highlander: There can be only ONE ADMIN control UI
           if (ws_edit_client==NULL) {
              out_driver.enableAdmin();
              ws_edit_client = client;
           }
// Debugging - allow to gain control
           ws_edit_client = client;
           sendEditResponse(data[1],(ws_edit_client==client),client);
           break;  // Turn off the Streaming Output
        case 'a':
           // Only the ADMIN control UI can CANCEL it's own session
           if (ws_edit_client==client) {
              // This is permitted
              out_driver.disableAdmin();
              ws_edit_client = NULL;
              sendEditResponse(data[1],true,client);
           } else {
              sendEditResponse(data[1],false,client);
           }
           break; // Turn on the Streaming Output
        case '1':
            LOG_PORT.println(F("(D1) Device Refresh Request**"));
            break;
        case '2': {
               LOG_PORT.println(F("(D2) CHANNEL update request**"));
               tDevId tempid=0;
               int retcode = 0;

               if (ws_edit_client == client) {
                  if (params.containsKey("devid")) {
                     tempid = txt2id(params["devid"].as<const char*>());
		     uint16_t newChan =   params["chan"];
                     int retcode = out_driver.nrf_startaddr_update(tempid, newChan, client);
                     if (retcode) {
                        cb_startaddr(tempid, client, retcode);
                     }
                  } else {
                    LOG_PORT.println(F("(D2) No DEVID in request"));
                  }
                } else {
                    LOG_PORT.println(F("(D2) Invalid EDIT OWNER"));
                }
            }
            break;
        case '4': {
               tDevId tempid=0;

               if (ws_edit_client==client) {
                  if (params.containsKey("devid")) {
                     // Parse the string to a device id
                     tempid = txt2id(params["devid"].as<const char*>());
                     int retcode = out_driver.nrf_flash(tempid, fw_name, client);

                     if (retcode)
                        cb_flash(tempid, client, retcode);

                  } else {
                     cb_flash(tempid,client, -21);
                  }
               } else {
                  cb_flash(tempid,client, -11);
               }
            }
            break;
        default : // Do Nothing
            break;
    }
}
#endif

void procG(uint8_t *data, AsyncWebSocketClient *client) {
    switch (data[1]) {
        case '1': {
            String response;
            serializeConfig(response, false, true);
            client->text("G1" + response);
            break;
        }

        case '2': {
            // Create buffer and root object
            DynamicJsonDocument json(1024);

            json["ssid"] = (String)WiFi.SSID();
            if (WiFi.hostname().isEmpty()){
               json["hostname"] = config.hostname.c_str();
            } else {
               json["hostname"] = (String)WiFi.hostname();
            }
            json["ip"] = WiFi.localIP().toString();
            json["mac"] = WiFi.macAddress();
            json["version"] = (String)VERSION;
            json["built"] = (String)BUILD_DATE;
            json["flashchipid"] = String(ESP.getFlashChipId(), HEX);
            json["usedflashsize"] = (String)ESP.getFlashChipSize();
            json["realflashsize"] = (String)ESP.getFlashChipRealSize();
            json["freeheap"] = (String)ESP.getFreeHeap();

            String response;
            serializeJson(json, response);
            client->text("G2" + response);
            break;
        }

        case '3': {
            String response;
            DynamicJsonDocument json(2048);

// dump the current running effect options
            JsonObject effect = json.createNestedObject("currentEffect");
            if (config.ds == DataSource::E131) {
                effect["name"] = "Disabled";
            } else {
                effect["name"] = (String)effects.getEffect() ? effects.getEffect() : "";
            }
            effect["brightness"] = effects.getBrightness();
            effect["speed"] = effects.getSpeed();
            effect["r"] = effects.getColor().r;
            effect["g"] = effects.getColor().g;
            effect["b"] = effects.getColor().b;
            effect["reverse"] = effects.getReverse();
            effect["mirror"] = effects.getMirror();
            effect["allleds"] = effects.getAllLeds();
            effect["startenabled"] = config.effect_startenabled;
            effect["idleenabled"] = config.effect_idleenabled;
            effect["idletimeout"] = config.effect_idletimeout;


// dump all the known effect and options
            JsonObject effectList = json.createNestedObject("effectList");
            for(int i=0; i < effects.getEffectCount(); i++){
                JsonObject effect = effectList.createNestedObject( effects.getEffectInfo(i)->htmlid );
                effect["name"] = effects.getEffectInfo(i)->name;
                effect["htmlid"] = effects.getEffectInfo(i)->htmlid;
                effect["hasColor"] = effects.getEffectInfo(i)->hasColor;
                effect["hasMirror"] = effects.getEffectInfo(i)->hasMirror;
                effect["hasReverse"] = effects.getEffectInfo(i)->hasReverse;
                effect["hasAllLeds"] = effects.getEffectInfo(i)->hasAllLeds;
                effect["wsTCode"] = effects.getEffectInfo(i)->wsTCode;
            }

            serializeJson(json, response);
            client->text("G3" + response);
//LOG_PORT.print(response);
            break;
        }
    }
}

void procS(uint8_t *data, AsyncWebSocketClient *client) {

    DynamicJsonDocument json(1024);
    DeserializationError error = deserializeJson(json, reinterpret_cast<char*>(data + 2));

    if (error) {
        LOG_PORT.println(F("*** procS(): Parse Error ***"));
        LOG_PORT.println(reinterpret_cast<char*>(data));
        return;
    }

    bool reboot = false;
    switch (data[1]) {
        case '1':   // Set Network Config
            dsNetworkConfig(json.as<JsonObject>());
            saveConfig();
            client->text("S1");
            break;
        case '2':   // Set Device Config
#ifdef MQTT
            // Reboot if MQTT changed
            if (config.mqtt != json["mqtt"]["enabled"])
                reboot = true;
#endif

            dsDeviceConfig(json.as<JsonObject>());
            saveConfig();

            if (reboot)
                client->text("S1");
            else
                client->text("S2");
            break;
        case '3':   // Set Effect Startup Config
            dsEffectConfig(json.as<JsonObject>());
            saveConfig();
            client->text("S3");
            break;
    }
}

void procT(uint8_t *data, AsyncWebSocketClient *client) {

    if (data[1] == '0') {
            //TODO: Store previous data source when effect is selected so we can switch back to it
            config.ds = DataSource::E131;
            effects.clearAll();
    }
    else if ( (data[1] >= '1') && (data[1] <= '8') ) {
        String TCode;
        TCode += (char)data[0];
        TCode += (char)data[1];
        const EffectDesc* effectInfo = effects.getEffectInfo(TCode);

        if (effectInfo) {

            DynamicJsonDocument j(1024);
            DeserializationError error = deserializeJson(j, reinterpret_cast<char*>(data + 2));

            // weird ... no error handling on json parsing

            JsonObject json = j.as<JsonObject>();

            config.ds = DataSource::WEB;
            effects.setEffect( effectInfo->name );

            if ( effectInfo->hasColor ) {
                if (json.containsKey("r") && json.containsKey("g") && json.containsKey("b")) {
                    effects.setColor({json["r"], json["g"], json["b"]});
                }
            }
            if ( effectInfo->hasMirror ) {
                if (json.containsKey("mirror")) {
                    effects.setMirror(json["mirror"]);
                }
            }
            if ( effectInfo->hasReverse ) {
                if (json.containsKey("reverse")) {
                    effects.setReverse(json["reverse"]);
                }
            }
            if ( effectInfo->hasAllLeds ) {
                if (json.containsKey("allleds")) {
                    effects.setAllLeds(json["allleds"]);
                }
            }
            if (json.containsKey("speed")) {
                effects.setSpeed(json["speed"]);
            }
            if (json.containsKey("brightness")) {
                effects.setBrightness(json["brightness"]);
            }
        }
    }

#ifdef MQTT
    if (config.mqtt)
        publishState();
#endif
}

void procV(uint8_t *data, AsyncWebSocketClient *client) {
    switch (data[1]) {
        case '1': {  // View stream
#if defined(ESPS_MODE_WNRF)
            client->binary(out_driver.getData(), config.channel_count);
#endif
            break;
           }
#if defined(ESPS_MODE_WNRF)
	case '2': { // View Frequency Histogram
            client->binary(out_driver.getNrfHistogram(),84);
           }
#endif
    }
}

void handle_fw_upload(AsyncWebServerRequest *request, String filename,
        size_t index, uint8_t *data, size_t len, bool final) {
    if (!index) {
        WiFiUDP::stopAll();
        LOG_PORT.print(F("* Upload Started: "));
        LOG_PORT.println(filename.c_str());
        efupdate.begin();
    }

    if (!efupdate.process(data, len)) {
        LOG_PORT.print(F("*** UPDATE ERROR: "));
        LOG_PORT.println(String(efupdate.getError()));
    }

    if (efupdate.hasError())
        request->send(200, "text/plain", "Update Error: " +
                String(efupdate.getError()));

    if (final) {
        LOG_PORT.println(F("* Upload Finished."));
        efupdate.end();
        SPIFFS.begin();
        saveConfig();
        reboot = true;
    }
}

void handle_config_upload(AsyncWebServerRequest *request, String filename,
        size_t index, uint8_t *data, size_t len, bool final) {
    static File file;
    if (!index) {
        WiFiUDP::stopAll();
        LOG_PORT.print(F("* Config Upload Started: "));
        LOG_PORT.println(filename.c_str());

        if (confuploadtemp) {
          free (confuploadtemp);
          confuploadtemp = nullptr;
        }
        confuploadtemp = (uint8_t*) malloc(CONFIG_MAX_SIZE);
    }

    LOG_PORT.printf("index %d len %d\n", index, len);
    memcpy(confuploadtemp + index, data, len);
    confuploadtemp[index + len] = 0;

    if (final) {
        int filesize = index+len;
        LOG_PORT.print(F("* Config Upload Finished:"));
        LOG_PORT.printf(" %d bytes", filesize);

        DynamicJsonDocument json(1024);
        DeserializationError error = deserializeJson(json, reinterpret_cast<char*>(confuploadtemp));

        if (error) {
            LOG_PORT.println(F("*** Parse Error ***"));
            LOG_PORT.println(reinterpret_cast<char*>(confuploadtemp));
            request->send(500, "text/plain", "Config Update Error." );
        } else {
            dsNetworkConfig(json.as<JsonObject>());
            dsDeviceConfig(json.as<JsonObject>());
            dsEffectConfig(json.as<JsonObject>());
            saveConfig();
            request->send(200, "text/plain", "Config Update Finished: " );
//          reboot = true;
        }

        if (confuploadtemp) {
            free (confuploadtemp);
            confuploadtemp = nullptr;
        }
    }
}


void wsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
        AwsEventType type, void * arg, uint8_t *data, size_t len) {
    switch (type) {
        case WS_EVT_DATA: {
            AwsFrameInfo *info = static_cast<AwsFrameInfo*>(arg);
            if (info->opcode == WS_TEXT) {
                switch (data[0]) {
                    case 'X':
                        procX(data, client);
                        break;
                    case 'E':
                        procE(data, client);
                        break;
                    case 'G':
                        procG(data, client);
                        break;
#ifdef ESPS_MODE_WNRF
                    case 'D':
                        procD(data, client);
                        break;
#endif
                    case 'S':
                        procS(data, client);
                        break;
                    case 'T':
                        procT(data, client);
                        break;
                    case 'V':
                        procV(data, client);
                        break;
                }
            } else {
                LOG_PORT.println(F("-- binary message --"));
            }
            break;
        }
        case WS_EVT_CONNECT: {
            int i;
              LOG_PORT.print(F("* WS Connect - "));
              LOG_PORT.println(client->id());
#if defined(ESPS_MODE_WNRF)
              for (i=0;i<MAX_WS;i++) {
                 if (connections[i] == NULL) {
                    connections[i] = client;
                    break;
                 }
              }
              if (i==MAX_WS)  {
                LOG_PORT.println("Maximum number of concurrent clients exceeded");
              } else {
                LOG_PORT.print("Using connection: ");
                LOG_PORT.println(i);
              }
#endif
            }
            break;
        case WS_EVT_DISCONNECT:{
            int i;
              LOG_PORT.print(F("* WS Disconnect - "));
              LOG_PORT.println(client->id());
#if defined(ESPS_MODE_WNRF)
 // LabRat - clearContext to be removed
              out_driver.clearContext((void *) client);

              for (i=0;i<MAX_WS;i++) {
                if (connections[i] == client) {
                   connections[i] = NULL;
                   break;
                }
              }
              if (ws_edit_client==client) {
                 // ADMIN EDITOR client has dropped
                 ws_edit_client = NULL;
                 out_driver.disableAdmin();
 // LabRat Add a PUSH to remaining clients
              }
#endif
            }
            break;
        case WS_EVT_PONG:
            LOG_PORT.println(F("* WS PONG *"));
            break;
        case WS_EVT_ERROR:
            LOG_PORT.println(F("** WS ERROR **"));
            break;
    }
}

#ifdef ESPS_MODE_WNRF
  // Async Callbacks from the WnrfDriver

  // response to OTA FLASH request
  void cb_flash (tDevId devid, void * context, int result) {
     char tempid[8];

     id2txt(tempid,devid);
     DynamicJsonDocument json(1024);
     JsonObject ota = json.createNestedObject("ota");
       ota["result"] = result;
       ota["dev_id"] = tempid;

     String message;
     serializeJson(ota, message);
     if (context) {
        ((AsyncWebSocketClient *) context)->text("D4" + message);
     }
  }

  // response to APP: Update RF channel request
  void cb_rfchan (tDevId id, void * context, int result) {
     // If result is ok..
     // Convert context into a client and send "D4" result
  }

  // response to MTC: Update DeviceId request
  void cb_devid (tDevId id, void * context, int result) {
     // If result is ok..
     // Convert context into a client and send "D5" result
  }

  // response to MTC: Update E1.31 Channel request
  void cb_startaddr(tDevId id, void * context, int result) {
     char tempid[8];

     id2txt(tempid,id);
     DynamicJsonDocument json(1024);
     JsonObject updchan = json.createNestedObject("updchan");
       updchan["result"] = result;
       updchan["dev_id"] = tempid;

     String message;
     serializeJson(updchan, message);
     if (context) {
        ((AsyncWebSocketClient *) context)->text("D2" + message);
     } else {
       LOG_PORT.println("** MISSING CONTEXT *** !!");
     }
  }


  void register_nrf_callbacks () {
     out_driver.nrf_async_otaflash = cb_flash;
     out_driver.nrf_async_rfchan   = cb_rfchan;
     out_driver.nrf_async_devid    = cb_devid;
     out_driver.nrf_async_startaddr= cb_startaddr;

     out_driver.nrf_async_devlist  = cb_devlist;
  }
#endif /*WNRF*/
#endif /* ESPIXELSTICK_H_ */
