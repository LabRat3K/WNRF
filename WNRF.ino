/*
  WNRF.ino

  Project: WNRF - An ESP8266, E1.31, and NRF24L01  based pixel driver
  Copyright (c) 2022 Andrew Williams
  http://www.ratsnest.ca
  Based on..
  Project: ESPixelStick - An ESP8266 and E1.31 based pixel driver
  Copyright (c) 2016 Shelby Merrick
  http://www.forkineye.com

   This program is provided free for you to use in any way that you wish,
   subject to the laws and regulations where you are using it.  Due diligence
   is strongly suggested before using this code.  Please give credit where due.

   The Author makes no warranty of any kind, express or implied, with regard
   to this program or the documentation contained in this document.  The
   Author shall not be liable in any event for incidental or consequential
   damages in connection with, or arising out of, the furnishing, performance
   or use of these programs.

*/

/*****************************************/
/*        BEGIN - Configuration          */
/*****************************************/

/* Fallback configuration if config.json is empty or fails */
const char ssid[] = "";
const char passphrase[] = "";


/*****************************************/
/*         END - Configuration           */
/*****************************************/

#include <ESPAsyncE131.h>
#include "ESPAsyncZCPP.h"
#include "ESPAsyncDDP.h"
#include <Hash.h>
#include <SPI.h>
#include "WNRF.h"
#include "FPPDiscovery.h"
#include "EFUpdate.h"
#include "wshandler.h"

extern "C" {
#include <user_interface.h>
}

// Debugging support
#if defined(DEBUG)
extern "C" void system_set_os_print(uint8 onoff);
extern "C" void ets_install_putc1(void* routine);

static void _u0_putc(char c) {
  while (((U0S >> USTXC) & 0x7F) == 0x7F);
  U0F = c;
}
#endif

/////////////////////////////////////////////////////////
//
//  Globals
//
/////////////////////////////////////////////////////////

// MQTT State
const char MQTT_SET_COMMAND_TOPIC[] = "/set";

// MQTT Payloads by default (on/off)
const char LIGHT_ON[] = "ON";
const char LIGHT_OFF[] = "OFF";

// Configuration file
const char CONFIG_FILE[] = "/config.json";

ESPAsyncE131        e131(10);       // ESPAsyncE131 with X buffers
ESPAsyncZCPP        zcpp(5);        // ESPAsyncZCPP with X buffers
ESPAsyncDDP         ddp(5);         // ESPAsyncDDP with X buffers
FPPDiscovery        fppDiscovery(VERSION);   // FPP Discovery Listener

config_t            config;         // Current configuration
uint32_t            *seqError;      // Sequence error tracking for each universe
uint32_t            *seqZCPPError;  // Sequence error tracking for each universe
uint16_t            lastZCPPConfig; // last config we saw
uint8_t             seqZCPPTracker; // sequence number of zcpp frames
uint16_t            uniLast = 1;    // Last Universe to listen for
bool                reboot = false; // Reboot flag
AsyncWebServer      web(HTTP_PORT); // Web Server
AsyncWebSocket      ws("/ws");      // Web Socket Plugin
uint8_t             *seqTracker;    // Current sequence numbers for each Universe */
uint32_t            lastUpdate;     // Update timeout tracker
WiFiEventHandler    wifiConnectHandler;     // WiFi connect handler
WiFiEventHandler    wifiDisconnectHandler;  // WiFi disconnect handler
Ticker              wifiTicker;     // Ticker to handle WiFi
Ticker              idleTicker;     // Ticker for effect on idle
#ifdef MQTT
AsyncMqttClient     mqtt;           // MQTT object
Ticker              mqttTicker;     // Ticker to handle MQTT
#endif
EffectEngine        effects;        // Effects Engine
IPAddress           ourLocalIP;
IPAddress           ourSubnetMask;

// Output Drivers
#if defined(ESPS_MODE_WNRF)
WnrfDriver      out_driver;
#else
#error "No valid output mode defined."
#endif

#define LED_WIFI 2
// LED_NRF comes from WnrfDriver.h

#define LED_OFF    0x0000
#define BLINK_1HZ  0xFF00
#define BLINK_2HZ  0xF0F0
#define BEAT       ~(0x0033)
#define FLICKER    ~(0x3030)
// Avoid using this, so that we have visual that the main loop is operational
#define SOLID      0xFFFF

#ifdef LED_WIFI
uint16_t led_state_wifi=LED_OFF;
uint32_t led_timeout =0;
uint16_t led_mask = 0x0001;
#endif
/////////////////////////////////////////////////////////
//
//  Forward Declarations
//
/////////////////////////////////////////////////////////

void loadConfig();
void initWifi();
void initWeb();
void updateConfig();

// Radio config
RF_PRE_INIT() {
    //wifi_set_phy_mode(PHY_MODE_11G);    // Force 802.11g mode
    system_phy_set_powerup_option(31);  // Do full RF calibration on power-up
    system_phy_set_max_tpw(82);         // Set max TX power
}

void ZCPPSub(); // Forward declaration

void setup() {
    // Configure SDK params
    wifi_set_sleep_type(NONE_SLEEP_T);

#if defined (DATA_PIN)
    // Initial pin states
    pinMode(DATA_PIN, OUTPUT);
    digitalWrite(DATA_PIN, LOW);
#endif

#if defined (LED_WIFI)
    pinMode(LED_WIFI,OUTPUT);
    digitalWrite(LED_WIFI, HIGH);
#endif
#if defined (LED_NRF)
    pinMode(LED_NRF, OUTPUT);
    digitalWrite(LED_NRF, LOW);
#endif
    // Setup serial log port
    LOG_PORT.begin(115200);
    delay(10);

#if defined(DEBUG)
    ets_install_putc1((void *) &_u0_putc);
    system_set_os_print(1);
#endif

    // Set default data source to E131
    config.ds = DataSource::E131;

    LOG_PORT.println("");
    LOG_PORT.print(F("WNRF v"));
    for (uint8_t i = 0; i < strlen_P(VERSION); i++)
        LOG_PORT.print((char)(pgm_read_byte(VERSION + i)));
    LOG_PORT.print(F(" ("));
    for (uint8_t i = 0; i < strlen_P(BUILD_DATE); i++)
        LOG_PORT.print((char)(pgm_read_byte(BUILD_DATE + i)));
    LOG_PORT.println(")");
    LOG_PORT.println(ESP.getFullVersion());

    // Enable SPIFFS
    if (!SPIFFS.begin())
    {
        LOG_PORT.println("File system did not initialise correctly");
    }
    else
    {
        LOG_PORT.println("File system initialised");
    }

    FSInfo fs_info;
    if (SPIFFS.info(fs_info))
    {
        LOG_PORT.print("Total bytes in file system: ");
        LOG_PORT.println(fs_info.usedBytes);
        LOG_PORT.print("Space:");
        LOG_PORT.println(fs_info.totalBytes - fs_info.usedBytes);

        Dir dir = SPIFFS.openDir("/");
        while (dir.next()) {
          LOG_PORT.print(dir.fileName());
          File f = dir.openFile("r");
          LOG_PORT.println(f.size());
        }
    }
    else
    {
        LOG_PORT.println("Failed to read file system details");
    }

    // Load configuration from SPIFFS and set Hostname
    loadConfig();
    if (config.hostname)
        WiFi.hostname(config.hostname);

#if defined (DATA_PIN)
    out_driver.setPin(DATA_PIN);
#endif
    updateConfig();

    // Do one effects cycle as early as possible
    if (config.ds == DataSource::WEB) {
        effects.run();
    }
    // set the effect idle timer
    idleTicker.attach(config.effect_idletimeout, idleTimeout);

    out_driver.show();

    // Setup WiFi Handlers
    wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);

