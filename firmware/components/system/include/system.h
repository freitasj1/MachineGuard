/**
 * @file system.h
 * @brief System decision task interface.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Public constants and macros
 * ========================================================================== */

/**
 * @brief Number of DSP evaluations used to build the healthy baseline.
 */
#define SYSTEM_WARMUP_EVALUATIONS       (600U)

/**
 * @brief Number of consecutive abnormal evaluations required to enter ALARM.
 */
#define SYSTEM_ALARM_CONSECUTIVE_COUNT  (5U)

/**
 * @brief Number of consecutive normal evaluations required to return HEALTHY.
 */
#define SYSTEM_HEALTHY_CONSECUTIVE_COUNT (5U)

/**
 * @brief Initial Z-score threshold.
 *
 * This value is provisional and must be calibrated experimentally.
 */
#define SYSTEM_ZSCORE_THRESHOLD         (3.0f)

/* ============================================================================
 * Public types
 * ========================================================================== */


/* ============================================================================
 * Public function prototypes
 * ========================================================================== */

/**
 * @brief System decision task.
 *
 * Receives DSP results, builds the healthy baseline during warm-up and
 * evaluates the machine state during normal operation.
 *
 * @param arg Pointer to app_context_t.
 */
void task_system(void *arg);

#ifdef __cplusplus
}
#endif