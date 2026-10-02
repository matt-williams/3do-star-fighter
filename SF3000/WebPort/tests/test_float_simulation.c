#include "../sf_web_simulation.h"
#include "../sf_web_math.h"
#include "../sf_web_fixed_step.h"
#include "../../SFlib/Bit_Struct.h"
#include "../../SFlib/Camera_Struct.h"
#include "../../SFlib/Laser_Struct.h"
#include "../../SFlib/Ship_Struct.h"
#include "../../SFlib/Smoke_Struct.h"

#include <assert.h>
#include <math.h>
#include <string.h>

ship_list ships;
laser_list lasers;
smoke_list smokes;
bit_list bits;
camera_data camera[MAX_CAMERAS];
unsigned char height_map[256][256];
unsigned char poly_map[128][128];
long camera_x_rotation;
long camera_y_rotation;
long camera_z_rotation;

static const long raw_cell = 1L << 24;
static const long beam_laser_type = 7;

static void reset_simulation(void)
{
    memset(&ships, 0, sizeof(ships));
    memset(&lasers, 0, sizeof(lasers));
    memset(&smokes, 0, sizeof(smokes));
    memset(&bits, 0, sizeof(bits));
    memset(camera, 0, sizeof(camera));
    sf_web_simulation_reset();
    sf_web_fixed_step_simulation_reset();
    sf_web_fixed_step_begin_simulation_step();
    sf_web_simulation_begin_step();
}

static void test_ships_are_explicitly_legacy_authoritative(void)
{
    ship_stack *ship = &ships.ship_item[0];

    reset_simulation();
    ship->header.status = 1;
    ship->x_pos = 3L * raw_cell;
    sf_web_simulation_begin_step();
    assert(sf_web_simulation_ship(ship) == NULL);
    ship->x_pos = 4L * raw_cell;
    sf_web_simulation_project_compatibility_step();
    assert(ship->x_pos == 4L * raw_cell);
}

static void test_smoke_updates_float_state_and_only_projects_outward(void)
{
    smoke_stack *smoke = &smokes.smoke_item[0];
    SFWebSimulationKinematics *state;
    float authoritative_x;

    reset_simulation();
    smoke->header.status = 1;
    smoke->x_vel = raw_cell;
    smoke->counter = 2;
    sf_web_simulation_spawn_smoke(smoke);
    sf_web_simulation_advance_smoke(smoke);
    state = sf_web_simulation_smoke(smoke);
    assert(state != NULL);
    authoritative_x = state->position.local_x;
    assert(authoritative_x > 0.19f && authoritative_x < 0.21f);
    assert(state->lifetime_ticks == 9);

    smoke->x_pos = 12L * raw_cell;
    smoke->x_vel = -raw_cell;
    sf_web_simulation_project_compatibility_step();
    assert(fabsf(state->position.local_x - authoritative_x) < 0.00001f);
    assert(smoke->x_pos > 0 && smoke->x_pos < raw_cell);
    assert(smoke->x_vel > 0);
}

static void test_lasers_integrate_both_endpoints_at_100_hz(void)
{
    laser_stack *laser = &lasers.laser_item[0];
    SFWebSimulationKinematics *state;

    reset_simulation();
    laser->header.status = 1;
    laser->x_pos2 = raw_cell / 2;
    laser->x_vel = raw_cell;
    laser->counter = 1;
    sf_web_simulation_spawn_laser(laser);
    sf_web_simulation_advance_laser(laser);
    sf_web_simulation_age_laser(laser);
    state = sf_web_simulation_laser(laser);
    assert(state != NULL);
    assert(fabsf(state->position.local_x - 0.2f) < 0.00001f);
    assert(fabsf(state->position2.local_x - 0.7f) < 0.00001f);
    assert(state->lifetime_ticks == 4);
    sf_web_simulation_age_laser(laser);
    sf_web_simulation_age_laser(laser);
    sf_web_simulation_age_laser(laser);
    sf_web_simulation_age_laser(laser);
    assert(laser->counter == 0);
    sf_web_simulation_age_laser(laser);
    assert(laser->counter == 0);
    sf_web_simulation_age_laser(laser);
    sf_web_simulation_age_laser(laser);
    sf_web_simulation_age_laser(laser);
    sf_web_simulation_age_laser(laser);
    assert(laser->counter == -1);

    laser->x_pos = 9L * raw_cell;
    sf_web_simulation_project_compatibility_step();
    assert(fabsf(state->position.local_x - 0.2f) < 0.00001f);
    assert(laser->x_pos > 0 && laser->x_pos < raw_cell);
}

