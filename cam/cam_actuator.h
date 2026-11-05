/* The DW9714 VCM autofocus configuration, captured from the vendor camera
 * HAL with actuator_dump_set_info() (see tools/parse_actuator_dump.py).
 *
 *   45 steps from infinity (0) to macro (44), 9 DAC codes per step,
 *   10-bit DAC, initial code 169.
 */
#ifndef CAM_ACTUATOR_H
#define CAM_ACTUATOR_H

#define ACT_TOTAL_STEPS 45
#define ACT_INITIAL_CODE 169
#define ACT_MACRO_BOUND 44
#define ACT_INF_BOUND 0
#define ACT_CODE_PER_STEP 9

#endif /* CAM_ACTUATOR_H */
