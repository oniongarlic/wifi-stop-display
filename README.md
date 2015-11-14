# Wireless Buss Stop Display

ESP8266 based wireless buss stop display

Components needed:
* ESP8266 module with available i2c IO
* i2c connected 20x4 character LCD display
* DS1307 i2c RTC 

The system connects to a MQTT broker and listens to a stop specific topic. The displayed 
stop can be set using any browser by browsing to kotibussi.local if mDNS is used, or directly to 
the device IP address (displayed when connected).

WiFi setup is currently hard coded, see wifisetup.h to setup your WiFi connection settings.

## Backend 
The display itself does not poll the SIRI service, instead a MQTT broker is used to get updates
pushed to the display. Currently the data is JSON, but might be more size optimized in the future.

The needed backend is available at https://github.com/oniongarlic/php-siri

## PubSubClient patch

The max MQTT data size is hard coded to 512 bytes in PubSubClient. This needs to be manually fixed to a
much larger size as the JSON data is pretty verbose and does not fit in 512 bytes. 2048 works.
