#include "../include.h"
#ifdef TC_CHARGER
#include "../datalayer/datalayer.h"
#include "TC-CHARGER.h"

/* This implements a charger protocol for an Elcon or TC charger 
 *
 */



/* CAN cycles and timers */
// static unsigned long previousMillis10ms = 0;
static unsigned long previousMillis1s = 0;
static unsigned long last_charger_millis = -INTERVAL_5_S;

//Actual content messages
static CAN_frame TC_1806E5F4 = {
          .FD = false,
          .ext_ID = true,
          .DLC = 8,
          .ID = 0x1806E5F4,
          .data = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
                               
static CAN_frame TC_18FF50E5 = {
          .FD = false,
          .ext_ID = true,
          .DLC = 8,
          .ID = 0x18FF50E5,
          .data = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
                               

void map_can_frame_to_variable_charger(CAN_frame rx_frame) {
  // Serial.println("receive_can_charger()");
  if (millis() - last_charger_millis >= INTERVAL_5_S) {
    // Serial.println("Charger Disconnected");
    // datalayer.system.status.shore_gun_connected = 0;
  }

  switch (rx_frame.ID) {
    case 0x18FF50E5:  // This message fires once a second when charging cable is plugged in
      // Serial.println(" TC Charger online message 0x18FF50E5");
      // datalayer.system.status.shore_gun_connected = 1;
      datalayer.charger.CAN_charger_still_alive = CAN_STILL_ALIVE; 
      datalayer.charger.charger_stat_HVcur = 0;
      datalayer.charger.charger_stat_HVvol = 220;
      datalayer.charger.charger_stat_LVcur = 0;
      datalayer.charger.charger_stat_LVvol = 0;
      last_charger_millis = millis();
      break;
    default:
      break;
  }
}

void transmit_can_charger() {
      // Serial.println("TC send charger");
  unsigned long currentMillis = millis();
  uint16_t  charge_enable = 0 ; // 0:Charger is starting up to charge.1: Charger close the output. 2: Charge end.
  uint16_t  max_charger_current = floor(datalayer.charger.charger_setpoint_HV_IDC);
  uint16_t  max_charger_voltage = floor(datalayer.charger.charger_setpoint_HV_VDC); 


  /* Send keepalive with mode every 1/2s */
  if (currentMillis - previousMillis1s >= INTERVAL_500_MS) {
    previousMillis1s = currentMillis;

    TC_1806E5F4.data.u8[0] = (max_charger_voltage*10 >> 8);
    TC_1806E5F4.data.u8[1] = (max_charger_voltage*10 & 0x00FF);
    TC_1806E5F4.data.u8[2] = (max_charger_current*10 >> 8);
    TC_1806E5F4.data.u8[3] = (max_charger_current*10 & 0x00FF);
    TC_1806E5F4.data.u8[4] = 0;
    TC_1806E5F4.data.u8[5] = (int8_t)(charge_enable); // Charge Enable
    TC_1806E5F4.data.u8[6] = 0; //Operating mode
    TC_1806E5F4.data.u8[7] = 0; 

    // Serial.println("TC Charger send message Voltage: " + String(max_charger_voltage) + "   Current: " + String(max_charger_current));
    // CAN_WriteFrame_charger(&TC_1806E5F4);
    // CAN_WriteFrame_charger(&TSM_18e54024);
    transmit_can_frame(&TC_1806E5F4, can_config.charger);    
    
  }



}
#endif