#ifdef MQTT
    // Setup MQTT Handlers
    if (config.mqtt) {
        mqtt.onConnect(onMqttConnect);
        mqtt.onDisconnect(onMqttDisconnect);
        mqtt.onMessage(onMqttMessage);
        mqtt.setServer(config.mqtt_ip.c_str(), config.mqtt_port);
        // Unset clean session (defaults to true) so we get retained messages of QoS > 0
        mqtt.setCleanSession(config.mqtt_clean);
        if (config.mqtt_user.length() > 0)
            mqtt.setCredentials(config.mqtt_user.c_str(), config.mqtt_password.c_str());
    }
#endif

    // Fallback to default SSID and passphrase if we fail to connect
    initWifi();
    if (WiFi.status() != WL_CONNECTED) {
        LOG_PORT.println(F("*** Timeout - Reverting to default SSID ***"));
        config.ssid = ssid;
        config.passphrase = passphrase;
        initWifi();
#if defined(LED_WIFI)
        led_state_wifi = BLINK_1HZ;
#endif
    }

    // If we fail again, go SoftAP or reboot
    if (WiFi.status() != WL_CONNECTED) {
        if (config.ap_fallback) {
            LOG_PORT.println(F("*** FAILED TO ASSOCIATE WITH AP, GOING SOFTAP ***"));
            WiFi.mode(WIFI_AP);
            String ssid = "WNRF " + String(config.hostname);
            WiFi.softAP(ssid.c_str());
            ourLocalIP = WiFi.softAPIP();
            ourSubnetMask = IPAddress(255,255,255,0);
#if defined(LED_WIFI)
            led_state_wifi = BEAT;
#endif
        } else {
            LOG_PORT.println(F("*** FAILED TO ASSOCIATE WITH AP, REBOOTING ***"));
            ESP.restart();
        }
    }

#if defined(LED_WIFI)
    //digitalWrite(LED_WIFI, LOW);
#endif
    wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWiFiDisconnect);

    LOG_PORT.print("IP : ");
    LOG_PORT.println(ourLocalIP);
    LOG_PORT.print("Subnet mask : ");
    LOG_PORT.println(ourSubnetMask);

    // Configure and start the web server
    initWeb();

    // Setup E1.31
    if (config.multicast) {
        if (e131.begin(E131_MULTICAST, config.universe,
                uniLast - config.universe + 1)) {
            LOG_PORT.println(F("- E131 Multicast Enabled"));
        }  else {
            LOG_PORT.println(F("*** E131 MULTICAST INIT FAILED ****"));
        }
    } else {
        if (e131.begin(E131_UNICAST)) {
            LOG_PORT.print(F("- E131 Unicast port: "));
            LOG_PORT.println(E131_DEFAULT_PORT);
        } else {
            LOG_PORT.println(F("*** E131 UNICAST INIT FAILED ****"));
        }
    }
    fppDiscovery.begin();

    if (ddp.begin(ourLocalIP)) {
      LOG_PORT.println(F("- DDP Enabled"));
    } else {
      LOG_PORT.println(F("*** DDP INIT FAILED ****"));
    }

    lastZCPPConfig = -1;
    if (zcpp.begin(ourLocalIP)) {
        LOG_PORT.println(F("- ZCPP Enabled"));
        ZCPPSub();
    } else {
        LOG_PORT.println(F("*** ZCPP INIT FAILED ****"));
    }
}

/////////////////////////////////////////////////////////
//
//  WiFi Section
//
/////////////////////////////////////////////////////////

void initWifi() {
  // Switch to station mode and disconnect just in case
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (!config.ssid.isEmpty()) {
     connectWifi();
     uint32_t timeout = millis();
     while (WiFi.status() != WL_CONNECTED) {
       LOG_PORT.print(".");
       delay(500);
       if (millis() - timeout > (1000 * config.sta_timeout) ) {
         LOG_PORT.println("");
         LOG_PORT.println(F("*** Failed to connect ***"));
         break;
       }
     }
  }
}

void reconnectWifi() {
  WiFi.reconnect();
}

void connectWifi() {
  delay(secureRandom(100, 500));

  LOG_PORT.println("");
  LOG_PORT.print(F("Connecting to "));
  LOG_PORT.print(config.ssid);
  LOG_PORT.print(F(" as "));
  LOG_PORT.println(config.hostname);

  WiFi.begin(config.ssid.c_str(), config.passphrase.c_str());
  if (config.dhcp) {
    LOG_PORT.print(F("Connecting with DHCP"));
  } else {
    // We don't use DNS, so just set it to our gateway
    WiFi.config(IPAddress(config.ip[0], config.ip[1], config.ip[2], config.ip[3]),
                IPAddress(config.gateway[0], config.gateway[1], config.gateway[2], config.gateway[3]),
                IPAddress(config.netmask[0], config.netmask[1], config.netmask[2], config.netmask[3]),
                IPAddress(config.gateway[0], config.gateway[1], config.gateway[2], config.gateway[3])
               );
    LOG_PORT.print(F("Connecting with Static IP"));
  }
}

