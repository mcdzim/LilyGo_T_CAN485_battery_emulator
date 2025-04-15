#include "../include.h"
#ifdef BYD_ATTO_3_BATTERY
#include "../datalayer/datalayer.h"
#include "../datalayer/datalayer_extended.h"
#include "../devboard/utils/events.h"
#include "BYD-ATTO-3-BATTERY.h"

/* Notes
  - SOC% by default is now ESTIMATED.
  - If you have a non-crashed pack, enable using real SOC. See Wiki for info.
  - TODO: In the future, we might be able to unlock crashed batteries and get SOC going always
*/

/* Do not change code below unless you are sure what you are doing */
#define NOT_DETERMINED_YET 0
#define STANDARD_RANGE 1
#define EXTENDED_RANGE 2
static uint8_t battery_type = NOT_DETERMINED_YET;
static unsigned long previousMillis50 = 0;   // will store last time a 50ms CAN Message was send
static unsigned long previousMillis100 = 0;  // will store last time a 100ms CAN Message was send
static unsigned long previousMillis500 = 0;  // will store last time a 500ms CAN Message was send
static bool SOC_method = false;
static uint8_t counter_50ms = 0;
static uint8_t counter_100ms = 0;
static uint8_t frame6_counter = 0xB;
static uint8_t frame7_counter = 0x5;
static uint16_t battery_voltage = 0;
static int16_t battery_temperature_ambient = 0;
static int16_t battery_daughterboard_temperatures[10];
static int16_t battery_lowest_temperature = 0;
static int16_t battery_highest_temperature = 0;
static int16_t battery_calc_min_temperature = 0;
static int16_t battery_calc_max_temperature = 0;
static uint16_t battery_highprecision_SOC = 0;
static uint16_t BMS_SOC = 0;
static uint16_t BMS_voltage = 0;
static int16_t BMS_current = 0;
static int16_t BMS_lowest_cell_temperature = 0;
static int16_t BMS_highest_cell_temperature = 0;
static int16_t BMS_average_cell_temperature = 0;
static uint16_t BMS_lowest_cell_voltage_mV = 3300;
static uint16_t BMS_highest_cell_voltage_mV = 3300;
static uint8_t battery_frame_index = 0;
static uint16_t battery_cellvoltages[CELLCOUNT_EXTENDED] = {0};
#ifdef DOUBLE_BATTERY
static int16_t battery2_temperature_ambient = 0;
static int16_t battery2_daughterboard_temperatures[10];
static int16_t battery2_lowest_temperature = 0;
static int16_t battery2_highest_temperature = 0;
static int16_t battery2_calc_min_temperature = 0;
static int16_t battery2_calc_max_temperature = 0;
static uint16_t battery2_highprecision_SOC = 0;
static uint16_t BMS2_SOC = 0;
static uint16_t BMS2_voltage = 0;
static int16_t BMS2_current = 0;
static int16_t BMS2_lowest_cell_temperature = 0;
static int16_t BMS2_highest_cell_temperature = 0;
static int16_t BMS2_average_cell_temperature = 0;
static uint16_t BMS2_lowest_cell_voltage_mV = 3300;
static uint16_t BMS2_highest_cell_voltage_mV = 3300;
static uint8_t battery2_frame_index = 0;
static uint16_t battery2_cellvoltages[CELLCOUNT_EXTENDED] = {0};
#endif  //DOUBLE_BATTERY
#define POLL_FOR_BATTERY_SOC 0x05
#define POLL_FOR_BATTERY_VOLTAGE 0x08
#define POLL_FOR_BATTERY_CURRENT 0x09
#define POLL_FOR_LOWEST_TEMP_CELL 0x2f
#define POLL_FOR_HIGHEST_TEMP_CELL 0x31
#define POLL_FOR_BATTERY_PACK_AVG_TEMP 0x32
#define POLL_FOR_BATTERY_CELL_MV_MAX 0x2D
#define POLL_FOR_BATTERY_CELL_MV_MIN 0x2B
#define UNKNOWN_POLL_1 0xFC
#define ESTIMATED 0
#define MEASURED 1
static uint16_t poll_state = POLL_FOR_BATTERY_SOC;

