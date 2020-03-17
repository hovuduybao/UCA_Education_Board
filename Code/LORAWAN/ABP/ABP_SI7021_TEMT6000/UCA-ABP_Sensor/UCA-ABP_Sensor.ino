/*******************************************************************************
 * Copyright (c) 2015 Thomas Telkamp and Matthijs Kooijman
 *
 * Permission is hereby granted, free of charge, to anyone
 * obtaining a copy of this document and accompanying files,
 * to do whatever they want with them without any restriction,
 * including, but not limited to, copying, modification and redistribution.
 * NO WARRANTY OF ANY KIND IS PROVIDED.
 *
 * This uses ABP (Activation-by-personalisation), where a DevAddr and
 * Session keys are preconfigured (unlike OTAA, where a DevEUI and
 * application key is configured, while the DevAddr and session keys are
 * assigned/generated in the over-the-air-activation procedure).
 *
 * Do not forget to define the radio type correctly in config.h.
 *
 *******************************************************************************/

#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>
#include "LowPower.h"
//Sensors librairies
#include <SI7021.h>

#define debugSerial Serial
#define SHOW_DEBUGINFO
#define debugPrintLn(...) { if (debugSerial) debugSerial.println(__VA_ARGS__); }
#define debugPrint(...) { if (debugSerial) debugSerial.print(__VA_ARGS__); }


//Define sensor PIN
#define LAPIN A3 // PIN with Light sensor analog output 
#define LPPIN 4 // PIN with Light power input
SI7021 sensor;

// LoRaWAN end-device address (DevAddr)
static const u4_t DEVADDR = 0x260113D4;

// LoRaWAN NwkSKey, network session key
// This is the default Semtech key, which is used by the early prototype TTN
// network.
static const PROGMEM u1_t NWKSKEY[16] = { 0xBB, 0x2F, 0x98, 0x02, 0xEC, 0x0D, 0xF8, 0xD6, 0x75, 0xC9, 0x7B, 0x15, 0xCE, 0xBD, 0x2D, 0xEC };


// LoRaWAN AppSKey, application session key
// This is the default Semtech key, which is used by the early prototype TTN
// network.
static const u1_t PROGMEM APPSKEY[16] = { 0x84, 0x6C, 0xA1, 0x8E, 0x28, 0xEA, 0xDF, 0xDC, 0xF0, 0x1B, 0x5B, 0xCB, 0x37, 0x74, 0x3D, 0x61 };

// These callbacks are only used in over-the-air activation, so they are
// left empty here (we cannot leave them out completely unless
// DISABLE_JOIN is set in config.h, otherwise the linker will complain).
void os_getArtEui (u1_t* buf) { }
void os_getDevEui (u1_t* buf) { }
void os_getDevKey (u1_t* buf) { }

static osjob_t sendjob;



// global enviromental parameters
static float temp = 0.0;
//static float pressure = 0.0;
static float humidity = 0.0;
static float batvalue;
static float light;



// Schedule TX every this many seconds (might become longer due to duty
// cycle limitations).
const unsigned TX_INTERVAL = 30;

// Pin mapping
const lmic_pinmap lmic_pins = {
    .nss = 10,
    .rxtx = LMIC_UNUSED_PIN,
    .rst = 8,
    .dio = {3, 7, 6},
};



// ---------------------------------------------------------------------------------
// Functions
// ---------------------------------------------------------------------------------

extern volatile unsigned long timer0_millis;
void addMillis(unsigned long extra_millis) {
  uint8_t oldSREG = SREG;
  cli();
  timer0_millis += extra_millis;
  SREG = oldSREG;
  sei();
}