void onWifiConnect(const WiFiEventStationModeGotIP &event) {
  LOG_PORT.println("");
  LOG_PORT.print(F("Connected with IP: "));
  LOG_PORT.println(WiFi.localIP());

  ourLocalIP = WiFi.localIP();
  ourSubnetMask = WiFi.subnetMask();

#ifdef MQTT
  // Setup MQTT connection if enabled
  if (config.mqtt)
    connectToMqtt();
#endif

  // Setup mDNS / DNS-SD
  //TODO: Reboot or restart mdns when config.id is changed?
  String chipId = String(ESP.getChipId(), HEX);
  MDNS.setInstanceName(String(config.id + " (" + chipId + ")").c_str());
  if (MDNS.begin(config.hostname.c_str())) {
    MDNS.addService("http", "tcp", HTTP_PORT);
    MDNS.addService("zcpp", "udp", ZCPP_PORT);
    MDNS.addService("ddp", "udp", DDP_PORT);
    MDNS.addService("e131", "udp", E131_DEFAULT_PORT);
    MDNS.addServiceTxt("e131", "udp", "TxtVers", String(RDMNET_DNSSD_TXTVERS));
    MDNS.addServiceTxt("e131", "udp", "ConfScope", RDMNET_DEFAULT_SCOPE);
    MDNS.addServiceTxt("e131", "udp", "E133Vers", String(RDMNET_DNSSD_E133VERS));
    MDNS.addServiceTxt("e131", "udp", "CID", chipId);
    MDNS.addServiceTxt("e131", "udp", "Model", "WNRF");
    MDNS.addServiceTxt("e131", "udp", "Manuf", "LabRat");
  } else {
    LOG_PORT.println(F("*** Error setting up mDNS responder ***"));
  }
}

void onWiFiDisconnect(const WiFiEventStationModeDisconnected &event) {
  LOG_PORT.println(F("*** WiFi Disconnected ***"));

#ifdef MQTT
  // Pause MQTT reconnect while WiFi is reconnecting
  mqttTicker.detach();
#endif
  wifiTicker.once(2, reconnectWifi);
}

// Subscribe to "n" universes, starting at "universe"
void multiSub() {
  uint8_t count;
  ip_addr_t ifaddr;
  ip_addr_t multicast_addr;

  count = uniLast - config.universe + 1;
  ifaddr.addr = static_cast<uint32_t>(WiFi.localIP());
  for (uint8_t i = 0; i < count; i++) {
    multicast_addr.addr = static_cast<uint32_t>(IPAddress(239, 255,
                          (((config.universe + i) >> 8) & 0xff),
                          (((config.universe + i) >> 0) & 0xff)));
    igmp_joingroup(&ifaddr, &multicast_addr);
  }
}

void ZCPPSub() {
  ip_addr_t ifaddr;
  ifaddr.addr = static_cast<uint32_t>(ourLocalIP);
  ip_addr_t multicast_addr;
  multicast_addr.addr = static_cast<uint32_t>(IPAddress(224, 0, 30, 5));
  igmp_joingroup(&ifaddr, &multicast_addr);
  LOG_PORT.println(F("- ZCPP Subscribed to multicast 224.0.30.5"));
}

/////////////////////////////////////////////////////////
//
//  MQTT Section
//
/////////////////////////////////////////////////////////

#ifdef MQTT
void connectToMqtt() {
  LOG_PORT.print(F("- Connecting to MQTT Broker "));
  LOG_PORT.println(config.mqtt_ip);
  mqtt.connect();
}

void onMqttConnect(bool sessionPresent) {
  LOG_PORT.println(F("- MQTT Connected"));

  // Get retained MQTT state
  mqtt.subscribe(config.mqtt_topic.c_str(), 0);
  mqtt.unsubscribe(config.mqtt_topic.c_str());

  // Setup subscriptions
  mqtt.subscribe(String(config.mqtt_topic + MQTT_SET_COMMAND_TOPIC).c_str(), 0);

  // Publish state
  publishState();

  // Publish discovery
  publishHA(config.mqtt_hadisco);
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  LOG_PORT.println(F("- MQTT Disconnected"));
  if (WiFi.isConnected()) {
    mqttTicker.once(2, connectToMqtt);
  }
}

