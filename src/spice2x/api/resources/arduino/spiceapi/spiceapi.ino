/*
 * SpiceAPI Arduino Example Project
 * 
 * To enable it in SpiceTools, use "-api 1337 -apipass changeme -apiserial COM1" or similar.
 */

/*
 * SpiceAPI Wrapper Buffer Sizes
 * 
 * They should be as big as possible to be able to create/parse
 * some of the bigger requests/responses. Due to dynamic memory
 * limitations of some weaker devices, if you set them too high
 * you will probably experience crashes/bugs/problems, one
 * example would be "Request ID is invalid" in the log.
 */
#define SPICEAPI_WRAPPER_BUFFER_SIZE 256
#define SPICEAPI_WRAPPER_BUFFER_SIZE_STR 256

/*
 * WiFi Support
 * Uncomment to enable the wireless API interface.
 */
//#define ENABLE_WIFI

/*
 * WiFi Settings
 * You can ignore these if you don't plan on using WiFi
 */
#ifdef ENABLE_WIFI
#include <ESP8266WiFi.h>
WiFiClient client;
#define SPICEAPI_INTERFACE client
#define SPICEAPI_INTERFACE_WIFICLIENT
#define SPICEAPI_INTERFACE_WIFICLIENT_HOST "192.168.178.143"
#define SPICEAPI_INTERFACE_WIFICLIENT_PORT 1337
#define WIFI_SSID "MySSID"
#define WIFI_PASS "MyWifiPassword"
#endif

/*
 * This is the interface a serial connection will use.
 * You can change this to another Serial port, e.g. with an
 * Arduino Mega you can use Serial1/Serial2/Serial3.
 */
#ifndef ENABLE_WIFI
#define SPICEAPI_INTERFACE Serial
#endif

/*
 * SpiceAPI Includes
 * 
 * If you have the JSON strings beforehands or want to craft them
 * manually, you don't have to import the wrappers at all and can
 * use Connection::request to send and receive raw JSON strings.
 */
#include "connection.h"
#include "wrappers.h"

/*
 * This global object represents the API connection.
 * The first parameter is the buffer size of the JSON string
 * we're receiving. So a size of 512 will only be able to
 * hold a JSON of 512 characters maximum.
 * 
 * An empty password string means no password is being used.
 * This is the recommended when using Serial only.
 */
spiceapi::Connection CON(512, "changeme");

void setup() {

#ifdef ENABLE_WIFI

  /*
   * When using WiFi, we can use the Serial interface for debugging.
   * You can open Serial Monitor and see what IP it gets assigned to.
   */
  Serial.begin(57600);

  // set WiFi mode to station (disables integrated AP)
  WiFi.mode(WIFI_STA);

  // now try connecting to our Router/AP
  Serial.print("Connecting");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  // print debug info over serial
  Serial.print("\nLocal IP: ");
  Serial.println(WiFi.localIP());
  
#else

  /*
   * Since the API makes use of the Serial module, we need to
   * set it up using our preferred baud rate manually.
   */
  SPICEAPI_INTERFACE.begin(57600);
  while (!SPICEAPI_INTERFACE);
  
#endif
}

void loop() {

  /*
   * Here's a few tests/examples on how to make use of the wrappers.
   */

  // insert cards for P1/P2
  spiceapi::card_insert(CON, 0, "E004012345678901");
  spiceapi::card_insert(CON, 1, "E004012345678902");
  
  // insert a single coin / multiple coins
  spiceapi::coin_insert(CON);
  spiceapi::coin_insert(CON, 3);

  // get the IIDX led ticker text
  char ticker[9];
  if (spiceapi::iidx_ticker_get(CON, ticker)) {
    // if a function returns true, that means success
    // now we can do something with the ticker as if it was a string
    //Serial1.println(ticker);
  }

  // get AVS info
  spiceapi::InfoAvs avs_info {};
  if (spiceapi::info_avs(CON, avs_info)) {
    //Serial1.println(avs_info.model);
  }

  // enter some keys on P1 keypad (blocks until sequence is entered fully)
  spiceapi::keypads_write(CON, 0, "1234");

  // get light states
  spiceapi::LightState lights[8];
  size_t lights_size = spiceapi::lights_read(CON, lights, 8);
  for (size_t i = 0; i < lights_size; i++) {
    auto &light = lights[i];
    //Serial1.println(light.name);
    //Serial1.println(light.value);

    // modify value to full bright
    light.value = 1.f;
  }

  // send back modified light states
  spiceapi::lights_write(CON, lights, lights_size);
  
  // refresh session (generates new crypt key, not that important for serial)
  spiceapi::control_session_refresh(CON);

  // you can also manually send requests without the wrappers
  // this avoids json generation, but you still need to parse it in some way
  const char *answer_json = CON.request(
    "{"
    "\"id\": 0,"
    "\"module\":\"coin\","
    "\"function\":\"insert\","
    "\"params\":[]"
    "}"
  );

  /*
   * For more functions/information, just check out wrappers.h yourself.
   * Have fun :)
   */
   delay(5000);
}
