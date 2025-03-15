#ifndef CMFA_EV_BATTERY_H
#define CMFA_EV_BATTERY_H
#include "../include.h"

#define BATTERY_SELECTED
#define MAX_PACK_VOLTAGE_DV 3040  //5000 = 500.0V
#define MIN_PACK_VOLTAGE_DV 2150
#define MAX_CELL_DEVIATION_MV 100
#define MAX_CELL_VOLTAGE_MV 4250  //Battery is put into emergency stop if one cell goes over this value
#define MIN_CELL_VOLTAGE_MV 2700  //Battery is put into emergency stop if one cell goes below this value

void setup_battery(void);
void transmit_can_frame(CAN_frame* tx_frame, int interface);

#endif