void onMqttMessage(char* topic, char* payload,
                   AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {

  DynamicJsonDocument r(1024);
  DeserializationError error = deserializeJson(r, payload);

  if (error) {
    LOG_PORT.println("MQTT: Parsing failed");
    return;
  }

  JsonObject root = r.as<JsonObject>();

  // if its a retained message and we want clean session, ignore it
  if ( properties.retain && config.mqtt_clean ) {
    return;
  }

  bool stateOn = false;

  if (root.containsKey("state")) {
    if (strcmp(root["state"], LIGHT_ON) == 0) {
      stateOn = true;
    } else if (strcmp(root["state"], LIGHT_OFF) == 0) {
      stateOn = false;
    }
  }

  if (root.containsKey("brightness")) {
    effects.setBrightness((float)root["brightness"] / 255.0);
  }

  if (root.containsKey("speed")) {
    effects.setSpeed(root["speed"]);
  }

  if (root.containsKey("color")) {
    effects.setColor({
      root["color"]["r"],
      root["color"]["g"],
      root["color"]["b"]
    });
  }

  if (root.containsKey("effect")) {
    // Set the explict effect provided by the MQTT client
    effects.setEffect(root["effect"]);
  }

  if (root.containsKey("reverse")) {
    effects.setReverse(root["reverse"]);
  }

  if (root.containsKey("mirror")) {
    effects.setMirror(root["mirror"]);
  }

  if (root.containsKey("allleds")) {
    effects.setAllLeds(root["allleds"]);
  }

  // Set data source based on state - Fall back to E131 when off
  if (stateOn) {
    if (effects.getEffect().equalsIgnoreCase("Disabled"))
      effects.setEffect("Solid");
    config.ds = DataSource::MQTT;
  } else {
    config.ds = DataSource::E131;
    effects.clearAll();
  }

  publishState();
}

void publishHA(bool join) {

  // Setup HA discovery
  String ha_config = config.mqtt_haprefix + "/light/" + String(ESP.getChipId(), HEX) + "/config";

  if (join) {
    DynamicJsonDocument root(1024);
    root["name"] = config.id;
    root["schema"] = "json";
    root["state_topic"] = config.mqtt_topic;
    root["command_topic"] = config.mqtt_topic + "/set";
    root["rgb"] = "true";
    root["brightness"] = "true";
    root["effect"] = "true";

    // Populate the effect list
    JsonArray effect_list = root.createNestedArray("effect_list");
    // effect[0] is 'disabled', skip it
    for (uint8_t i = 1; i < effects.getEffectCount(); i++) {
      effect_list.add(effects.getEffectInfo(i)->name);
    }

    // Register the attributes topic
    root["json_attributes_topic"] = config.mqtt_topic + "/attributes";

    // Create a unique id using the chip id, and fill in the device properties
    // to enable integration support in HomeAssistant.
    root["unique_id"] = "WNRF_" + String(ESP.getChipId(), HEX);
    JsonObject device = root.createNestedObject("device");
    device["identifiers"] = WiFi.macAddress();
    device["manufacturer"] = "WNRF";
    device["model"] = String(config.channel_count / 3) + " Pixel Controller";
    device["name"] = config.id;
    device["sw_version"] = "WNRF v" + String(VERSION);

    char buffer[measureJson(root) + 1];
    serializeJson(root, buffer, sizeof(buffer));
    mqtt.publish(ha_config.c_str(), 0, true, buffer);

    publishAttributes();
  } else {
    mqtt.publish(ha_config.c_str(), 0, true, "");
  }
}

void publishState() {

  DynamicJsonDocument root(1024);
  if ((config.ds != DataSource::E131 && config.ds != DataSource::ZCPP) && (!effects.getEffect().equalsIgnoreCase("Disabled")))
    root["state"] = LIGHT_ON;
  else
    root["state"] = LIGHT_OFF;

  JsonObject color = root.createNestedObject("color");
  color["r"] = effects.getColor().r;
  color["g"] = effects.getColor().g;
  color["b"] = effects.getColor().b;
  root["brightness"] = effects.getBrightness() * 255;
  root["speed"] = effects.getSpeed();
  if (!effects.getEffect().equalsIgnoreCase("Disabled")) {
    root["effect"] = effects.getEffect();
  }
  root["reverse"] = effects.getReverse();
  root["mirror"] = effects.getMirror();
  root["allleds"] = effects.getAllLeds();

  char buffer[measureJson(root) + 1];
  serializeJson(root, buffer, sizeof(buffer));
  mqtt.publish(config.mqtt_topic.c_str(), 0, true, buffer);
}

void publishAttributes() {
  String topic = config.mqtt_topic + "/attributes";
  DynamicJsonDocument root(1024);

  // Publish the e131 attributes=
  root["universe"] = config.universe;
  root["universe_limit"] = config.universe_limit;
  root["channel_start"] = config.channel_start;
  root["channel_count"] = config.channel_count;
  root["multicast"] = config.multicast;

  char buffer[measureJson(root) + 1];
  serializeJson(root, buffer, sizeof(buffer));
  mqtt.publish(topic.c_str(), 0, true, buffer);
}
#endif

/////////////////////////////////////////////////////////
//
//  Web Section
//
/////////////////////////////////////////////////////////

File file; // Watch the spelling
char fw_name[40];
int getFWName(void) {
    Dir dir = SPIFFS.openDir("/16f/");
    if (dir.next()) {
      snprintf(fw_name,40,"%s",dir.fileName().c_str());
      return 1;
    } else {
      sprintf(fw_name,"");
      return 0;
    }
}

void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
  if(!index){
    LOG_PORT.print(F("UploadStart: "));
    LOG_PORT.println(filename.c_str());

    // Delete any existing files
    Dir dir = SPIFFS.openDir("/16f/");
    while (dir.next()) {
      LOG_PORT.print("Removing (");
      LOG_PORT.print(dir.fileName());
      SPIFFS.remove(dir.fileName());
      File f = dir.openFile("r");
      LOG_PORT.println(")");
    }
    file = SPIFFS.open("/16f/"+filename,"w");
  }

  if (!file) {
     // Something went wrong - invalid file handle
     request->send(500, "text/plain", "File Creation Error: " );
     cb_upload_reply(500, (char *) filename.c_str());
  }
  if (len) {
     file.write(data,len);
  }
  if(final){
    file.close();
    LOG_PORT.print(F("\nUploadEnded: "));
    LOG_PORT.print(filename.c_str());
    LOG_PORT.print(", ");
    LOG_PORT.println(index+len);
    request->send(200, "text/plain", "File Upload Completed: " );
    // Update filename
    getFWName();
    LOG_PORT.print(F("FILENAME:"));
    LOG_PORT.print(fw_name);
    LOG_PORT.print(".\n");
    cb_upload_reply(200, fw_name);
  }
}

// Configure and start the web server
void initWeb() {
  // Handle OTA update from asynchronous callbacks
  Update.runAsync(true);

  // Add header for SVG plot support?
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");

  // Setup WebSockets
  ws.onEvent(wsEvent);
  web.addHandler(&ws);

  // Heap status handler
  web.on("/heap", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(200, "text/plain", String(ESP.getFreeHeap()));
  });

  // JSON Config Handler
  web.on("/conf", HTTP_GET, [](AsyncWebServerRequest * request) {
    String jsonString;
    serializeConfig(jsonString, true);
    request->send(200, "text/json", jsonString);
  });

  // Firmware upload handler - only in station mode
  web.on("/updatefw", HTTP_POST, [](AsyncWebServerRequest * request) {
    ws.textAll("X6");
  }, handle_fw_upload).setFilter(ON_STA_FILTER);

  // File Upload Handler
  web.on("/wnrfu", HTTP_POST, [](AsyncWebServerRequest *request) {},
      [](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data,
                    size_t len, bool final) {handleUpload(request, filename, index, data, len, final);}
  );

  // Static Handler
  web.serveStatic("/", SPIFFS, "/www/").setDefaultFile("index.html");

  // Raw config file Handler - but only on station
  //  web.serveStatic("/config.json", SPIFFS, "/config.json").setFilter(ON_STA_FILTER);

  web.onNotFound([](AsyncWebServerRequest * request) {
    request->send(404, "text/plain", "Page not found");
  });

  DefaultHeaders::Instance().addHeader(F("Access-Control-Allow-Origin"), "*");

  // Config file upload handler - only in station mode
  web.on("/config", HTTP_POST, [](AsyncWebServerRequest * request) {
    ws.textAll("X6");
  }, handle_config_upload).setFilter(ON_STA_FILTER);

  web.begin();

  LOG_PORT.print(F("- Web Server started on port "));
  LOG_PORT.println(HTTP_PORT);
}

/////////////////////////////////////////////////////////
//
//  JSON / Configuration Section
//
/////////////////////////////////////////////////////////

