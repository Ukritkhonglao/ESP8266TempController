/*
UdpTempController.ino

Version 0.0.5
Last Modified 06/22/2015
By Jim Mayhugh

V0.0.3 - added upper and lower temp control to UDP commands
         added updateState.ino

V0.0.4 - added findChips.ino to discover chips and set DS18B20
          to 9-bit resolution
         added setDebug for serial debug output 0 = no debug, 1 or higher = debug

V0.0.5 - Added mode control

V0.0.6 - using Ticker.h for DS18B20 reads

V0.0.7 - Added Debug Mode and set domain name
         Added setting initial SSID, PASSWD and UDP port via Serial Monitor
          and saving to EEPROM.

V0.0.8 - Added time delay in seconds for switches.
         Added additional UDP Commands.
         Added individual debug values for troubleshooting purposes.

V0.0.9 - Added logging to remote port
         Display Error message on LCD if unit fails to connect to intranet
         Show ESP8266 MAC Address in Serial Monitor
         Display EEPROM Values in Serial Monitor on startup
         
Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

This software uses multiple libraries that are subject to additional
licenses as defined by the author of that software. It is the user's
and developer's responsibility to determine and adhere to any additional
requirements that may arise.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Configuration :
  Enter the ssid and password of your Wifi AP.
  Enter the port number your server is listening on.

*/

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <Ticker.h>
#include <OneWire.h>
#include <Wire.h>
#include <ESP8266LCD.h>
#include <EEPROM.h>
#include <EEPROMAnything.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

int status = WL_IDLE_STATUS;
const uint8_t domainCnt = 21;
const uint8_t usemDNS = 0xAA;

const uint16_t udpDebug       = 0x0001;
const uint16_t tempDebug      = 0x0002;
const uint16_t switchDebug    = 0x0004;
const uint16_t lcdDebug       = 0x0008;
const uint16_t eepromDebug    = 0x0010;
const uint16_t findChipsDebug = 0x0020;

uint16_t setDebug = 0x0000;

char packetBuffer[512]; // buffer to hold incoming and outgoing packets
char updateBuffer[128]; // buffer to hold updateStatus
char lcdBuffer[21];

int16_t noBytes, packetCnt;
int16_t delayVal = 25, sDelayVal = 5000, uDelayVal = 60;
int16_t lowerC, lowerF, upperC, upperF;
uint32_t lowerDelay, upperDelay, tempDelay, startUpperTimer, startLowerTimer;
int8_t i;
uint8_t udpAddr[5];
uint8_t data[15];
uint8_t chip[8];
uint8_t chipStatus[3];
char *delim =",";
char *result = NULL;
char mDNSdomain[domainCnt] = "ESP8266";
uint8_t chipCnt = 0;
uint8_t mode = 0xFF, mDNSset;

// EEPROM Storage locations
const uint16_t EEPROMsize   = 512;
const uint16_t EEMode       = 0x0008; // 'M' = Manual, 'A' = Automatic, anything else = uninitialized
const uint16_t EELowerC     = 0x0010;
const uint16_t EELowerF     = 0x0020;
const uint16_t EEUpperC     = 0x0030;
const uint16_t EEUpperF     = 0x0040;
const uint16_t EEmDNSset    = 0x0050; // 0xAA = set, anything else is uninitialized
const uint16_t EEmDNSdomain = 0x0060; // mDNS domain name
const uint16_t EEWiFiSet    = 0x0080; // 0xAA = set, anything else is unitialized
const uint16_t EEssid       = 0x0090; // WiFi SSID   string
const uint16_t EEpasswd     = 0x00B0; // WiFi PASSWD string
const uint16_t EEuseUDPport = 0x00E0; // 0xAA = set, anything else is uninitialized
const uint16_t EEudpPort    = 0x00F0; // UDP port address
const uint16_t EEs0DelaySet = 0x0100; // 0xAA = set, anything else is uninitialized
const uint16_t EEs0Delay    = 0x0110; // Switch1 Delay
const uint16_t EEs1DelaySet = 0x0120; // 0xAA = set, anything else is uninitialized
const uint16_t EEs1Delay    = 0x0130; // Switch1 Delay

const uint8_t useS0 = 0xAA;
const uint8_t useS1 = 0xAA;
uint8_t s0Set = 0, s1Set = 0;

// WiFi stuff
const uint8_t WiFiStrCnt = 32;  // max string length
const uint8_t useWiFi = 0xAA;
const uint8_t useUDPport = 0xAA;
const uint8_t udpPortCnt = 4;
const uint8_t macCnt = 6;
uint8_t wifiSet = 0, udpSet = 0;
uint8_t macAddress[macCnt] = {0,0,0,0,0,0};
char ssid[WiFiStrCnt]   = "SSID";        // your network SSID (name)
char passwd[WiFiStrCnt] = "PASSWD";      // your network password
uint16_t udpPort = 0x0000;                // local port to listen for UDP packets