void do_sleep(unsigned int sleepyTime) {
  unsigned int eights = sleepyTime / 8;
  unsigned int fours = (sleepyTime % 8) / 4;
  unsigned int twos = ((sleepyTime % 8) % 4) / 2;
  unsigned int ones = ((sleepyTime % 8) % 4) % 2;

#ifdef SHOW_DEBUGINFO
  debugPrint(F("Sleeping for "));
  debugPrint(sleepyTime);
  debugPrint(F(" seconds = "));
  debugPrint(eights);
  debugPrint(F(" x 8 + "));
  debugPrint(fours);
  debugPrint(F(" x 4 + "));
  debugPrint(twos);
  debugPrint(F(" x 2 + "));
  debugPrintLn(ones);
  delay(500); //Wait for serial to complete
#endif


  for ( int x = 0; x < eights; x++) {
    // put the processor to sleep for 8 seconds
    LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
  }
  for ( int x = 0; x < fours; x++) {
    // put the processor to sleep for 4 seconds
    LowPower.powerDown(SLEEP_4S, ADC_OFF, BOD_OFF);
  }
  for ( int x = 0; x < twos; x++) {
    // put the processor to sleep for 2 seconds
    LowPower.powerDown(SLEEP_2S, ADC_OFF, BOD_OFF);
  }
  for ( int x = 0; x < ones; x++) {
    // put the processor to sleep for 1 seconds
    LowPower.powerDown(SLEEP_1S, ADC_OFF, BOD_OFF);
  }
  addMillis(sleepyTime * 1000);
}


long readVcc() {
  long result;
  // Read 1.1V reference against AVcc
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  delay(2); // Wait for Vref to settle
  ADCSRA |= _BV(ADSC); // Convert
  while (bit_is_set(ADCSRA,ADSC));
  result = ADCL;
  result |= ADCH<<8;
  result = 1126400L / result; // Back-calculate AVcc in mV
  return result;
}

float readLight() {
  float result;
  // Light sensor Voltage
digitalWrite(LPPIN, HIGH); // Power the sensor
delay(1);
int sensorValue = analogRead(LAPIN);
  // Convert the analog reading (which goes from 0 - 1023) to a voltage (0 - 3.3V):
  float voltage = sensorValue * (batvalue / 1023.0)/100; // Batvalue is in tens of mV, so the result has to be divided by 100
result = voltage*200; // multiply by 2000 to have Lx
digitalWrite(LPPIN, LOW); // switch off the sensor
  return result;
}

void updateEnvParameters()
{
 // get humidity and temperature in one shot, saves power because sensor takes temperature when doing humidity anyway
  si7021_env data = sensor.getHumidityAndTemperature();
  
  temp = data.celsiusHundredths;
  humidity = data.humidityBasisPoints;
  light = readLight();
  batvalue = (int)(readVcc()/10);  // readVCC returns in tens of mVolt 
    

  #ifdef SHOW_DEBUGINFO
  // print out the value you read:
  Serial.print("Humidity : ");
  Serial.println(humidity);
  Serial.print("T°c : ");
  Serial.println(temp);
  Serial.print("Vbatt : ");
  Serial.println(batvalue);
  #endif
  
}




void onEvent (ev_t ev) {
    Serial.print(os_getTime());
    Serial.print(": ");
    switch(ev) {
        case EV_SCAN_TIMEOUT:
            Serial.println(F("EV_SCAN_TIMEOUT"));
            break;
        case EV_BEACON_FOUND:
            Serial.println(F("EV_BEACON_FOUND"));
            break;
        case EV_BEACON_MISSED:
            Serial.println(F("EV_BEACON_MISSED"));
            break;
        case EV_BEACON_TRACKED:
            Serial.println(F("EV_BEACON_TRACKED"));
            break;
        case EV_JOINING:
            Serial.println(F("EV_JOINING"));
            break;
        case EV_JOINED:
            Serial.println(F("EV_JOINED"));
            break;
        case EV_RFU1:
            Serial.println(F("EV_RFU1"));
            break;
        case EV_JOIN_FAILED:
            Serial.println(F("EV_JOIN_FAILED"));
            break;
        case EV_REJOIN_FAILED:
            Serial.println(F("EV_REJOIN_FAILED"));
            break;
        case EV_TXCOMPLETE:
            Serial.println(F("EV_TXCOMPLETE (includes waiting for RX windows)"));
            if (LMIC.txrxFlags & TXRX_ACK)
              Serial.println(F("Received ack"));
            if (LMIC.dataLen) {
              Serial.println(F("Received "));
              Serial.println(LMIC.dataLen);
              Serial.println(F(" bytes of payload"));
              for (int i = 0; i < LMIC.dataLen; i++) {
              if (LMIC.frame[LMIC.dataBeg + i] < 0x10) {
              Serial.print(F("0"));
              }
              Serial.print(LMIC.frame[LMIC.dataBeg + i], HEX);
              }
            }
            // Schedule next transmission
            do_sleep(TX_INTERVAL);
            //os_setTimedCallback(&sendjob, os_getTime()+sec2osticks(TX_INTERVAL), do_send);
            os_setCallback(&sendjob, do_send);
            break;
        case EV_LOST_TSYNC:
            Serial.println(F("EV_LOST_TSYNC"));
            break;
        case EV_RESET:
            Serial.println(F("EV_RESET"));
            break;
        case EV_RXCOMPLETE:
            // data received in ping slot
            Serial.println(F("EV_RXCOMPLETE"));
            break;
        case EV_LINK_DEAD:
            Serial.println(F("EV_LINK_DEAD"));
            break;
        case EV_LINK_ALIVE:
            Serial.println(F("EV_LINK_ALIVE"));
            break;
         default:
            Serial.println(F("Unknown event"));
            break;
    }
}