// Configuration Validations
void validateConfig() {
    // E1.31 Limits
    if (config.universe < 1)
        config.universe = 1;

    if (config.universe_limit > UNIVERSE_MAX || config.universe_limit < 1)
        config.universe_limit = UNIVERSE_MAX;

    if (config.channel_start < 1)
        config.channel_start = 1;
    else if (config.channel_start > config.universe_limit)
        config.channel_start = config.universe_limit;

#ifdef MQTT
    // Set default MQTT port if missing
    if (config.mqtt_port == 0)
        config.mqtt_port = MQTT_PORT;

    // Generate default MQTT topic if blank
    if (!config.mqtt_topic.length()) {
        config.mqtt_topic = "diy/esps/" + String(ESP.getChipId(), HEX);
    }

    // Set default Home Assistant Discovery prefix if blank
    if (!config.mqtt_haprefix.length()) {
        config.mqtt_haprefix = "homeassistant";
    }
#endif

#if defined(ESPS_MODE_WNRF)
    // Set Mode
    config.devmode = MODE_NRF;
    if (config.nrf_legacy)
        config.channel_count = 32;
    else
        config.channel_count = 512;
#endif

    if (config.effect_speed < 1)
        config.effect_speed = 1;
    if (config.effect_speed > 10)
        config.effect_speed = 10;

    if (config.effect_brightness > 1.0)
        config.effect_brightness = 1.0;
    if (config.effect_brightness < 0.0)
        config.effect_brightness = 0.0;

    if (config.effect_idletimeout == 0) {
        config.effect_idletimeout = 10;
        config.effect_idleenabled = false;
    }

    if (config.effect_startenabled) {
        if (effects.isValidEffect(config.effect_name)) {
            effects.setEffect(config.effect_name);

            if ( !config.effect_name.equalsIgnoreCase("disabled")
              && !config.effect_name.equalsIgnoreCase("view") ) {
                config.ds = DataSource::WEB;
            }

        }
    }
}

void updateConfig() {
    // Validate first
    validateConfig();

    // Find the last universe we should listen for
    uint16_t span = config.channel_start + config.channel_count - 1;
    if (span % config.universe_limit)
        uniLast = config.universe + span / config.universe_limit;
    else
        uniLast = config.universe + span / config.universe_limit - 1;

    // Setup the sequence error tracker
    uint8_t uniTotal = (uniLast + 1) - config.universe;

    if (seqTracker) free(seqTracker);
    if ((seqTracker = static_cast<uint8_t *>(malloc(uniTotal))))
        memset(seqTracker, 0x00, uniTotal);

    seqZCPPTracker = 0;

    if (seqError) free(seqError);
    if ((seqError = static_cast<uint32_t *>(malloc(uniTotal * 4))))
        memset(seqError, 0x00, uniTotal * 4);

    seqZCPPError = 0;

    // Zero out packet stats
    e131.stats.num_packets = 0;
    zcpp.stats.num_packets = 0;

    // Initialize for our pixel type
#if defined(ESPS_MODE_WNRF)
    out_driver.begin(config.nrf_baud, config.nrf_chan, config.channel_count);
    effects.begin(&out_driver, config.channel_count / 3 );
    register_nrf_callbacks(); // Allow NRF driver to send ASYNC responses to WEB client
#endif

    LOG_PORT.print(F("- Listening for "));
    LOG_PORT.print(config.channel_count);
    LOG_PORT.print(F(" channels, from Universe "));
    LOG_PORT.print(config.universe);
    LOG_PORT.print(F(" to "));
    LOG_PORT.println(uniLast);

    // Setup IGMP subscriptions if multicast is enabled
    if (config.multicast)
        multiSub();

#ifdef MQTT
    // Update Home Assistant Discovery if enabled
    if (config.mqtt) {
        publishHA(config.mqtt_hadisco);
        publishState();
    }
#endif
}

// De-Serialize Network config
void dsNetworkConfig(const JsonObject &json) {
    if (json.containsKey("network")) {
        JsonObject networkJson = json["network"];

        // Fallback to embedded ssid and passphrase if null in config
        config.ssid = networkJson["ssid"].as<String>();
        if (!config.ssid.length())
            config.ssid = ssid;

        config.passphrase = networkJson["passphrase"].as<String>();
        if (!config.passphrase.length())
            config.passphrase = passphrase;

        // Network
        for (int i = 0; i < 4; i++) {
            config.ip[i] = networkJson["ip"][i];
            config.netmask[i] = networkJson["netmask"][i];
            config.gateway[i] = networkJson["gateway"][i];
        }
        config.dhcp = networkJson["dhcp"];
        config.sta_timeout = networkJson["sta_timeout"] | CLIENT_TIMEOUT;
        if (config.sta_timeout < 5) {
            config.sta_timeout = 5;
        }

        config.ap_fallback = networkJson["ap_fallback"];
        config.ap_timeout = networkJson["ap_timeout"] | AP_TIMEOUT;
        if (config.ap_timeout < 15) {
            config.ap_timeout = 15;
        }

        // Generate default hostname if needed
        config.hostname = networkJson["hostname"].as<String>();
    }
    else {
        LOG_PORT.println("No network settings found.");
    }

    if (!config.hostname.length()) {
        config.hostname = "esps-" + String(ESP.getChipId(), HEX);
    }
}

// De-serialize Effect Config
void dsEffectConfig(const JsonObject &json) {
    // Effects
    if (json.containsKey("effects")) {
        JsonObject effectsJson = json["effects"];
        config.effect_name = effectsJson["name"].as<String>();
        config.effect_mirror = effectsJson["mirror"];
        config.effect_allleds = effectsJson["allleds"];
        config.effect_reverse = effectsJson["reverse"];
        if (effectsJson.containsKey("speed"))
            config.effect_speed = effectsJson["speed"];
        config.effect_color = { effectsJson["r"], effectsJson["g"], effectsJson["b"] };
        if (effectsJson.containsKey("brightness"))
            config.effect_brightness = effectsJson["brightness"];
        config.effect_startenabled = effectsJson["startenabled"];
        config.effect_idleenabled = effectsJson["idleenabled"];
        config.effect_idletimeout = effectsJson["idletimeout"];
    }
    else
    {
        LOG_PORT.println("No effect settings found.");
    }
}