CAN_frame ATTO_3_12D = {.FD = false,
                        .ext_ID = false,
                        .DLC = 8,
                        .ID = 0x12D,
                        .data = {0xA0, 0x28, 0x02, 0xA0, 0x0C, 0x71, 0xCF, 0x49}};
CAN_frame ATTO_3_441 = {.FD = false,
                        .ext_ID = false,
                        .DLC = 8,
                        .ID = 0x441,
                        .data = {0x98, 0x3A, 0x88, 0x13, 0x07, 0x00, 0xFF, 0x8C}};
CAN_frame ATTO_3_7E7_POLL = {.FD = false,
                             .ext_ID = false,
                             .DLC = 8,
                             .ID = 0x7E7,  //Poll PID 03 22 00 05 (POLL_FOR_BATTERY_SOC)
                             .data = {0x03, 0x22, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00}};

// Define the data points for %SOC depending on pack voltage
const uint8_t numPoints = 14;
const uint16_t SOC[numPoints] = {10000, 9970, 9490, 8470, 7750, 6790, 5500, 4900, 3910, 3000, 2280, 1600, 480, 0};
const uint16_t voltage_extended[numPoints] = {4400, 4230, 4180, 4171, 4169, 4160, 4130,
                                              4121, 4119, 4100, 4070, 4030, 3950, 3800};
const uint16_t voltage_standard[numPoints] = {3620, 3485, 3443, 3435, 3433, 3425, 3400,
                                              3392, 3390, 3375, 3350, 3315, 3250, 3140};

uint16_t estimateSOCextended(uint16_t packVoltage) {  // Linear interpolation function
  if (packVoltage >= voltage_extended[0]) {
    return SOC[0];
  }
  if (packVoltage <= voltage_extended[numPoints - 1]) {
    return SOC[numPoints - 1];
  }

  for (int i = 1; i < numPoints; ++i) {
    if (packVoltage >= voltage_extended[i]) {
      double t = (packVoltage - voltage_extended[i]) / (voltage_extended[i - 1] - voltage_extended[i]);
      return SOC[i] + t * (SOC[i - 1] - SOC[i]);
    }
  }
  return 0;  // Default return for safety, should never reach here
}

uint16_t estimateSOCstandard(uint16_t packVoltage) {  // Linear interpolation function
  if (packVoltage >= voltage_standard[0]) {
    return SOC[0];
  }
  if (packVoltage <= voltage_standard[numPoints - 1]) {
    return SOC[numPoints - 1];
  }

  for (int i = 1; i < numPoints; ++i) {
    if (packVoltage >= voltage_standard[i]) {
      double t = (packVoltage - voltage_standard[i]) / (voltage_standard[i - 1] - voltage_standard[i]);
      return SOC[i] + t * (SOC[i - 1] - SOC[i]);
    }
  }
  return 0;  // Default return for safety, should never reach here
}

