#ifndef BED_CONTROL_H
#define BED_CONTROL_H

#include "../J1/common_type.h"
#include "module_base.h"


#pragma pack(1)
typedef struct {
  uint8_t key;
  uint8_t bed_count;  // Fixed 1 for SACP protocol compatibility
  uint8_t zone_index;  // Fixed 0 for SACP protocol compatibility
  float_to_int_t cur_temp;
  int16_t target_temp;
} bed_control_info_t;

#pragma pack()
class BedControl {
  public:
    ErrCode set_temperature(uint16_t temperature);
    ErrCode get_info(bed_control_info_t &info);
    ErrCode get_module_info(module_info_t &info);
};
extern BedControl bed_control;
#endif