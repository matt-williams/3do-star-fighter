#ifndef SF_WEB_FIXED_STEP_H
#define SF_WEB_FIXED_STEP_H

#include <stdint.h>

/*
 * Browser pacing reports time in 60 Hz VBL fields while the target simulation
 * interval is 10 ms.  Three units of this 300 Hz rational clock equal one
 * 100 Hz fixed step and five equal one VBL field.
 */
#define SF_WEB_FIXED_STEP_HZ 100u
#define SF_WEB_FIXED_STEP_MICROSECONDS 10000u
#define SF_WEB_VBL_HZ 60u
#define SF_WEB_REFERENCE_UPDATE_HZ 20u
#define SF_WEB_REFERENCE_UPDATE_VBL_FIELDS 3u
#define SF_WEB_FIXED_STEPS_PER_REFERENCE_UPDATE \
    (SF_WEB_FIXED_STEP_HZ / SF_WEB_REFERENCE_UPDATE_HZ)

typedef struct SFWebRealTimeStepClock {
    uint64_t fixed_steps_elapsed;
    uint32_t fractional_microseconds;
} SFWebRealTimeStepClock;

typedef struct SFWebFixedStepClock {
    uint64_t fixed_steps_elapsed;
    uint32_t fractional_step_units;
} SFWebFixedStepClock;

typedef struct SFWebFixedStepRate {
    uint32_t remainder;
} SFWebFixedStepRate;

void sf_web_fixed_step_clock_reset(SFWebFixedStepClock *clock);
uint64_t sf_web_fixed_step_clock_advance_vbl_fields(
    SFWebFixedStepClock *clock, uint32_t fields);
void sf_web_real_time_step_clock_reset(SFWebRealTimeStepClock *clock);
uint64_t sf_web_real_time_step_clock_advance_microseconds(
    SFWebRealTimeStepClock *clock, uint32_t microseconds);
void sf_web_fixed_step_rate_reset(SFWebFixedStepRate *rate);
uint64_t sf_web_fixed_step_rate_advance(SFWebFixedStepRate *rate,
    uint64_t fixed_steps);

/*
 * Every browser simulation step selects one fifth of a legacy 20 Hz delta.
 * The phase acts as a shared fixed-point residual, so a constant delta sums
 * exactly to its former value across five 10 ms steps without extending every
 * legacy entity layout with fractional fields.
 */
void sf_web_fixed_step_simulation_reset(void);
void sf_web_fixed_step_begin_simulation_step(void);
uint64_t sf_web_fixed_step_tick(void);
uint64_t sf_web_fixed_step_deadline_after(uint64_t ticks);
int sf_web_fixed_step_deadline_reached(uint64_t deadline);
long sf_web_fixed_step_scale_legacy_delta(long legacy_delta);
int sf_web_fixed_step_is_reference_tick(void);

#endif