void update_values_battery() {  //This function maps all the values fetched via CAN to the correct parameters used for modbus

  if (BMS_voltage > 0) {
    datalayer.battery.status.voltage_dV = BMS_voltage * 10;
  }

#ifdef USE_ESTIMATED_SOC
  // When the battery is crashed hard, it locks itself and SOC becomes unavailable.
  // We instead estimate the SOC% based on the battery voltage.
  // This is a bad solution, you wont be able to use 100% of the battery
  if (battery_type == EXTENDED_RANGE) {
    datalayer.battery.status.real_soc = estimateSOCextended(datalayer.battery.status.voltage_dV);
  }
  if (battery_type == STANDARD_RANGE) {
    datalayer.battery.status.real_soc = estimateSOCstandard(datalayer.battery.status.voltage_dV);
  }
  SOC_method = ESTIMATED;
#else  // Pack is not crashed, we can use periodically transmitted SOC
  datalayer.battery.status.real_soc = battery_highprecision_SOC * 10;
  SOC_method = MEASURED;
#endif

  datalayer.battery.status.current_dA = -BMS_current;

  datalayer.battery.status.remaining_capacity_Wh = static_cast<uint32_t>(
      (static_cast<double>(datalayer.battery.status.real_soc) / 10000) * datalayer.battery.info.total_capacity_Wh);

  datalayer.battery.status.max_discharge_power_W = MAXPOWER_DISCHARGE_W;  //TODO: Map from CAN later on

  datalayer.battery.status.max_charge_power_W = MAXPOWER_CHARGE_W;  //TODO: Map from CAN later on

  datalayer.battery.status.cell_max_voltage_mV = BMS_highest_cell_voltage_mV;

  datalayer.battery.status.cell_min_voltage_mV = BMS_lowest_cell_voltage_mV;

  //Map all cell voltages to the global array
  memcpy(datalayer.battery.status.cell_voltages_mV, battery_cellvoltages, CELLCOUNT_EXTENDED * sizeof(uint16_t));

  // Check if we are on Standard range or Extended range battery.
  // We use a variety of checks to ensure we catch a potential Standard range battery
  if ((battery_cellvoltages[125] > 0) && (battery_type == NOT_DETERMINED_YET)) {
    battery_type = EXTENDED_RANGE;
  }
  if ((battery_cellvoltages[104] == 4095) && (battery_type == NOT_DETERMINED_YET)) {
    battery_type = STANDARD_RANGE;  //This cell reading is always 4095 on Standard range
  }
  if ((battery_daughterboard_temperatures[9] == 215) && (battery_type == NOT_DETERMINED_YET)) {
    battery_type = STANDARD_RANGE;  //Sensor 10 is missing on Standard range
  }
  if ((battery_daughterboard_temperatures[8] == 215) && (battery_type == NOT_DETERMINED_YET)) {
    battery_type = STANDARD_RANGE;  //Sensor 9 is missing on Standard range
  }

  switch (battery_type) {
    case STANDARD_RANGE:
      datalayer.battery.info.total_capacity_Wh = 50000;
      datalayer.battery.info.number_of_cells = CELLCOUNT_STANDARD;
      datalayer.battery.info.max_design_voltage_dV = MAX_PACK_VOLTAGE_STANDARD_DV;
      datalayer.battery.info.min_design_voltage_dV = MIN_PACK_VOLTAGE_STANDARD_DV;
      break;
    case EXTENDED_RANGE:
      datalayer.battery.info.total_capacity_Wh = 60000;
      datalayer.battery.info.number_of_cells = CELLCOUNT_EXTENDED;
      datalayer.battery.info.max_design_voltage_dV = MAX_PACK_VOLTAGE_EXTENDED_DV;
      datalayer.battery.info.min_design_voltage_dV = MIN_PACK_VOLTAGE_EXTENDED_DV;
      break;
    case NOT_DETERMINED_YET:
    default:
      //Do nothing
      break;
  }

#ifdef SKIP_TEMPERATURE_SENSOR_NUMBER
  // Initialize min and max variables for temperature calculation
  battery_calc_min_temperature = battery_daughterboard_temperatures[0];
  battery_calc_max_temperature = battery_daughterboard_temperatures[0];

  // Loop through the array of 10x daughterboard temps to find the smallest and largest values
  // Note, it is possible for user to skip using a faulty sensor in the .h file
  if (SKIP_TEMPERATURE_SENSOR_NUMBER == 1) {  //If sensor 1 is skipped, init minmax to sensor 2
    battery_calc_min_temperature = battery_daughterboard_temperatures[1];
    battery_calc_max_temperature = battery_daughterboard_temperatures[1];
  }
  for (int i = 1; i < 10; i++) {
    if (i == (SKIP_TEMPERATURE_SENSOR_NUMBER - 1)) {
      i++;
    }
    if (battery_daughterboard_temperatures[i] < battery_calc_min_temperature) {
      battery_calc_min_temperature = battery_daughterboard_temperatures[i];
    }
    if (battery_daughterboard_temperatures[i] > battery_calc_max_temperature) {
      battery_calc_max_temperature = battery_daughterboard_temperatures[i];
    }
  }
  //Write the result to datalayer
  datalayer.battery.status.temperature_min_dC = battery_calc_min_temperature * 10;
  datalayer.battery.status.temperature_max_dC = battery_calc_max_temperature * 10;
#else   //User does not need filtering out a broken sensor, just use the min-max the BMS sends
  datalayer.battery.status.temperature_min_dC = BMS_lowest_cell_temperature * 10;
  datalayer.battery.status.temperature_max_dC = BMS_highest_cell_temperature * 10;
#endif  //!SKIP_TEMPERATURE_SENSOR_NUMBER

  // Update webserver datalayer
  datalayer_extended.bydAtto3.SOC_method = SOC_method;
  datalayer_extended.bydAtto3.SOC_estimated = datalayer.battery.status.real_soc;
  //Once we implement switching logic, remember to change from where the estimated is taken
  datalayer_extended.bydAtto3.SOC_highprec = battery_highprecision_SOC;
  datalayer_extended.bydAtto3.SOC_polled = BMS_SOC;
  datalayer_extended.bydAtto3.voltage_periodic = battery_voltage;
  datalayer_extended.bydAtto3.voltage_polled = BMS_voltage;
  datalayer_extended.bydAtto3.battery_temperatures[0] = battery_daughterboard_temperatures[0];
  datalayer_extended.bydAtto3.battery_temperatures[1] = battery_daughterboard_temperatures[1];
  datalayer_extended.bydAtto3.battery_temperatures[2] = battery_daughterboard_temperatures[2];
  datalayer_extended.bydAtto3.battery_temperatures[3] = battery_daughterboard_temperatures[3];
  datalayer_extended.bydAtto3.battery_temperatures[4] = battery_daughterboard_temperatures[4];
  datalayer_extended.bydAtto3.battery_temperatures[5] = battery_daughterboard_temperatures[5];
  datalayer_extended.bydAtto3.battery_temperatures[6] = battery_daughterboard_temperatures[6];
  datalayer_extended.bydAtto3.battery_temperatures[7] = battery_daughterboard_temperatures[7];
  datalayer_extended.bydAtto3.battery_temperatures[8] = battery_daughterboard_temperatures[8];
  datalayer_extended.bydAtto3.battery_temperatures[9] = battery_daughterboard_temperatures[9];
}