// LCD Stuff

// The shield uses the I2C SCL pin 5 and SDA pin 4 on the ESP8266.
// Make sure that SDA and SCA use 4.7K pullup resistors 
// You can connect other I2C sensors to the I2C bus and share
// the I2C bus.

uint8_t const lcdChars = 20;
uint8_t const lcdRows  = 4;

ESP8266LCD lcd = ESP8266LCD(7);

// A UDP instance to let us send and receive packets over UDP
WiFiUDP Udp;

// A UDP instance to allow interval updates to be sent
IPAddress statusIP;
uint16_t  statusPort;
WiFiUDP udpStatus;

// OneWire Stuff
// Family codes
const uint8_t t3tcID         = 0xAA; // Teensy 3.0 1-wire slave with MAX31855 K-type Thermocouple chip
const uint8_t dsLCD          = 0x47; // Teensy 3.x 1-wire slave 4x20 HD44780 LCD
const uint8_t dsGLCDP        = 0x45; // Teensy 3.1 1-wire slave 800x400 7" GLCD with Paging
const uint8_t dsGLCD         = 0x44; // Teensy 3.1 1-wire slave 800x400 7" GLCD
const uint8_t max31850ID     = 0x3B; // MAX31850 K-type Thermocouple chip
const uint8_t ds2762ID       = 0x30; // Maxim 2762 digital k-type thermocouple
const uint8_t ds18b20ID      = 0x28; // Maxim DS18B20 digital Thermometer device
const uint8_t ds2406ID       = 0x12; // Maxim DS2406+ digital switch

// DS2406+ Digital Switch Family Code and registers
const uint8_t dsPIO_A        = 0x20;
const uint8_t dsPIO_B        = 0x40;
const uint8_t ds2406MemWr    = 0x55;
const uint8_t ds2406MemRd    = 0xaa;
const uint8_t ds2406AddLow   = 0x07;
const uint8_t ds2406AddHi    = 0x00;
const uint8_t ds2406PIOAoff  = 0x3f;
const uint8_t ds2406PIOAon   = 0x1f;
const uint8_t ds2406End      = 0xff;

const uint8_t switchStatusON   = 'N';
const uint8_t switchStatusOFF  = 'F';

const uint8_t oneWireAddress  =  2; // OneWire Bus Address - use pin 2 for TeensyNet board
const uint8_t chipAddrSize    =  8; // 64bit OneWire Address
const uint8_t tempDataSize    =  9; // temp data
const uint8_t switchDataSize  = 13; // switch data
const uint8_t chipNameSize    = 15;
const uint8_t cDelayVal       = 150; //delay reading DS18B20

bool  tempConversion = FALSE;
bool  sendStatus = FALSE;
bool  softReset = FALSE;    
// Temp Stuff
typedef struct
{
  uint8_t     tempAddr[chipAddrSize];
  uint8_t     tempData[tempDataSize];
  int16_t     tempFahrenheit;
  int16_t     tempCelsius;
  char        tempName[chipNameSize+1];
}tempStruct;

const tempStruct tempClear = { {0,0,0,0,0,0,0,0}, {0,0,0,0,0,0,0,0,0}, 0, 0, "" };

tempStruct ds18b20 = { {0,0,0,0,0,0,0,0}, {0,0,0,0,0,0,0,0,0}, 0, 0, "" };

// SwitchStuff
const uint8_t maxSwitches = 2;
const uint8_t upper = 0, lower = 1;

typedef struct
{
  uint8_t     switchAddr[chipAddrSize];
  uint8_t     switchData[switchDataSize];
  char        switchStatus;
  uint32_t    switchDelay;
  bool        switchDelaySet;
  char        switchName[chipNameSize+1];
}switchStruct;

const switchStruct switchClear = { {0,0,0,0,0,0,0,0}, {0,0,0,0,0,0,0,0,0,0,0,0,0}, 'F', 0, FALSE, "" };

switchStruct ds2406[maxSwitches] =
{
  { {0,0,0,0,0,0,0,0}, {0,0,0,0,0,0,0,0,0,0,0,0,0}, 'F', 0, FALSE, "" },
  { {0,0,0,0,0,0,0,0}, {0,0,0,0,0,0,0,0,0,0,0,0,0}, 'F', 0, FALSE, "" }
};

OneWire ds(oneWireAddress);  // on pin 2 (a 4.7K resistor is necessary)
Ticker  ds18; // timer to allow DS18B20 to be read
Ticker  ds2406_0;
Ticker  ds2406_1;
Ticker  update;

// mDNS stuff
// multicast DNS responder
MDNSResponder mdns;


