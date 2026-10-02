#include "../sf_web_fixed_step.h"

#include <assert.h>
static void test_clock_tracks_reference_cadence(void)
{
    SFWebFixedStepClock clock;

    sf_web_fixed_step_clock_reset(&clock);
    assert(sf_web_fixed_step_clock_advance_vbl_fields(&clock,
        SF_WEB_REFERENCE_UPDATE_VBL_FIELDS) == 5u);
    assert(sf_web_fixed_step_clock_advance_vbl_fields(&clock,
        SF_WEB_REFERENCE_UPDATE_VBL_FIELDS) == 5u);
    assert(sf_web_fixed_step_clock_advance_vbl_fields(&clock,
        SF_WEB_REFERENCE_UPDATE_VBL_FIELDS) == 5u);
    assert(clock.fixed_steps_elapsed == 15u);
    assert(clock.fractional_step_units == 0u);
}

static void test_real_time_clock_tracks_display_intervals_without_drift(void)
{
    SFWebRealTimeStepClock clock;

    sf_web_real_time_step_clock_reset(&clock);
    assert(sf_web_real_time_step_clock_advance_microseconds(&clock,
        16667u) == 1u);
    assert(clock.fractional_microseconds == 6667u);
    assert(sf_web_real_time_step_clock_advance_microseconds(&clock,
        16666u) == 2u);
    assert(clock.fixed_steps_elapsed == 3u);
    assert(clock.fractional_microseconds == 3333u);
    assert(sf_web_real_time_step_clock_advance_microseconds(&clock,
        66667u) == 7u);
    assert(clock.fixed_steps_elapsed == 10u);
    assert(clock.fractional_microseconds == 0u);
}

static void test_rate_accumulator_tracks_presentation_time(void)
{
    SFWebFixedStepRate rate;

    sf_web_fixed_step_rate_reset(&rate);
    assert(sf_web_fixed_step_rate_advance(&rate, 4u) == 0u);
    assert(rate.remainder == 4u);
    assert(sf_web_fixed_step_rate_advance(&rate, 1u) == 1u);
    assert(rate.remainder == 0u);
    assert(sf_web_fixed_step_rate_advance(&rate, 11u) == 2u);
    assert(rate.remainder == 1u);
}

static void test_simulation_steps_scale_every_legacy_delta_exactly(void)
{
    static const long expected_positive_deltas[] = {3, 3, 4, 3, 4};
    static const long expected_negative_deltas[] = {-3, -3, -4, -3, -4};
    unsigned int tick;
    long positive_total = 0;
    long negative_total = 0;
    unsigned int reference_ticks = 0;

    sf_web_fixed_step_simulation_reset();
    assert(sf_web_fixed_step_scale_legacy_delta(17) == 17);
    for (tick = 0; tick < SF_WEB_FIXED_STEPS_PER_REFERENCE_UPDATE; ++tick) {
        sf_web_fixed_step_begin_simulation_step();
        assert(sf_web_fixed_step_scale_legacy_delta(17) ==
            expected_positive_deltas[tick]);
        assert(sf_web_fixed_step_scale_legacy_delta(-17) ==
            expected_negative_deltas[tick]);
        positive_total += expected_positive_deltas[tick];
        negative_total += expected_negative_deltas[tick];
        reference_ticks += sf_web_fixed_step_is_reference_tick() != 0;
    }
    assert(positive_total == 17);
    assert(negative_total == -17);
    assert(reference_ticks == 1u);
}

static void test_countdowns_expire_on_the_reference_step(void)
{
    unsigned int tick;

    sf_web_fixed_step_simulation_reset();
    for (tick = 0; tick < SF_WEB_FIXED_STEPS_PER_REFERENCE_UPDATE; ++tick) {
        sf_web_fixed_step_begin_simulation_step();
        assert(sf_web_fixed_step_scale_legacy_delta(-1) ==
            (tick == SF_WEB_FIXED_STEPS_PER_REFERENCE_UPDATE - 1 ? -1 : 0));
    }
}

int main(void)
{
    test_clock_tracks_reference_cadence();
    test_real_time_clock_tracks_display_intervals_without_drift();
    test_rate_accumulator_tracks_presentation_time();
    test_simulation_steps_scale_every_legacy_delta_exactly();
    test_countdowns_expire_on_the_reference_step();
    return 0;
}