static void test_beam_lasers_are_explicitly_legacy_authoritative(void)
{
    laser_stack *laser = &lasers.laser_item[0];

    reset_simulation();
    laser->header.status = 1;
    laser->type = beam_laser_type;
    laser->x_pos = raw_cell;
    sf_web_simulation_begin_step();
    assert(sf_web_simulation_laser(laser) == NULL);
    laser->x_pos = 2L * raw_cell;
    sf_web_simulation_project_compatibility_step();
    assert(laser->x_pos == 2L * raw_cell);
}

static void test_bits_own_pose_velocity_rotation_and_lifetime(void)
{
    bit_stack *bit = &bits.bit_item[0];
    SFWebSimulationKinematics *state;
    float rotation;

    reset_simulation();
    bit->header.status = 1;
    bit->x_vel = raw_cell;
    bit->x_r_vel = SF_LEGACY_TURN_UNITS / 20;
    bit->counter = 3;
    sf_web_simulation_spawn_bit(bit);
    sf_web_simulation_advance_bit(bit, 0);
    sf_web_simulation_age_bit(bit);
    state = sf_web_simulation_bit(bit);
    assert(state != NULL);
    assert(state->position.local_x > 0.19f && state->position.local_x < 0.21f);
    assert(state->x_radians > 0.06f && state->x_radians < 0.064f);
    assert(state->lifetime_ticks == 12);
    rotation = state->x_radians;

    bit->x_pos = 7L * raw_cell;
    bit->x_rot = 0;
    sf_web_simulation_project_compatibility_step();
    assert(state->position.local_x > 0.19f && state->position.local_x < 0.21f);
    assert(fabsf(state->x_radians - rotation) < 0.00001f);
}

static void test_tick_deadlines_and_float_terrain_height(void)
{
    SFWebSimulationPosition position = { 2, 3, 0, 0.5f, 0.5f, 0.0f };
    uint64_t deadline;

    memset(height_map, 17, sizeof(height_map));
    height_map[3][2] = 17;
    height_map[3][3] = 21;
    height_map[4][2] = 25;
    height_map[4][3] = 29;
    assert(fabsf(sf_web_simulation_terrain_height(&position) - 6.0f) < 0.0001f);

    sf_web_simulation_reset();
    sf_web_fixed_step_simulation_reset();
    deadline = sf_web_simulation_deadline_after(3);
    assert(!sf_web_simulation_deadline_reached(deadline));
    sf_web_fixed_step_begin_simulation_step();
    sf_web_simulation_begin_step();
    sf_web_fixed_step_begin_simulation_step();
    sf_web_simulation_begin_step();
    assert(!sf_web_simulation_deadline_reached(deadline));
    sf_web_fixed_step_begin_simulation_step();
    sf_web_simulation_begin_step();
    assert(sf_web_simulation_tick() == 3u);
    assert(sf_web_simulation_deadline_reached(deadline));
}

static void test_camera_pose_projects_from_float_authority(void)
{
    SFWebSimulationKinematics *state;
    long x_position;
    long y_position;
    long z_position;
    long x_rotation;
    long y_rotation;
    long z_rotation;

    reset_simulation();
    sf_web_simulation_set_camera_pose(&camera[1], raw_cell / 2, 0, 0,
        SF_LEGACY_TURN_UNITS / 2, 0, 0);
    state = sf_web_simulation_camera(&camera[1]);
    assert(state != NULL);
    state->position.local_x += 0.25f;
    state->y_radians = 1.57079632679f;
    sf_web_simulation_project_camera_pose(&camera[1], &x_position,
        &y_position, &z_position, &x_rotation, &y_rotation, &z_rotation);
    assert(x_position == 3L * raw_cell / 4L);
    assert(y_position == 0 && z_position == 0);
    assert(x_rotation == 512);
    assert(y_rotation == 256);
    assert(z_rotation == 0);
}

int main(void)
{
    test_ships_are_explicitly_legacy_authoritative();
    test_smoke_updates_float_state_and_only_projects_outward();
    test_lasers_integrate_both_endpoints_at_100_hz();
    test_beam_lasers_are_explicitly_legacy_authoritative();
    test_bits_own_pose_velocity_rotation_and_lifetime();
    test_tick_deadlines_and_float_terrain_height();
    test_camera_pose_projects_from_float_authority();
    return 0;
}