void setup(void)
{
  // Open serial communications and wait for port to open:
  ESP.wdtDisable(); // disable the watchdog Timer
  
  Serial.begin(115200);
  delay(sDelayVal);

  setDebug |= eepromDebug; // disply eeprom statue during startup
  EEPROM.begin(EEPROMsize);
  showEEPROM();
  setDebug &= ~eepromDebug;

  Serial.print("MAC Address = ");
  uint8_t *mac = WiFi.macAddress(macAddress);
  for(uint8_t q = 0; q < macCnt; q++)
  {
    if(mac[q] < 0x10)
      Serial.print("0");
    Serial.print(mac[q], HEX);
    if(q < (macCnt - 1))
      Serial.print(":");
  }
  Serial.println();

  lcd.begin(lcdChars, lcdRows);
  lcd.clear();
  lcd.home();

  if(wifiSet != useWiFi)
  {
    uint8_t z = 0;

    for(uint8_t z = 0; z < WiFiStrCnt; z++) // clear ssid and passwd string
    {
      ssid[z]   = 0xFF;
      passwd[z] = 0xFF;
    }

    Serial.print("Enter SSID:");
    while(1)
    {
      while(Serial.available())
      {
        ssid[z] = Serial.read();
        if( (ssid[z] == 0x0A) || (ssid[z] == 0x0D) || (ssid[z] == 0x00) )
        {
          ssid[z] = 0x00;
          break;
        }
        z++;
        if(z == 30)
        {
          ssid[z] = 0x00;
          break;
        }
      }
      if(ssid[z] == 0x00)
        break;
    }
    Serial.println(ssid);

    z = 0;
    Serial.print("Enter PASSWD:");
    while(1)
    {
      while(Serial.available())
      {
        passwd[z] = Serial.read();
        if( (passwd[z] == 0x0A) || (passwd[z] == 0x0D) || (passwd[z] == 0x00) )
        {
          passwd[z] = 0x00;
          break;
        }
        z++;
        if(z == 30)
        {
          passwd[z] = 0x00;
          break;
        }
      }
      if(passwd[z] == 0x00)
        break;
    }
    Serial.println(passwd);
    wifiSet = useWiFi;
    updateEEPROM(EEWiFiSet);
  }

  if(udpSet != useUDPport)
  {
    uint8_t z = 0;

    for(uint8_t z = 0; z < udpPortCnt; z++) // clear UDP Port Address string
    {
      udpAddr[z]   = 0xFF;
    }

    Serial.print("Enter UDP Port:");
    while(1)
    {
      while(Serial.available())
      {
        udpAddr[z] = Serial.read();
        if( (udpAddr[z] == 0x0A) || (udpAddr[z] == 0x0D) || (udpAddr[z] == 0x00) )
        {
          udpAddr[z] = 0x00;
          break;
        }
        z++;
        if(z == 30)
        {
          udpAddr[z] = 0x00;
          break;
        }
      }
      if(udpAddr[z] == 0x00)
        break;
    }
    udpPort = atoi( (char *) udpAddr);
    udpSet = useUDPport;
    Serial.println(udpPort);
    updateEEPROM(EEuseUDPport);
  }

  // setting up Station AP
  WiFi.begin(ssid, passwd);
  
  // Wait for connect to AP
  Serial.print("[Connecting]");
  Serial.print(ssid);
  int tries=0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    tries++;
    if (tries > 30)
    {
      Serial.println();
      Serial.println("Unable to Connect - Check and restart");
      lcd.clear();
      lcd.home();
      lcd.print(" UNABLE TO CONNECT  ");
      lcd.setCursor(0, 1);
      lcd.print("    PLEASE CHECK    ");
      lcd.setCursor(0, 2);
      lcd.print("      AND RESET     ");
      while(1)
      {
        lcd.noDisplay();
        delay(1000);
        lcd.display();
        delay(1000);
      }
//      break;
    }
  }
  Serial.println();

  // Set up mDNS responder:
  // - first argument is the domain name, in this example
  //   the fully-qualified domain name is "esp8266.local"
  // - second argument is the IP address to advertise
  //   we send our IP address on the WiFi network
  if (!mdns.begin(mDNSdomain, WiFi.localIP()))
  {
    Serial.println("Error setting up MDNS responder!");
    while(1) { 
      delay(1000);
    }
  }
  Serial.println("mDNS responder started");


  if(setDebug > 0)
  {
    printWifiStatus();
  }

  Serial.print("Connected to wifi at IP Address: ");
  IPAddress ip = WiFi.localIP();
  Serial.println(ip);
  Serial.print("Udp server started at port at ");
  Serial.println(udpPort);
  Udp.begin(udpPort);
  findChips();
}

void loop(void)
{
  noBytes = Udp.parsePacket();

  scanChips();
  updateState();
  updateLCD();

  if ( noBytes ) 
    processUDP();

  if ( sendStatus == TRUE )
  {
    statusUpdate();
    sendStatus = FALSE;
  }

/*
  if(softReset == TRUE)
  {
    delay(2000);
    ESP.reset();
  }
*/
}