// De-serialize Device Config
void dsDeviceConfig(const JsonObject &json) {
    // Device
    if (json.containsKey("device")) {
        config.id = json["device"]["id"].as<String>();
    }
    else
    {
        LOG_PORT.println("No device settings found.");
    }

    // E131
    if (json.containsKey("e131")) {
        config.universe = json["e131"]["universe"];
        config.universe_limit = json["e131"]["universe_limit"];
        config.channel_start = json["e131"]["channel_start"];
        config.channel_count = json["e131"]["channel_count"];
        config.multicast = json["e131"]["multicast"];
    }
    else
    {
        LOG_PORT.println("No e131 settings found.");
    }

#ifdef MQTT
    // MQTT
    if (json.containsKey("mqtt")) {
        JsonObject mqttJson = json["mqtt"];
        config.mqtt = mqttJson["enabled"];
        config.mqtt_ip = mqttJson["ip"].as<String>();
        config.mqtt_port = mqttJson["port"];
        config.mqtt_user = mqttJson["user"].as<String>();
        config.mqtt_password = mqttJson["password"].as<String>();
        config.mqtt_topic = mqttJson["topic"].as<String>();
        config.mqtt_clean = mqttJson["clean"] | false;
        config.mqtt_hadisco = mqttJson["hadisco"] | false;
        config.mqtt_haprefix = mqttJson["haprefix"].as<String>();
    }
    else
    {
        LOG_PORT.println("No mqtt settings found.");
    }
#endif

#if defined(ESPS_MODE_WNRF)
    if (json.containsKey("wnrf")) {
        config.nrf_legacy = (json["wnrf"]["enabled"]);
        if (config.nrf_legacy) {
            config.nrf_chan = NrfChan::NRFCHAN_LEGACY;
            config.nrf_baud = NrfBaud::BAUD_2Mbps;
            config.channel_count = 32;
        } else {
            config.nrf_chan = NrfChan(static_cast<uint8_t>(json["wnrf"]["nrf_chan"]));
            config.nrf_baud = NrfBaud(static_cast<uint32_t>(json["wnrf"]["nrf_baud"]));
            config.channel_count = 512;
        }
    }
    else
    {
        LOG_PORT.println("WNRF defaulted to: CHAN 82, 2Mbps.");
	config.nrf_chan = NrfChan::NRFCHAN_G;
	config.nrf_baud = NrfBaud::BAUD_2Mbps;
	config.channel_count = 512;
    }
#endif
}

// Load configugration JSON file
void loadConfig() {
    // Zeroize Config struct
    memset(&config, 0, sizeof(config));

    effects.setFromDefaults();

    // Load CONFIG_FILE json. Create and init with defaults if not found
    File file = SPIFFS.open(CONFIG_FILE, "r");
    if (!file) {
        LOG_PORT.println(F("- No configuration file found."));
        config.ssid = ssid;
        config.passphrase = passphrase;
        config.hostname = "esps-" + String(ESP.getChipId(), HEX);
        config.ap_fallback = true;
        config.id = "No Config Found";
        saveConfig();
    } else {
        // Parse CONFIG_FILE json
        size_t size = file.size();
        if (size > CONFIG_MAX_SIZE) {
            LOG_PORT.println(F("*** Configuration File too large ***"));
            return;
        }

        std::unique_ptr<char[]> buf(new char[size]);
        file.readBytes(buf.get(), size);

        DynamicJsonDocument json(1024);
        DeserializationError error = deserializeJson(json, buf.get());
        if (error) {
            LOG_PORT.println(F("*** Configuration File Format Error ***"));
            return;
        }

        dsNetworkConfig(json.as<JsonObject>());
        dsDeviceConfig(json.as<JsonObject>());
        dsEffectConfig(json.as<JsonObject>());

        LOG_PORT.println(F("- Configuration loaded."));
    }

    // Validate it
    validateConfig();

    effects.setFromConfig();

}

// Serialize the current config into a JSON string
void serializeConfig(String &jsonString, bool pretty, bool creds) {
    // Create buffer and root object
    DynamicJsonDocument json(1024);

    // Device
    JsonObject device = json.createNestedObject("device");
    device["id"] = config.id.c_str();
    device["mode"] = config.devmode;

    // Network
    JsonObject network = json.createNestedObject("network");
    network["ssid"] = config.ssid.c_str();
    if (creds)
        network["passphrase"] = config.passphrase.c_str();
    network["hostname"] = config.hostname.c_str();
    JsonArray ip = network.createNestedArray("ip");
    JsonArray netmask = network.createNestedArray("netmask");
    JsonArray gateway = network.createNestedArray("gateway");
    for (int i = 0; i < 4; i++) {
        ip.add(config.ip[i]);
        netmask.add(config.netmask[i]);
        gateway.add(config.gateway[i]);
    }
    network["dhcp"] = config.dhcp;
    network["sta_timeout"] = config.sta_timeout;

    network["ap_fallback"] = config.ap_fallback;
    network["ap_timeout"] = config.ap_timeout;

    // Effects
    JsonObject _effects = json.createNestedObject("effects");
    _effects["name"] = config.effect_name;

    _effects["mirror"] = config.effect_mirror;
    _effects["allleds"] = config.effect_allleds;
    _effects["reverse"] = config.effect_reverse;
    _effects["speed"] = config.effect_speed;
    _effects["brightness"] = config.effect_brightness;

    _effects["r"] = config.effect_color.r;
    _effects["g"] = config.effect_color.g;
    _effects["b"] = config.effect_color.b;

    _effects["brightness"] = config.effect_brightness;
    _effects["startenabled"] = config.effect_startenabled;
    _effects["idleenabled"] = config.effect_idleenabled;
    _effects["idletimeout"] = config.effect_idletimeout;

#ifdef MQTT
    // MQTT
    JsonObject _mqtt = json.createNestedObject("mqtt");
    _mqtt["enabled"] = config.mqtt;
    _mqtt["ip"] = config.mqtt_ip.c_str();
    _mqtt["port"] = config.mqtt_port;
    _mqtt["user"] = config.mqtt_user.c_str();
    _mqtt["password"] = config.mqtt_password.c_str();
    _mqtt["topic"] = config.mqtt_topic.c_str();
    _mqtt["clean"] = config.mqtt_clean;
    _mqtt["hadisco"] = config.mqtt_hadisco;
    _mqtt["haprefix"] = config.mqtt_haprefix.c_str();
#endif

    // E131
    JsonObject e131 = json.createNestedObject("e131");
    e131["universe"] = config.universe;
    e131["universe_limit"] = config.universe_limit;
    e131["channel_start"] = config.channel_start;
    e131["channel_count"] = config.channel_count;
    e131["multicast"] = config.multicast;

#if defined(ESPS_MODE_WNRF)
    JsonObject wnrf = json.createNestedObject("wnrf");
    wnrf["enabled"]  = config.nrf_legacy;
    wnrf["nrf_chan"] = static_cast<uint8_t>(config.nrf_chan);
    wnrf["nrf_baud"] = static_cast<uint8_t>(config.nrf_baud);
    getFWName();
    wnrf["nrf_fw"] =fw_name;
#endif

    if (pretty)
        serializeJsonPretty(json, jsonString);
    else
        serializeJson(json, jsonString);
}