void handle_incoming_can_frame_battery(CAN_frame rx_frame) {
  switch (rx_frame.ID) {
    case 0x244:
      datalayer.battery.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x245:
      datalayer.battery.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      if (rx_frame.data.u8[0] == 0x01) {
        battery_temperature_ambient = (rx_frame.data.u8[4] - 40);
      }
      break;
    case 0x286:
      datalayer.battery.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x334:
      datalayer.battery.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x338:
      datalayer.battery.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x344:
      datalayer.battery.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x345:
      datalayer.battery.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x347:
      datalayer.battery.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x34A:
      datalayer.battery.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x35E:
      datalayer.battery.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x360:
      datalayer.battery.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x36C:
      datalayer.battery.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x438:
      datalayer.battery.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x43A:
      datalayer.battery.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x43B:
      datalayer.battery.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x43C:
      if (rx_frame.data.u8[0] == 0x00) {
        battery_daughterboard_temperatures[0] = (rx_frame.data.u8[1] - 40);
        battery_daughterboard_temperatures[1] = (rx_frame.data.u8[2] - 40);
        battery_daughterboard_temperatures[2] = (rx_frame.data.u8[3] - 40);
        battery_daughterboard_temperatures[3] = (rx_frame.data.u8[4] - 40);
        battery_daughterboard_temperatures[4] = (rx_frame.data.u8[5] - 40);
        battery_daughterboard_temperatures[5] = (rx_frame.data.u8[6] - 40);
      }
      if (rx_frame.data.u8[0] == 0x01) {
        battery_daughterboard_temperatures[6] = (rx_frame.data.u8[1] - 40);
        battery_daughterboard_temperatures[7] = (rx_frame.data.u8[2] - 40);
        battery_daughterboard_temperatures[8] = (rx_frame.data.u8[3] - 40);
        battery_daughterboard_temperatures[9] = (rx_frame.data.u8[4] - 40);
      }
      break;
    case 0x43D:
      datalayer.battery.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      battery_frame_index = rx_frame.data.u8[0];

      if (battery_frame_index < (CELLCOUNT_EXTENDED / 3)) {
        uint8_t base_index = battery_frame_index * 3;
        for (uint8_t i = 0; i < 3; i++) {
          battery_cellvoltages[base_index + i] =
              (((rx_frame.data.u8[2 * (i + 1)] & 0x0F) << 8) | rx_frame.data.u8[2 * i + 1]);
        }
      }
      break;
    case 0x444:
      datalayer.battery.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      battery_voltage = ((rx_frame.data.u8[1] & 0x0F) << 8) | rx_frame.data.u8[0];
      //battery_temperature_something = rx_frame.data.u8[7] - 40; resides in frame 7
      break;
    case 0x445:
      datalayer.battery.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x446:
      datalayer.battery.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x447:
      datalayer.battery.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      battery_highprecision_SOC = ((rx_frame.data.u8[5] & 0x0F) << 8) | rx_frame.data.u8[4];  // 03 E0 = 992 = 99.2%
      battery_lowest_temperature = (rx_frame.data.u8[1] - 40);                                //Best guess for now
      battery_highest_temperature = (rx_frame.data.u8[3] - 40);                               //Best guess for now
      break;
    case 0x47B:
      datalayer.battery.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x524:
      datalayer.battery.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x7EF:  //OBD2 PID reply from battery
      switch (rx_frame.data.u8[3]) {
        case POLL_FOR_BATTERY_SOC:
          BMS_SOC = rx_frame.data.u8[4];
          break;
        case POLL_FOR_BATTERY_VOLTAGE:
          BMS_voltage = (rx_frame.data.u8[5] << 8) | rx_frame.data.u8[4];
          break;
        case POLL_FOR_BATTERY_CURRENT:
          BMS_current = ((rx_frame.data.u8[5] << 8) | rx_frame.data.u8[4]) - 5000;
          break;
        case POLL_FOR_LOWEST_TEMP_CELL:
          BMS_lowest_cell_temperature = (rx_frame.data.u8[4] - 40);
          break;
        case POLL_FOR_HIGHEST_TEMP_CELL:
          BMS_highest_cell_temperature = (rx_frame.data.u8[4] - 40);
          break;
        case POLL_FOR_BATTERY_PACK_AVG_TEMP:
          BMS_average_cell_temperature = (rx_frame.data.u8[4] - 40);
          break;
        case POLL_FOR_BATTERY_CELL_MV_MAX:
          BMS_highest_cell_voltage_mV = (rx_frame.data.u8[5] << 8) | rx_frame.data.u8[4];
          break;
        case POLL_FOR_BATTERY_CELL_MV_MIN:
          BMS_lowest_cell_voltage_mV = (rx_frame.data.u8[5] << 8) | rx_frame.data.u8[4];
          break;
        default:  //Unrecognized reply
          break;
      }
      break;
    default:
      break;
  }
}
void transmit_can_battery() {
  unsigned long currentMillis = millis();
  //Send 50ms message
  if (currentMillis - previousMillis50 >= INTERVAL_50_MS) {
    // Check if sending of CAN messages has been delayed too much.
    if ((currentMillis - previousMillis50 >= INTERVAL_50_MS_DELAYED) && (currentMillis > BOOTUP_TIME)) {
      set_event(EVENT_CAN_OVERRUN, (currentMillis - previousMillis50));
    } else {
      clear_event(EVENT_CAN_OVERRUN);
    }
    previousMillis50 = currentMillis;

    // Set close contactors to allowed (Useful for crashed packs, started via contactor control thru GPIO)
    if (datalayer.battery.status.bms_status == ACTIVE) {
      datalayer.system.status.battery_allows_contactor_closing = true;
    } else {  // Fault state, open contactors!
      datalayer.system.status.battery_allows_contactor_closing = false;
    }

    counter_50ms++;
    if (counter_50ms > 23) {
      ATTO_3_12D.data.u8[2] = 0x00;  // Goes from 02->00
      ATTO_3_12D.data.u8[3] = 0x22;  // Goes from A0->22
      ATTO_3_12D.data.u8[5] = 0x31;  // Goes from 71->31
    }

    // Update the counters in frame 6 & 7 (they are not in sync)
    if (frame6_counter == 0x0) {
      frame6_counter = 0xF;  // Reset to 0xF after reaching 0x0
    } else {
      frame6_counter--;  // Decrement the counter
    }
    if (frame7_counter == 0x0) {
      frame7_counter = 0xF;  // Reset to 0xF after reaching 0x0
    } else {
      frame7_counter--;  // Decrement the counter
    }

    ATTO_3_12D.data.u8[6] = (0x0F | (frame6_counter << 4));
    ATTO_3_12D.data.u8[7] = (0x09 | (frame7_counter << 4));

    transmit_can_frame(&ATTO_3_12D, can_config.battery);
#ifdef DOUBLE_BATTERY
    transmit_can_frame(&ATTO_3_12D, can_config.battery_double);
#endif  //DOUBLE_BATTERY
  }
  // Send 100ms CAN Message
  if (currentMillis - previousMillis100 >= INTERVAL_100_MS) {
    previousMillis100 = currentMillis;

    if (counter_100ms < 100) {
      counter_100ms++;
    }

    if (counter_100ms > 3) {

      ATTO_3_441.data.u8[4] = 0x9D;
      ATTO_3_441.data.u8[5] = 0x01;
      ATTO_3_441.data.u8[6] = 0xFF;
      ATTO_3_441.data.u8[7] = 0xF5;
    }

    transmit_can_frame(&ATTO_3_441, can_config.battery);
#ifdef DOUBLE_BATTERY
    transmit_can_frame(&ATTO_3_441, can_config.battery_double);
#endif  //DOUBLE_BATTERY
  }
  // Send 500ms CAN Message
  if (currentMillis - previousMillis500 >= INTERVAL_500_MS) {
    previousMillis500 = currentMillis;

    switch (poll_state) {
      case POLL_FOR_BATTERY_SOC:
        ATTO_3_7E7_POLL.data.u8[3] = POLL_FOR_BATTERY_SOC;
        poll_state = POLL_FOR_BATTERY_VOLTAGE;
        break;
      case POLL_FOR_BATTERY_VOLTAGE:
        ATTO_3_7E7_POLL.data.u8[3] = POLL_FOR_BATTERY_VOLTAGE;
        poll_state = POLL_FOR_BATTERY_CURRENT;
        break;
      case POLL_FOR_BATTERY_CURRENT:
        ATTO_3_7E7_POLL.data.u8[3] = POLL_FOR_BATTERY_CURRENT;
        poll_state = POLL_FOR_LOWEST_TEMP_CELL;
        break;
      case POLL_FOR_LOWEST_TEMP_CELL:
        ATTO_3_7E7_POLL.data.u8[3] = POLL_FOR_LOWEST_TEMP_CELL;
        poll_state = POLL_FOR_HIGHEST_TEMP_CELL;
        break;
      case POLL_FOR_HIGHEST_TEMP_CELL:
        ATTO_3_7E7_POLL.data.u8[3] = POLL_FOR_HIGHEST_TEMP_CELL;
        poll_state = POLL_FOR_BATTERY_PACK_AVG_TEMP;
        break;
      case POLL_FOR_BATTERY_PACK_AVG_TEMP:
        ATTO_3_7E7_POLL.data.u8[3] = POLL_FOR_BATTERY_PACK_AVG_TEMP;
        poll_state = POLL_FOR_BATTERY_CELL_MV_MAX;
        break;
      case POLL_FOR_BATTERY_CELL_MV_MAX:
        ATTO_3_7E7_POLL.data.u8[3] = POLL_FOR_BATTERY_CELL_MV_MAX;
        poll_state = POLL_FOR_BATTERY_CELL_MV_MIN;
        break;
      case POLL_FOR_BATTERY_CELL_MV_MIN:
        ATTO_3_7E7_POLL.data.u8[3] = POLL_FOR_BATTERY_CELL_MV_MIN;
        poll_state = POLL_FOR_BATTERY_SOC;
        break;
      default:
        poll_state = POLL_FOR_BATTERY_SOC;
        break;
    }

    transmit_can_frame(&ATTO_3_7E7_POLL, can_config.battery);
#ifdef DOUBLE_BATTERY
    transmit_can_frame(&ATTO_3_7E7_POLL, can_config.battery_double);
#endif  //DOUBLE_BATTERY
  }
}

