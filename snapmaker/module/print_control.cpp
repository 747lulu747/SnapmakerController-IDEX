#include "print_control.h"
#include "src/module/motion.h"
#include "src/module/tool_change.h"
#include "src/module/planner.h"
#include "src/gcode/gcode.h"
#include "motion_control.h"
#include "../../Marlin/src/module/temperature.h"
#include "../../Marlin/src/module/settings.h"
#include "system.h"
#include "fdm.h"
#include "power_loss.h"
#include "../module/filament_sensor.h"


PrintControl print_control;


#define GCODE_BUFFER_SIZE (1024*2)
uint16_t buffer_head = 0;
uint16_t buffer_tail = 0;
static uint8_t gcode_buffer[GCODE_BUFFER_SIZE];

void PrintControl::init() {

}

bool PrintControl::buffer_is_empty() {
 return buffer_head == buffer_tail && !planner.has_blocks_queued();
}

bool PrintControl::is_backup_mode() {
  return mode_ == PRINT_AUTO_PARK_MODE;
}

uint32_t PrintControl::get_buf_used() {
  return (buffer_head + GCODE_BUFFER_SIZE - buffer_tail) % GCODE_BUFFER_SIZE;
}

uint32_t PrintControl::get_buf_free() {
  return GCODE_BUFFER_SIZE - get_buf_used();
}

uint32_t PrintControl::get_cur_line() {
  return power_loss.cur_line;
}

uint32_t PrintControl::next_req_line() {
  return power_loss.next_req;
}


bool PrintControl::filament_check() {
  bool is_trigger = false;
  if (mode_ < PRINT_DUPLICATION_MODE) {
    is_trigger = filament_sensor.is_trigger(active_extruder);
  } else {
    is_trigger = filament_sensor.is_trigger();
  }
  if (!is_trigger) {
    return false;
  }

  system_status_source_e source = SYSTEM_STATUE_SCOURCE_NONE;
  switch (mode_) {
    case PRINT_BACKUP_MODE:
    case PRINT_DUPLICATION_MODE:
    case PRINT_MIRRORED_MODE:
      source = SYSTEM_STATUE_SCOURCE_FILAMENT;
      break;
    case PRINT_AUTO_PARK_MODE:
      if (filament_sensor.is_trigger(!active_extruder)) {
        source = SYSTEM_STATUE_SCOURCE_FILAMENT;
      } else {
        source = SYSTEM_STATUE_SCOURCE_TOOL_CHANGE;
      }
      break;
  }
  SERIAL_ECHOLNPAIR("lilament trigger and source:", source);
  system_service.set_status(SYSTEM_STATUE_PAUSING, source);
  return true;
}

bool PrintControl::get_commands(uint8_t *cmd, uint32_t &line, uint16_t max_len) {

  if (power_loss.power_loss_status != POWER_LOSS_IDLE) {
    return false;
  }

  if (system_service.get_status() != SYSTEM_STATUE_PRINTING) {
    return false;
  }

  if (filament_check()) {
    return false;
  }

  if(commands_lock_) {
    return false;
  }

  while (buffer_head != buffer_tail) {
    if (gcode_buffer[buffer_head] == ' ' || gcode_buffer[buffer_head] == '\n') {
      if (gcode_buffer[buffer_head] == '\n') {
        power_loss.line_number_sum++;
      }
      buffer_head = (buffer_head + 1) % GCODE_BUFFER_SIZE;
    } else {
      break;
    }
  }
  uint32_t get_commands = 0;
  while (buffer_head != buffer_tail) {
    if (get_commands >= max_len) {
      SERIAL_ECHOLNPAIR("cmd too long failed!");
      return false;
    }

    cmd[get_commands] = gcode_buffer[buffer_head];
    buffer_head = (buffer_head + 1) % GCODE_BUFFER_SIZE;

    if (cmd[get_commands] == '\n') {
      cmd[get_commands] = 0;
      power_loss.line_number_sum++;
      line = power_loss.line_number_sum;
      return true;
    }
    get_commands++;
  }
  return false;
}

ErrCode PrintControl::push_gcode(uint32_t start_line, uint32_t end_line, uint8_t *data, uint16_t size) {
  uint8_t gcode_count = 0;
  uint32_t free = get_buf_free();

  if (free < size) {
    SERIAL_ECHOLNPAIR("gcode no memory ,free:", free, " cur:", size);
    return E_NO_MEM;
  }

  for (uint16_t i = 0; i < size; i++) {
    if (data[i] == '\n') {
      gcode_count ++;
    }
  }
  if ((end_line - start_line + 1) != gcode_count) {
    SERIAL_ECHOLNPAIR("failed line start:", start_line, " end:", end_line, " count:", gcode_count, " next_req:", power_loss.next_req);
    return E_PARAM;
  }
  for (uint32_t i = 0; i < size; i++) {
    gcode_buffer[buffer_tail] = data[i];
    buffer_tail = (buffer_tail + 1) % GCODE_BUFFER_SIZE;
  }
  power_loss.next_req = end_line + 1;

  return E_SUCCESS;
}