void do_send(osjob_t* j){
    // Check if there is not a current TX/RX job running
    if (LMIC.opmode & OP_TXRXPEND) {
        Serial.println(F("OP_TXRXPEND, not sending"));
    } else {
        

 updateEnvParameters();
       

#ifdef SHOW_DEBUGINFO
    debugPrint(F("T="));
    debugPrintLn(temp);

    debugPrint(F("H="));
    debugPrintLn(humidity);
    debugPrint(F("L="));
    debugPrintLn(light);
    debugPrint(F("BV="));
    debugPrintLn(batvalue);
#endif
    int t = (int)((temp) / 10.0);
    int h = (int)(humidity / 50.0);
    int bat = batvalue; // multifly by 10 for V in Cayenne
    int l = light; // light sensor in Lx

    unsigned char mydata[15];
    mydata[0] = 0x1; // CH1
    mydata[1] = 0x67; // Temp
    mydata[2] = t >> 8;
    mydata[3] = t & 0xFF;
    mydata[4] = 0x2; // CH2
    mydata[5] = 0x68; // Humidity
    mydata[6] = h & 0xFF;
    mydata[7] = 0x3; // CH3
    mydata[8] = 0x2; // Analog output
    mydata[9] = bat >> 8;
    mydata[10] = bat & 0xFF;
    mydata[11] = 0x4; // CH4
    mydata[12] = 0x65; // Luminosity
    mydata[13] = l >> 8;
    mydata[14] = l & 0xFF;
    
    LMIC_setTxData2(1, mydata, sizeof(mydata), 0);
    debugPrintLn(F("PQ")); //Packet queued

    Serial.println(F("Packet queued"));

        
    }
    // Next TX is scheduled after TX_COMPLETE event.
}

void setup() {
    Serial.begin(115200);
    Serial.println(F("Starting"));

    #ifdef SHOW_DEBUGINFO
  debugPrintLn(F("Starting"));
  delay(100);
  #endif
  
  Wire.begin();
  sensor.begin();

  updateEnvParameters(); // To have value for the first Tx
  

    // LMIC init
    os_init();
    // Reset the MAC state. Session and pending data transfers will be discarded.
    LMIC_reset();

    // Set static session parameters. Instead of dynamically establishing a session
    // by joining the network, precomputed session parameters are be provided.
    #ifdef PROGMEM
    // On AVR, these values are stored in flash and only copied to RAM
    // once. Copy them to a temporary buffer here, LMIC_setSession will
    // copy them into a buffer of its own again.
    uint8_t appskey[sizeof(APPSKEY)];
    uint8_t nwkskey[sizeof(NWKSKEY)];
    memcpy_P(appskey, APPSKEY, sizeof(APPSKEY));
    memcpy_P(nwkskey, NWKSKEY, sizeof(NWKSKEY));
    LMIC_setSession (0x1, DEVADDR, nwkskey, appskey);
    #else
    // If not running an AVR with PROGMEM, just use the arrays directly
    LMIC_setSession (0x1, DEVADDR, NWKSKEY, APPSKEY);
    #endif

    // Disable link check validation
    LMIC_setLinkCheckMode(0);

    // TTN uses SF9 for its RX2 window.
    LMIC.dn2Dr = DR_SF9;

    // Set data rate and transmit power for uplink (note: txpow seems to be ignored by the library)
    LMIC_setDrTxpow(DR_SF7,14);

    // Start job
    do_send(&sendjob);
}

void loop() {
    os_runloop_once();
}
