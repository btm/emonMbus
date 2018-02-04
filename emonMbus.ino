/*
 * emoncms mbus node
 * Based on OpenEnergyMonitor code by Trystan Lea, Glyn Hudson, and others
 * https://github.com/openenergymonitor/HeatpumpMonitor
 * Copyright 2018, Bryan McLellan <btm@loftninjas.org>
 * License: GPLv3 https://www.gnu.org/licenses/gpl.txt
 */

#define DEBUG true

// Serial interface used for mbus to allow use of 8E1 encoding
#include <CustomSoftwareSerial.h>
CustomSoftwareSerial* customSerial;

#define MBUS_BAUD_RATE 2400
#define MBUS_ADDRESS 1
#define MBUS_TIMEOUT 1000 // milliseconds
#define MBUS_DATA_SIZE 255
#define MBUS_GOOD_FRAME true
#define MBUS_BAD_FRAME false
#define MBUS_RX_PIN 4
#define MBUS_TX_PIN 5

// OpenEnergyMonitor wireless communication
#define RFM69_ENABLE 1 // disable radio for mbus development without sending data
#define RF_freq RF12_433MHZ
#define RF69_COMPAT 1 // set to 1 to use RFM69CW, must be before JeeLib include
#include <JeeLib.h>   // make sure V12 (latest) is used if using RFM69CW

const int nodeID = 12;
const int networkGroup = 210;   // OpenEnergyMonitor group is 210

unsigned long loop_start = 0;
unsigned long last_loop = 0;
bool firstrun = true;

/* emonhub.conf configuration
  [[12]]
    nodename = emonmbus
    [[[rx]]]
       names = energy, flow, power, flowrate, supplyT, returnT
       scales = 1,1,1,1,0.01,0.01
       units = kW,kW,m,mm,C,C
 */
typedef struct {
  int energy, flow, power, flowrate, supplyT, returnT ;
} PayloadTX;

PayloadTX emon_data_tx;

void setup() {
  Serial.begin(115200);
  Serial.println(F("emonMbus startup"));

  Serial.print(F("RF69: node: "));
  Serial.print(nodeID);
  Serial.print(F(" networkGroup: "));
  Serial.println(networkGroup);
  if (RFM69_ENABLE) rf12_initialize(nodeID, RF_freq, networkGroup);

  Serial.println(F("mbus:"));
  Serial.print(F("  slave address: "));
  Serial.println(MBUS_ADDRESS);
  Serial.print(F("  baud rate: "));
  Serial.println(MBUS_BAUD_RATE);
  customSerial = new CustomSoftwareSerial(MBUS_RX_PIN, MBUS_TX_PIN); // rx, tx
  customSerial->begin(MBUS_BAUD_RATE, CSERIAL_8E1); // mbus uses 8E1 encoding
  delay(1000); // let the serial initialize, or we get a bad first frame
}

void loop() {
  loop_start = millis();
  
  if (RFM69_ENABLE && rf12_recvDone() && rf12_crc == 0 && (rf12_hdr & RF12_HDR_CTL) == 0)
  {
    int node_id = (rf12_hdr & 0x1F);
    byte n = rf12_len;
    if (DEBUG) {
      Serial.print(F("RFM node: "));
      Serial.print(node_id);
      Serial.print(F(" len:"));
      Serial.println(n);
    }
  }

  /************************
   * DATA COLLECTION LOOP *
   ************************/
  if ((loop_start-last_loop)>=9800 || firstrun) { // 9800 = ~10 seconds
    last_loop = loop_start; firstrun = false;

    /*************
     * MBUS DATA *
     *************/
    bool mbus_good_frame = false;
    byte mbus_data[MBUS_DATA_SIZE] = { 0 };

    if (DEBUG) Serial.print(F("mbus: requesting data from address: "));
    if (DEBUG) Serial.println(MBUS_ADDRESS);
    mbus_request_data(MBUS_ADDRESS);
    mbus_good_frame = mbus_get_response(mbus_data, sizeof(mbus_data));
    if (mbus_good_frame) {
      if (DEBUG) Serial.println(F("mbus: good frame: "));
      if (DEBUG) print_bytes(mbus_data, sizeof(mbus_data));
      
      // SMTenergy 6h 27b, SMTflowrate 3Bh 44b, SMTflowT 59h 49b, SMTreturnT 5Dh 53b, SMTflow 14h 33b, SMTheat 2Ch 39b   
      emon_data_tx.energy = get_spire_value(mbus_data, 27, 4);
      emon_data_tx.flow = get_spire_value(mbus_data, 33, 4);
      emon_data_tx.power = get_spire_value(mbus_data, 39, 3);
      emon_data_tx.flowrate = get_spire_value(mbus_data, 44, 3);
      emon_data_tx.supplyT = get_spire_value(mbus_data, 49, 2);
      emon_data_tx.returnT = get_spire_value(mbus_data, 53, 2);
      Serial.print(F("SMTenergy kWh: ")); Serial.println(emon_data_tx.energy);
      Serial.print(F("SMTflow: ")); Serial.println(emon_data_tx.flow);
      Serial.print(F("SMTpower: ")); Serial.println(emon_data_tx.power);
      Serial.print(F("SMTflowrate: ")); Serial.println(emon_data_tx.flowrate);
      Serial.print(F("SMTsupplyT: ")); Serial.println(emon_data_tx.supplyT);
      Serial.print(F("SMTreturnT: ")); Serial.println(emon_data_tx.returnT);

      send_rf_data();
    } else {
      Serial.print(F("mbus: bad frame: "));
      print_bytes(mbus_data, sizeof(mbus_data));
    }
  }
}

void send_rf_data()
{
  rf12_sleep(RF12_WAKEUP);
  rf12_sendNow(0, &emon_data_tx, sizeof(emon_data_tx));
  rf12_sendWait(2);
}