// Save configuration JSON file
void saveConfig() {
    // Update Config
    updateConfig();

    // Serialize Config
    String jsonString;
    serializeConfig(jsonString, true, true);

    // Save Config
    File file = SPIFFS.open(CONFIG_FILE, "w");
    if (!file) {
        LOG_PORT.println(F("*** Error creating configuration file ***"));
        return;
    } else {
        file.println(jsonString);
        LOG_PORT.println(F("* Configuration saved."));
    }
}

void idleTimeout() {
   idleTicker.attach(config.effect_idletimeout, idleTimeout);
    if ( (config.effect_idleenabled) && (config.ds == DataSource::E131 || config.ds == DataSource::ZCPP) ) {
        config.ds = DataSource::IDLEWEB;
        effects.setFromConfig();
    }
}

void sendZCPPConfig(ZCPP_packet_t& packet) {

    LOG_PORT.println("Sending ZCPP Configuration query response.");

    memset(&packet, 0x00, sizeof(packet));
    memcpy(packet.QueryConfigurationResponse.Header.token, ZCPP_token, sizeof(ZCPP_token));
    packet.QueryConfigurationResponse.Header.type = ZCPP_TYPE_QUERY_CONFIG_RESPONSE;
    packet.QueryConfigurationResponse.Header.protocolVersion = ZCPP_CURRENT_PROTOCOL_VERSION;
    packet.QueryConfigurationResponse.sequenceNumber = ntohs(lastZCPPConfig);
    strncpy(packet.QueryConfigurationResponse.userControllerName, config.id.c_str(), std::min((int)strlen(config.id.c_str()), (int)sizeof(packet.QueryConfigurationResponse.userControllerName)));
    if (config.channel_count == 0) {
        packet.QueryConfigurationResponse.ports = 0;
    }
    else {
        packet.QueryConfigurationResponse.ports = 1;
        packet.QueryConfigurationResponse.PortConfig[0].port = 0;
        packet.QueryConfigurationResponse.PortConfig[0].string = 0;
        packet.QueryConfigurationResponse.PortConfig[0].startChannel = ntohl((uint32_t)config.channel_start);
        packet.QueryConfigurationResponse.PortConfig[0].channels = ntohl((uint32_t)config.channel_count);
    }

    zcpp.sendConfigResponse(&packet);
}

