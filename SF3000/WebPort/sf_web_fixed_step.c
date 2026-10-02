#include "sf_web_fixed_step.h"

#include <stddef.h>
#include <string.h>

enum {
    SF_WEB_CLOCK_UNITS_PER_VBL_FIELD = 5,
    SF_WEB_CLOCK_UNITS_PER_FIXED_STEP = 3
};

static uint32_t simulation_step_phase;
static int simulation_step_active;

static int64_t sf_web_truncate_divide_by_five(int64_t value)
{
    return value / SF_WEB_FIXED_STEPS_PER_REFERENCE_UPDATE;
}

void sf_web_fixed_step_clock_reset(SFWebFixedStepClock *clock)
{
    memset(clock, 0, sizeof(*clock));
}

uint64_t sf_web_fixed_step_clock_advance_vbl_fields(
    SFWebFixedStepClock *clock, uint32_t fields)
{
    uint64_t units = clock->fractional_step_units +
        (uint64_t)fields * SF_WEB_CLOCK_UNITS_PER_VBL_FIELD;
    uint64_t fixed_steps = units / SF_WEB_CLOCK_UNITS_PER_FIXED_STEP;

    clock->fractional_step_units = (uint32_t)
        (units % SF_WEB_CLOCK_UNITS_PER_FIXED_STEP);
    clock->fixed_steps_elapsed += fixed_steps;
    return fixed_steps;
}

void sf_web_real_time_step_clock_reset(SFWebRealTimeStepClock *clock)
{
    memset(clock, 0, sizeof(*clock));
}

uint64_t sf_web_real_time_step_clock_advance_microseconds(
    SFWebRealTimeStepClock *clock, uint32_t microseconds)
{
    uint64_t elapsed = (uint64_t)clock->fractional_microseconds +
        microseconds;
    uint64_t fixed_steps = elapsed / SF_WEB_FIXED_STEP_MICROSECONDS;

    clock->fractional_microseconds = (uint32_t)(elapsed %
        SF_WEB_FIXED_STEP_MICROSECONDS);
    clock->fixed_steps_elapsed += fixed_steps;
    return fixed_steps;
}

void sf_web_fixed_step_rate_reset(SFWebFixedStepRate *rate)
{
    memset(rate, 0, sizeof(*rate));
}

uint64_t sf_web_fixed_step_rate_advance(SFWebFixedStepRate *rate,
    uint64_t fixed_steps)
{
    uint64_t total = (uint64_t)rate->remainder + fixed_steps;

    rate->remainder = (uint32_t)(total %
        SF_WEB_FIXED_STEPS_PER_REFERENCE_UPDATE);
    return total / SF_WEB_FIXED_STEPS_PER_REFERENCE_UPDATE;
}

void sf_web_fixed_step_simulation_reset(void)
{
    simulation_step_phase = 0;
    simulation_step_active = 0;
}

void sf_web_fixed_step_begin_simulation_step(void)
{
    if (simulation_step_active) {
        simulation_step_phase = (simulation_step_phase + 1) %
            SF_WEB_FIXED_STEPS_PER_REFERENCE_UPDATE;
    } else {
        simulation_step_active = 1;
    }
}

long sf_web_fixed_step_scale_legacy_delta(long legacy_delta)
{
    int64_t lower;
    int64_t upper;

    if (!simulation_step_active) {
        return legacy_delta;
    }

    lower = sf_web_truncate_divide_by_five((int64_t)legacy_delta *
        simulation_step_phase);
    upper = sf_web_truncate_divide_by_five((int64_t)legacy_delta *
        (simulation_step_phase + 1));
    return (long)(upper - lower);
}

int sf_web_fixed_step_is_reference_tick(void)
{
    return simulation_step_active &&
        simulation_step_phase == SF_WEB_FIXED_STEPS_PER_REFERENCE_UPDATE - 1;
}
