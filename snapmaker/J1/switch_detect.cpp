#include "src/inc/MarlinConfigPre.h"
#include "src/core/millis_t.h"
#include "HAL.h"
#include "src/module/motion.h"
#include "src/module/temperature.h"
#include "src/module/planner.h"
#include "src/module/stepper.h"
#include "switch_detect.h"

#define SW_FILAMNET0_BIT      0
#define SW_FILAMNET1_BIT      1
#define SW_PROBE0_BIT         2
#define SW_PROBE1_BIT         3
#define SW_POWER_LOSS_BIT     4
#define SW_STALL_GUARD_BIT    5
#define SW_MANUAL_STOP_BIT    6
#define SW_STALLGUARD_BIT     7

SwitchDetect switch_detect;

#define UPDATE_STATUS(IO, level, reg, bit) do{ \
                                      if(TEST(enable_bits, bit)) { \
                                        if(READ(IO) == level) SBI(reg, bit); \
                                        else CBI(reg, bit); \
                                        if(TEST(reg, bit) && TEST(status_bits, bit)) \
                                          trigged = true; \
                                      } \
                                    } while(0)

#define UPDATE_STATUS_REG(IO, bit)  UPDATE_STATUS(IO, status_bits, bit)

#define TRIGER

void SwitchDetect::init() {
  SET_INPUT(X0_CAL_PIN);
  SET_INPUT(X1_CAL_PIN);
  SET_INPUT_PULLUP(STALL_GUARD_PIN);
  SET_INPUT_PULLUP(POWER_LOST_PIN);
  disable_all();
}

void SwitchDetect::check() {
  uint32_t tmp_status_bits = 0;
  bool trigged = false;

  if(TEST(status_bits, SW_MANUAL_STOP_BIT)) {
    trigged = true;
    CBI(status_bits, SW_MANUAL_STOP_BIT);
  }
  if(TEST(status_bits, SW_STALLGUARD_BIT)) {
    trigged = true;
    CBI(status_bits, SW_STALLGUARD_BIT);
  }

  do {
    if(trigged == true) break;
    if(probe_detect_level == 0)
      UPDATE_STATUS(X0_CAL_PIN, LOW, tmp_status_bits, SW_PROBE0_BIT);
    else
      UPDATE_STATUS(X0_CAL_PIN, HIGH, tmp_status_bits, SW_PROBE0_BIT);
    if(trigged == true) break;
    if(probe_detect_level == 0)
      UPDATE_STATUS(X1_CAL_PIN, LOW, tmp_status_bits, SW_PROBE1_BIT);
    else
      UPDATE_STATUS(X1_CAL_PIN, HIGH, tmp_status_bits, SW_PROBE1_BIT);
  }while(0);

  if(trigged == true)
    stepper.quick_stop();
  status_bits = tmp_status_bits;
}



void SwitchDetect::disable_all() {
  enable_bits = 0;
  status_bits = 0;
}

void SwitchDetect::set_probe_detect_level(uint8_t Level) {
  probe_detect_level = Level;
}

void SwitchDetect::enable_probe() {
  enable(SW_PROBE0_BIT);
  enable(SW_PROBE1_BIT);
}

void SwitchDetect::disable_probe() {
  disable(SW_PROBE0_BIT);
  disable(SW_PROBE1_BIT);
}

void SwitchDetect::enable_power_lost() {
  enable(SW_POWER_LOSS_BIT);
}

void SwitchDetect::disable_power_lost() {
  disable(SW_POWER_LOSS_BIT);
}

void SwitchDetect::enable_stall_guard() {
  enable(SW_STALL_GUARD_BIT);
}

void SwitchDetect::disable_stall_guard() {
  disable(SW_STALL_GUARD_BIT);
}

void SwitchDetect::enable(uint8_t Item) {
  SBI(enable_bits, Item);
  CBI(status_bits, Item);
}

void SwitchDetect::disable(uint8_t Item) {
  CBI(enable_bits, Item);
  CBI(status_bits, Item);
}

void SwitchDetect::manual_trig_stop() {
  SBI(status_bits, SW_MANUAL_STOP_BIT);
}

void SwitchDetect::stall_guard_stop() {
  SBI(status_bits, SW_STALLGUARD_BIT);
}

bool SwitchDetect::read_e0_probe_status() {
  return READ(X0_CAL_PIN) == LOW;
}

bool SwitchDetect::read_e1_probe_status() {
  return READ(X1_CAL_PIN) == LOW;
}