void setup_battery(void) {  // Performs one time setup at startup
  strncpy(datalayer.system.info.battery_protocol, "BYD Atto 3", 63);
  datalayer.system.info.battery_protocol[63] = '\0';
  datalayer.battery.info.chemistry = battery_chemistry_enum::LFP;
  datalayer.battery.info.max_design_voltage_dV = MAX_PACK_VOLTAGE_EXTENDED_DV;  //Startup in extremes
  datalayer.battery.info.min_design_voltage_dV = MIN_PACK_VOLTAGE_STANDARD_DV;  //We later determine range
  datalayer.battery.info.max_cell_voltage_mV = MAX_CELL_VOLTAGE_MV;
  datalayer.battery.info.min_cell_voltage_mV = MIN_CELL_VOLTAGE_MV;
  datalayer.battery.info.max_cell_voltage_deviation_mV = MAX_CELL_DEVIATION_MV;
#ifdef DOUBLE_BATTERY
  datalayer.battery2.info.number_of_cells = CELLCOUNT_STANDARD;
  datalayer.battery2.info.chemistry = battery_chemistry_enum::LFP;
  datalayer.battery2.info.max_design_voltage_dV = datalayer.battery.info.max_design_voltage_dV;
  datalayer.battery2.info.min_design_voltage_dV = datalayer.battery.info.min_design_voltage_dV;
  datalayer.battery2.info.max_cell_voltage_mV = datalayer.battery.info.max_cell_voltage_mV;
  datalayer.battery2.info.min_cell_voltage_mV = datalayer.battery.info.min_cell_voltage_mV;
  datalayer.battery2.info.max_cell_voltage_deviation_mV = MAX_CELL_DEVIATION_MV;
#endif  //DOUBLE_BATTERY
}

