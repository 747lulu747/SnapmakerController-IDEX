#ifndef FILAMENT_SENSOR
#define FILAMENT_SENSOR

#include "stdint.h"

#define FILAMENT_SENSOR_COUNT 2
#define FILAMENT_LOOP(i) for (uint8_t i = 0; i < FILAMENT_SENSOR_COUNT; i++)
#define FILAMENT_CHECK_DISTANCE 3  // mm
#define FILAMENT_THRESHOLD 5  // ADC diff value
#define FILAMENT_CHECK_TIMES 2

typedef struct {
  bool enabled[FILAMENT_SENSOR_COUNT];
  float distance;  // Move this distance to detect abnormal sensing deviation
  uint8_t check_times;  // Determine the number of exceptions
  uint16_t threshold;
}filament_check_param_t;

class FilamentSample {
  public:
    // Was temperature isr updated  
    inline void sample(const uint32_t s) { raw += s; sample_num++;}
    void ready() {
      if (sample_num) {
        value = raw / sample_num;
      }
      sample_num = raw = 0;
    }
    uint16_t get() {return value;}
  private:
    int32_t raw = 0;
    uint16_t value;
    uint8_t sample_num = 0;
};

class FilamentSensor
{
  public:
    void init();
    void e0_step(uint8_t step);
    void e1_step(uint8_t step);
    void next_sample(uint8_t e);
    void ready() {
      FILAMENT_LOOP(i) {
        filament[i].ready();
      }
    }
    void enable(uint8_t e) {
      filament_param.enabled[e] = true;
      triggered[e] = false;
      check_step_count[e] = 0;
      next_sample(e);
    }
    void disable(uint8_t e) {
      triggered[e] = false;
      filament_param.enabled[e] = false;
    }
    void enable_all() {
      FILAMENT_LOOP(i) {
        enable(i);
      }
    }
    void disable_all() {
      FILAMENT_LOOP(i) {
        disable(i);
      }
    }
    bool is_trigger(uint8_t e) {
      return triggered[e] && is_enable(e);
    }
    bool is_trigger() {
      return is_trigger(0) || is_trigger(1);
    }
    bool is_enable(uint8_t e) {
      return filament_param.enabled[e];
    }

    void debug();
    void check();
    void test_adc(uint8_t e, float step_mm, uint32_t count);
    void reset();
    void used_default_param();
  public:
    FilamentSample filament[FILAMENT_SENSOR_COUNT];
    filament_check_param_t filament_param;
  private:
    uint8_t err_mask = 0x1;
    int32_t check_step_count[FILAMENT_SENSOR_COUNT];
    uint8_t err_times[FILAMENT_SENSOR_COUNT] = {0, 0};
    int32_t e_step_count[FILAMENT_SENSOR_COUNT] = {0, 0};
    bool triggered[FILAMENT_SENSOR_COUNT] = {false, false};
    uint16_t start_adc[FILAMENT_SENSOR_COUNT] = {0, 0};
};

extern FilamentSensor filament_sensor;

#endif