#if defined(LED_WIFI)
void blink_led() {
  // Divide timeout into 16 slices
  if (millis()-led_timeout > (2000/16)) {
     led_timeout = millis();
#if defined(LED_WIFI)
     digitalWrite(LED_WIFI,(led_state_wifi & led_mask)>0);
#endif
     led_mask<<=1;
     if (!led_mask)
       led_mask = 1;
  }
}
#endif
/////////////////////////////////////////////////////////
//
//  Main Loop
//
/////////////////////////////////////////////////////////
void loop() {
    // Reboot handler
    if (reboot) {
        delay(REBOOT_DELAY);
        ESP.restart();
    }

    bool doShow = true;

#if defined(LED_WIFI) || defined(LED_NRF)
    blink_led();
#endif
    // Render output for current data source
    if ( (config.ds == DataSource::E131) || (config.ds == DataSource::ZCPP) || (config.ds == DataSource::DDP)  || (config.ds == DataSource::IDLEWEB) ) {
            // Parse a packet and update pixels
            while (!e131.isEmpty()) {
                e131_packet_t packet;
                idleTicker.attach(config.effect_idletimeout, idleTimeout);
                if (config.ds == DataSource::IDLEWEB || config.ds == DataSource::ZCPP) {
                    config.ds = DataSource::E131;
                }

                e131.pull(&packet);
                uint16_t universe = htons(packet.universe);
                uint8_t *data = packet.property_values + 1;
                //LOG_PORT.print(universe);
                //LOG_PORT.println(packet.sequence_number);
                if ((universe >= config.universe) && (universe <= uniLast)) {
                    // Universe offset and sequence tracking
                    uint8_t uniOffset = (universe - config.universe);
                    if (packet.sequence_number != seqTracker[uniOffset]++) {
                        LOG_PORT.print(F("Sequence Error - expected: "));
                        LOG_PORT.print(seqTracker[uniOffset] - 1);
                        LOG_PORT.print(F(" actual: "));
                        LOG_PORT.print(packet.sequence_number);
                        LOG_PORT.print(F(" universe: "));
                        LOG_PORT.println(universe);
                        seqError[uniOffset]++;
                        seqTracker[uniOffset] = packet.sequence_number + 1;
                    }

                    // Offset the channels if required
                    uint16_t offset = 0;
                    offset = config.channel_start - 1;

                    // Find start of data based off the Universe
                    int16_t dataStart = uniOffset * config.universe_limit - offset;

                    // Calculate how much data we need for this buffer
                    uint16_t dataStop = config.channel_count;
                    uint16_t channels = htons(packet.property_value_count) - 1;
                    if (config.universe_limit < channels)
                        channels = config.universe_limit;
                    if ((dataStart + channels) < dataStop)
                        dataStop = dataStart + channels;

                    // Set the data
                    uint16_t buffloc = 0;

                    // ignore data from start of first Universe before channel_start
                    if (dataStart < 0) {
                        dataStart = 0;
                        buffloc = config.channel_start - 1;
                    }

                    for (int i = dataStart; i < dataStop; i++) {
                        out_driver.setValue(i, data[buffloc]);
                        buffloc++;
                    }
                }
            }
            while (!ddp.isEmpty()) {
              DDP_packet_t ddpPacket;
              ddp.pull(&ddpPacket);
              if (ddpPacket.header.flags & 0x01) {
                doShow = true;
              } else {
                doShow = false;
              }
              uint16_t len = htons(ddpPacket.header.dataLen);
              uint32_t offset = htonl(ddpPacket.header.channelOffset);
              bool tc = ddpPacket.header.flags & 0x10;
              uint8_t *data = ddpPacket.header.data;
              if (tc) {
                data = ddpPacket.timeCodeHeader.data;
              }
              for (int i = offset; i < offset + len; i++) {
                if (i < config.channel_count) {
                 out_driver.setValue(i, data[i - offset]);
                }
              }
            }

            bool abortPacketRead = false;
            while (!zcpp.isEmpty() && !abortPacketRead) {
                ZCPP_packet_t zcppPacket;
                idleTicker.attach(config.effect_idletimeout, idleTimeout);
                if (config.ds == DataSource::IDLEWEB || config.ds == DataSource::E131) {
                    config.ds = DataSource::ZCPP;
                }

                zcpp.pull(&zcppPacket);

                switch (zcppPacket.Discovery.Header.type) {
                  case ZCPP_TYPE_DISCOVERY: // discovery
                      {
                          LOG_PORT.println("ZCPP Discovery received.");
                          int serialPorts = 0;
                          int pixelPorts = 0;
                          int wnrfPorts = 0;
        #if defined(ESPS_MODE_WNRF)
                            wnrfPorts = 1;
        #endif
                          char version[9];
                          memset(version, 0x00, sizeof(version));
                          for (uint8_t i = 0; i < min(strlen_P(VERSION), sizeof(version)-1); i++)
                            version[i] = pgm_read_byte(VERSION + i);

                          uint8_t mac[WL_MAC_ADDR_LENGTH];
                          zcpp.sendDiscoveryResponse(&zcppPacket, version, WiFi.macAddress(mac), config.id.c_str(), pixelPorts, serialPorts, 680 * 3, 512, 680 * 3, static_cast<uint32_t>(ourLocalIP), static_cast<uint32_t>(ourSubnetMask));
                      }
                      break;
                  case ZCPP_TYPE_CONFIG: // config
                      LOG_PORT.println("ZCPP Config received.");
                      if (htons(zcppPacket.Configuration.sequenceNumber) != lastZCPPConfig) {
                        // a new config to apply
                        LOG_PORT.print("    The config is new: ");
                        LOG_PORT.println(htons(zcppPacket.Configuration.sequenceNumber));

                        config.id = String(zcppPacket.Configuration.userControllerName);
                        LOG_PORT.print("    Controller Name: ");
                        LOG_PORT.println(config.id);

                        ZCPP_PortConfig* p = zcppPacket.Configuration.PortConfig;
                        for (int i = 0; i < zcppPacket.Configuration.ports; i++) {
                            if (p->port == 0) {
                                switch(p->protocol) {
                                    default:
                                        LOG_PORT.print("Attempt to configure invalid protocol ");
                                        LOG_PORT.print(p->protocol);
                                        break;
                                }
                                LOG_PORT.print("    Protocol: ");
                                config.channel_start = htonl(p->startChannel);
                                LOG_PORT.print("    Start Channel: ");
                                LOG_PORT.println(config.channel_start);
                                config.channel_count = htonl(p->channels);
                                LOG_PORT.print("    Channel Count: ");
                                LOG_PORT.println(config.channel_count);
                            }
                            else {
                                LOG_PORT.print("Attempt to configure invalid port ");
                                LOG_PORT.print(p->port);
                            }

                            p += sizeof(zcppPacket.Configuration.PortConfig);
                          }

                          if (zcppPacket.Configuration.flags & ZCPP_CONFIG_FLAG_LAST) {
                              lastZCPPConfig = htons(zcppPacket.Configuration.sequenceNumber);
                              saveConfig();
                              if ((zcppPacket.Configuration.flags & ZCPP_CONFIG_FLAG_QUERY_CONFIGURATION_RESPONSE_REQUIRED) != 0) {
                                sendZCPPConfig(zcppPacket);
                              }
                          }
                      }
                      else {
                        LOG_PORT.println("    The config has not changed.");
                      }
                      break;
                  case ZCPP_TYPE_QUERY_CONFIG: // query config
                      sendZCPPConfig(zcppPacket);
                      break;
                  case ZCPP_TYPE_SYNC: // sync
                    doShow = true;
                    // exit read and send data to the pixels
                    abortPacketRead = true;
                    break;
                  case ZCPP_TYPE_DATA: // data
                      uint8_t seq = zcppPacket.Data.sequenceNumber;
                      uint32_t offset = htonl(zcppPacket.Data.frameAddress);
                      bool frameLast = zcppPacket.Data.flags & ZCPP_DATA_FLAG_LAST;
                      uint16_t len = htons(zcppPacket.Data.packetDataLength);
                      bool sync = (zcppPacket.Data.flags & ZCPP_DATA_FLAG_SYNC_WILL_BE_SENT) != 0;

                      if (sync) {
                        // suppress display until we see a sync
                        doShow = false;
                      }

                      if (seq != seqZCPPTracker) {
                        LOG_PORT.print(F("Sequence Error - expected: "));
                        LOG_PORT.print(seqZCPPTracker);
                        LOG_PORT.print(F(" actual: "));
                        LOG_PORT.println(seq);
                        seqZCPPError++;
                      }

                      if (frameLast)
                        seqZCPPTracker = seq + 1;

                      zcpp.stats.num_packets++;

                      for (int i = offset; i < offset + len; i++) {
                         out_driver.setValue(i, zcppPacket.Data.data[i - offset]);
                      }

                      break;
                }
            }
    }

    if (doShow) {
        /* LabRat - replace with != DataSource::E131 ?? */
        if ( (config.ds == DataSource::WEB)
          || (config.ds == DataSource::IDLEWEB)
          || (config.ds == DataSource::ZCPP)
          || (config.ds == DataSource::DDP)
          || (config.ds == DataSource::MQTT) ) {
                effects.run();
        }

        /* Streaming refresh */
        if (out_driver.canRefresh())
            out_driver.show();
    }

// workaround crash - consume incoming bytes on serial port
    if (LOG_PORT.available()) {
        int8_t inch = LOG_PORT.read();

        while (inch >=0 ) {
           switch (inch) {
              case 'q':  out_driver.sendBeacon();  break; // QUERY DEVICE
              case 'p':  out_driver.printIt();      break;
              case 'a':  out_driver.disableAdmin(); break;
              case 'A':  out_driver.enableAdmin();  break;
/*
              case 'd':  out_driver.sendNewDevId(0x0001,0x0042); break; // Set DEVICE ID
              case 'c':  out_driver.sendNewChan(0x0001,101); break;
              case 's':  out_driver.sendNewStart(0x0001,142); break;
*/
              default: break;
           }
           inch = LOG_PORT.read();
        }

    }

    /* Was there a NRF payload? */
    out_driver.checkRx();
}