#ifdef DOUBLE_BATTERY

void update_values_battery2() {  //This function maps all the values fetched via CAN to the correct parameters used for modbus

  if (BMS2_voltage > 0) {
    datalayer.battery2.status.voltage_dV = BMS2_voltage * 10;
  }

  // We instead estimate the SOC% based on the battery2 voltage
  // This is a very bad solution, and as soon as an usable SOC% value has been found on CAN, we should switch to that!
  if (battery_type == EXTENDED_RANGE) {
    datalayer.battery2.status.real_soc = estimateSOCextended(datalayer.battery2.status.voltage_dV);
  }
  if (battery_type == STANDARD_RANGE) {
    datalayer.battery2.status.real_soc = estimateSOCstandard(datalayer.battery2.status.voltage_dV);
  }

  datalayer.battery2.status.current_dA = -BMS2_current;

  datalayer.battery2.status.remaining_capacity_Wh = static_cast<uint32_t>(
      (static_cast<double>(datalayer.battery2.status.real_soc) / 10000) * datalayer.battery2.info.total_capacity_Wh);

  datalayer.battery2.status.max_discharge_power_W = 10000;  //TODO: Map from CAN later on

  datalayer.battery2.status.max_charge_power_W = 10000;  //TODO: Map from CAN later on

  datalayer.battery2.status.cell_max_voltage_mV = BMS2_highest_cell_voltage_mV;

  datalayer.battery2.status.cell_min_voltage_mV = BMS2_lowest_cell_voltage_mV;

  datalayer.battery2.status.temperature_min_dC = BMS2_lowest_cell_temperature * 10;  // Add decimals

  datalayer.battery2.status.temperature_max_dC = BMS2_highest_cell_temperature * 10;

  //Map all cell voltages to the global array
  memcpy(datalayer.battery2.status.cell_voltages_mV, battery2_cellvoltages, CELLCOUNT_EXTENDED * sizeof(uint16_t));

  datalayer.battery2.info.total_capacity_Wh = datalayer.battery.info.total_capacity_Wh;
  datalayer.battery2.info.number_of_cells = datalayer.battery.info.number_of_cells;
  datalayer.battery2.info.max_design_voltage_dV = datalayer.battery.info.max_design_voltage_dV;
  datalayer.battery2.info.min_design_voltage_dV = datalayer.battery.info.min_design_voltage_dV;
}