ErrCode PrintControl::start() {
  if (system_service.get_status() != SYSTEM_STATUE_IDLE) {
    return PRINT_RESULT_START_ERR_E;
  }
  switch (mode_) {
    case PRINT_BACKUP_MODE:
    case PRINT_AUTO_PARK_MODE:
      dual_x_carriage_mode = DXC_FULL_CONTROL_MODE;
      break;
    default:
      dual_x_carriage_mode = (DualXMode)mode_;
      break;
  }
  if (homing_needed()) {
    motion_control.home();
  }
  if (mode_ >= PRINT_DUPLICATION_MODE) {
    duplicate_extruder_x_offset = (dual_x_carriage_mode == DXC_DUPLICATION_MODE) ? \
                          DUPLICATION_MODE_X_OFFSET : MIRRORED_MODE_X_OFFSET;
    idex_set_mirrored_mode(dual_x_carriage_mode == DXC_MIRRORED_MODE);
    motion_control.home_x();
  }


  power_loss.cur_line = power_loss.line_number_sum = 0;
  power_loss.next_req = 0;
  buffer_head = buffer_tail = 0;
  power_loss.clear();
  if (power_loss.is_power_pin_trigger()) {
    power_loss.power_loss_en = false;
    SERIAL_ECHOLNPAIR(" power-loss signal is abnormal, disable the power-loss function");
  }
  filament_sensor.reset();
  memset(&print_err_info, 0, sizeof(print_err_info));
  system_service.set_status(SYSTEM_STATUE_PRINTING);
  return E_SUCCESS;
}

ErrCode PrintControl::pause() {
  buffer_head = buffer_tail = 0;
  motion_control.quickstop();
  power_loss.stash_print_env();
  if (current_position.z + Z_DOWN_SAFE_DISTANCE < Z_MAX_POS) {
    motion_control.move_z(Z_DOWN_SAFE_DISTANCE, PRINT_TRAVEL_FEADRATE);
  } else {
    motion_control.move_to_z(Z_MAX_POS, PRINT_TRAVEL_FEADRATE);
  }
  motion_control.synchronize();
  motion_control.retrack_e(PRINT_RETRACK_DISTANCE, CHANGE_FILAMENT_SPEED);
  motion_control.home_x();
  motion_control.home_y();
  system_service.set_status(SYSTEM_STATUE_PAUSED);
  return E_SUCCESS;
}

ErrCode PrintControl::resume() {
  buffer_head = buffer_tail = 0;
  power_loss.resume_print_env();
  system_service.set_status(SYSTEM_STATUE_PRINTING);
  return E_SUCCESS;
}

ErrCode PrintControl::stop() {
  if (system_service.get_status() != SYSTEM_STATUE_IDLE) {
    power_loss.clear();
    motion_control.quickstop();
    buffer_head = buffer_tail = 0;
    motion_control.retrack_e(PRINT_RETRACK_DISTANCE, CHANGE_FILAMENT_SPEED);
    motion_control.home();
    system_service.set_status(SYSTEM_STATUE_IDLE);
    mode_ = PRINT_BACKUP_MODE;
    dual_x_carriage_mode = DXC_FULL_CONTROL_MODE;
    HOTEND_LOOP() {
      thermalManager.setTargetHotend(0, e);
      fdm_head.set_fan_speed(e, 0, 0);
       set_work_flow_percentage(e, 100);
    }
    thermalManager.setTargetBed(0);
    set_feedrate_percentage(100);
  }
  (void)settings.save();
  return E_SUCCESS;
}

ErrCode PrintControl::set_mode(print_mode_e mode) {
  mode_ = mode;
  return E_SUCCESS;
}

void PrintControl::set_feedrate_percentage(int16_t percentage) {
  feedrate_percentage = percentage;
}

int16_t PrintControl::get_feedrate_percentage() {
  return feedrate_percentage;
}

int16_t PrintControl::get_work_flow_percentage(uint8_t e) {
  return planner.flow_percentage[e];
}

void PrintControl::set_work_flow_percentage(uint8_t e, int16_t percentage) {
  planner.set_flow(e, percentage);
}

void PrintControl::error_and_stop() {
  print_err_info.is_err = true;
  print_err_info.err_line = next_req_line();
  buffer_head = buffer_tail = 0;
  motion_control.quickstop();
  power_loss.stash_print_env();
  power_loss.write_flash();
  motion_control.synchronize();
  motion_control.retrack_e(PRINT_RETRACK_DISTANCE, CHANGE_FILAMENT_SPEED);
  motion_control.home();
  system_service.set_status(SYSTEM_STATUE_IDLE);
}