void handle_incoming_can_frame_battery2(CAN_frame rx_frame) {
  switch (rx_frame.ID) {
    case 0x244:
      datalayer.battery2.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x245:
      datalayer.battery2.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      if (rx_frame.data.u8[0] == 0x01) {
        battery2_temperature_ambient = (rx_frame.data.u8[4] - 40);
      }
      break;
    case 0x286:
      datalayer.battery2.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x334:
      datalayer.battery2.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x338:
      datalayer.battery2.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x344:
      datalayer.battery2.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x345:
      datalayer.battery2.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x347:
      datalayer.battery2.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x34A:
      datalayer.battery2.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x35E:
      datalayer.battery2.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x360:
      datalayer.battery2.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x36C:
      datalayer.battery2.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x438:
      datalayer.battery2.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x43A:
      datalayer.battery2.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x43B:
      datalayer.battery2.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x43C:  // Daughterboard temperatures reside in this CAN message
      datalayer.battery2.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      if (rx_frame.data.u8[0] == 0x00) {
        battery2_daughterboard_temperatures[0] = (rx_frame.data.u8[1] - 40);
        battery2_daughterboard_temperatures[1] = (rx_frame.data.u8[2] - 40);
        battery2_daughterboard_temperatures[2] = (rx_frame.data.u8[3] - 40);
        battery2_daughterboard_temperatures[3] = (rx_frame.data.u8[4] - 40);
        battery2_daughterboard_temperatures[4] = (rx_frame.data.u8[5] - 40);
        battery2_daughterboard_temperatures[5] = (rx_frame.data.u8[6] - 40);
      }
      if (rx_frame.data.u8[0] == 0x01) {
        battery2_daughterboard_temperatures[6] = (rx_frame.data.u8[1] - 40);
        battery2_daughterboard_temperatures[7] = (rx_frame.data.u8[2] - 40);
        battery2_daughterboard_temperatures[8] = (rx_frame.data.u8[3] - 40);
        battery2_daughterboard_temperatures[9] = (rx_frame.data.u8[4] - 40);
      }
      break;
    case 0x43D:
      datalayer.battery2.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      battery2_frame_index = rx_frame.data.u8[0];
      if (battery2_frame_index < (CELLCOUNT_EXTENDED / 3)) {
        uint8_t base2_index = battery2_frame_index * 3;
        for (uint8_t i = 0; i < 3; i++) {
          battery2_cellvoltages[base2_index + i] =
              (((rx_frame.data.u8[2 * (i + 1)] & 0x0F) << 8) | rx_frame.data.u8[2 * i + 1]);
        }
      }
      break;
    case 0x444:
      datalayer.battery2.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x445:
      datalayer.battery2.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x446:
      datalayer.battery2.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x447:
      datalayer.battery2.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      battery2_highprecision_SOC = ((rx_frame.data.u8[5] & 0x0F) << 8) | rx_frame.data.u8[4];
      battery2_lowest_temperature = (rx_frame.data.u8[1] - 40);
      battery2_highest_temperature = (rx_frame.data.u8[3] - 40);
      break;
    case 0x47B:
      datalayer.battery2.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x524:
      datalayer.battery2.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x7EF:  //OBD2 PID reply from battery2
      switch (rx_frame.data.u8[3]) {
        case POLL_FOR_BATTERY_SOC:
          BMS2_SOC = rx_frame.data.u8[4];
          break;
        case POLL_FOR_BATTERY_VOLTAGE:
          BMS2_voltage = (rx_frame.data.u8[5] << 8) | rx_frame.data.u8[4];
          break;
        case POLL_FOR_BATTERY_CURRENT:
          BMS2_current = ((rx_frame.data.u8[5] << 8) | rx_frame.data.u8[4]) - 5000;
          break;
        case POLL_FOR_LOWEST_TEMP_CELL:
          BMS2_lowest_cell_temperature = (rx_frame.data.u8[4] - 40);
          break;
        case POLL_FOR_HIGHEST_TEMP_CELL:
          BMS2_highest_cell_temperature = (rx_frame.data.u8[4] - 40);
          break;
        case POLL_FOR_BATTERY_PACK_AVG_TEMP:
          BMS2_average_cell_temperature = (rx_frame.data.u8[4] - 40);
          break;
        case POLL_FOR_BATTERY_CELL_MV_MAX:
          BMS2_highest_cell_voltage_mV = (rx_frame.data.u8[5] << 8) | rx_frame.data.u8[4];
          break;
        case POLL_FOR_BATTERY_CELL_MV_MIN:
          BMS2_lowest_cell_voltage_mV = (rx_frame.data.u8[5] << 8) | rx_frame.data.u8[4];
          break;
        default:  //Unrecognized reply
          break;
      }
      break;
    default:
      break;
  }
}
#endif  //DOUBLE_BATTERY

#endif
