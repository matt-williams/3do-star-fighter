#include "sf_web_simulation.h"

#include "../SFlib/Bit_Struct.h"
#include "../SFlib/Camera_Struct.h"
#include "../SFlib/Laser_Struct.h"
#include "../SFlib/Ship_Struct.h"
#include "../SFlib/Smoke_Struct.h"
#include "sf_web_fixed_step.h"
#include "sf_web_math.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define SF_WEB_WORLD_CELL_RAW 16777216.0f
#define SF_WEB_TERRAIN_CELL_COUNT 256
#define SF_WEB_SIMULATION_DT 0.01f
#define SF_WEB_LEGACY_UPDATES_PER_SECOND 20.0f
#define SF_WEB_TICKS_PER_LEGACY_UPDATE 5
#define SF_WEB_BEAM_LASER_TYPE 7

extern ship_list ships;
extern laser_list lasers;
extern smoke_list smokes;
extern bit_list bits;
extern camera_data camera[MAX_CAMERAS];
extern unsigned char height_map[256][256];

typedef struct SFWebSimulationSlot {
    SFWebSimulationKinematics state;
} SFWebSimulationSlot;

static SFWebSimulationSlot laser_slots[MAX_LASERS];
static SFWebSimulationSlot smoke_slots[MAX_SMOKES];
static SFWebSimulationSlot bit_slots[MAX_BITS];
static SFWebSimulationSlot camera_slots[MAX_CAMERAS];
static int simulation_enabled;

static uint64_t sf_web_current_tick(void)
{
    return sf_web_fixed_step_tick();
}

#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(link_header) == 12u,
    "float simulation sidecars require the legacy 12-byte link header");
_Static_assert(offsetof(ship_stack, x_pos) == 12u,
    "ship raw compatibility prefix changed");
_Static_assert(offsetof(laser_stack, x_pos) == 12u,
    "laser raw compatibility prefix changed");
_Static_assert(offsetof(smoke_stack, x_pos) == 12u,
    "smoke raw compatibility prefix changed");
_Static_assert(offsetof(bit_stack, x_pos) == 12u,
    "bit raw compatibility prefix changed");
#endif

static int32_t sf_web_raw_cell(int32_t raw)
{
    int32_t cell = raw / (int32_t)SF_WEB_WORLD_CELL_RAW;
    int32_t local = raw % (int32_t)SF_WEB_WORLD_CELL_RAW;

    return local < 0 ? cell - 1 : cell;
}

static float sf_web_raw_local(int32_t raw)
{
    int32_t local = raw % (int32_t)SF_WEB_WORLD_CELL_RAW;

    if (local < 0) {
        local += (int32_t)SF_WEB_WORLD_CELL_RAW;
    }
    return (float)local / SF_WEB_WORLD_CELL_RAW;
}

static void sf_web_position_from_raw(SFWebSimulationPosition *position,
                                     int32_t x, int32_t y, int32_t z)
{
    position->cell_x = sf_web_raw_cell(x) & (SF_WEB_TERRAIN_CELL_COUNT - 1);
    position->cell_y = sf_web_raw_cell(y) & (SF_WEB_TERRAIN_CELL_COUNT - 1);
    position->cell_z = sf_web_raw_cell(z);
    position->local_x = sf_web_raw_local(x);
    position->local_y = sf_web_raw_local(y);
    position->local_z = sf_web_raw_local(z);
}

static int32_t sf_web_raw_from_cell_local(int32_t cell, float local)
{
    int64_t raw;

    local = floorf(local * SF_WEB_WORLD_CELL_RAW + 0.5f);
    raw = (int64_t)cell * (int64_t)SF_WEB_WORLD_CELL_RAW + (int64_t)local;
    return (int32_t)(uint32_t)raw;
}

static void sf_web_normalize_position(SFWebSimulationPosition *position)
{
    float cells;

    cells = floorf(position->local_x);
    position->cell_x = (position->cell_x + (int32_t)cells) &
        (SF_WEB_TERRAIN_CELL_COUNT - 1);
    position->local_x -= cells;
    cells = floorf(position->local_y);
    position->cell_y = (position->cell_y + (int32_t)cells) &
        (SF_WEB_TERRAIN_CELL_COUNT - 1);
    position->local_y -= cells;
    cells = floorf(position->local_z);
    position->cell_z += (int32_t)cells;
    position->local_z -= cells;
}

static void sf_web_position_set_z_raw(SFWebSimulationPosition *position,
                                      int32_t z)
{
    position->cell_z = sf_web_raw_cell(z);
    position->local_z = sf_web_raw_local(z);
}

static void sf_web_integrate_position(SFWebSimulationPosition *position,
                                      float velocity_x, float velocity_y,
                                      float velocity_z)
{
    position->local_x += velocity_x * SF_WEB_SIMULATION_DT;
    position->local_y += velocity_y * SF_WEB_SIMULATION_DT;
    position->local_z += velocity_z * SF_WEB_SIMULATION_DT;
    sf_web_normalize_position(position);
}

static float sf_web_velocity_from_raw(int32_t raw)
{
    return (float)raw * SF_WEB_LEGACY_UPDATES_PER_SECOND /
        SF_WEB_WORLD_CELL_RAW;
}

static int32_t sf_web_velocity_to_raw(float velocity)
{
    return (int32_t)lroundf(velocity * SF_WEB_WORLD_CELL_RAW /
        SF_WEB_LEGACY_UPDATES_PER_SECOND);
}

static float sf_web_angular_velocity_from_raw(int32_t raw)
{
    return (float)raw * SF_WEB_LEGACY_UPDATES_PER_SECOND *
        (6.28318530717958647692f / (float)SF_LEGACY_TURN_UNITS);
}

static int32_t sf_web_angular_velocity_to_raw(float velocity)
{
    return (int32_t)lroundf(velocity / SF_WEB_LEGACY_UPDATES_PER_SECOND *
        ((float)SF_LEGACY_TURN_UNITS / 6.28318530717958647692f));
}

static int64_t sf_web_lifetime_from_legacy(long counter)
{
    return (int64_t)counter * SF_WEB_TICKS_PER_LEGACY_UPDATE;
}

static long sf_web_lifetime_to_legacy(int64_t ticks)
{
    if (ticks > 0) {
        return (long)((ticks + SF_WEB_TICKS_PER_LEGACY_UPDATE - 1) /
            SF_WEB_TICKS_PER_LEGACY_UPDATE);
    }
    if (ticks < 0) {
        return (long)(ticks / SF_WEB_TICKS_PER_LEGACY_UPDATE);
    }
    return 0;
}

static ptrdiff_t sf_web_slot_index(const void *item, const void *base,
                                   size_t item_size, size_t count)
{
    ptrdiff_t offset;

    if (item == NULL) {
        return -1;
    }
    offset = (const unsigned char *)item - (const unsigned char *)base;
    if (offset < 0 || (size_t)offset >= item_size * count ||
        (size_t)offset % item_size != 0) {
        return -1;
    }
    return offset / (ptrdiff_t)item_size;
}

static SFWebSimulationSlot *sf_web_laser_slot(laser_stack *laser)
{
    ptrdiff_t index = sf_web_slot_index(laser, lasers.laser_item,
        sizeof(*laser), MAX_LASERS);

    return index < 0 ? NULL : &laser_slots[index];
}

static SFWebSimulationSlot *sf_web_smoke_slot(smoke_stack *smoke)
{
    ptrdiff_t index = sf_web_slot_index(smoke, smokes.smoke_item,
        sizeof(*smoke), MAX_SMOKES);

    return index < 0 ? NULL : &smoke_slots[index];
}

static SFWebSimulationSlot *sf_web_bit_slot(bit_stack *bit)
{
    ptrdiff_t index = sf_web_slot_index(bit, bits.bit_item,
        sizeof(*bit), MAX_BITS);

    return index < 0 ? NULL : &bit_slots[index];
}

static void sf_web_activate_laser_slot(SFWebSimulationSlot *slot,
                                       const laser_stack *laser)
{
    SFWebSimulationKinematics *state;

    if (slot == NULL || laser == NULL) {
        return;
    }
    memset(slot, 0, sizeof(*slot));
    state = &slot->state;
    sf_web_position_from_raw(&state->position, (int32_t)laser->x_pos,
        (int32_t)laser->y_pos, (int32_t)laser->z_pos);
    sf_web_position_from_raw(&state->position2, (int32_t)laser->x_pos2,
        (int32_t)laser->y_pos2, (int32_t)laser->z_pos2);
    state->velocity_x = sf_web_velocity_from_raw((int32_t)laser->x_vel);
    state->velocity_y = sf_web_velocity_from_raw((int32_t)laser->y_vel);
    state->velocity_z = sf_web_velocity_from_raw((int32_t)laser->z_vel);
    state->lifetime_ticks = sf_web_lifetime_from_legacy(laser->counter);
    state->activated_tick = sf_web_current_tick();
    state->active = 1;
}

static void sf_web_activate_smoke_slot(SFWebSimulationSlot *slot,
                                       const smoke_stack *smoke)
{
    SFWebSimulationKinematics *state;

    if (slot == NULL || smoke == NULL) {
        return;
    }
    memset(slot, 0, sizeof(*slot));
    state = &slot->state;
    sf_web_position_from_raw(&state->position, (int32_t)smoke->x_pos,
        (int32_t)smoke->y_pos, (int32_t)smoke->z_pos);
    state->velocity_x = sf_web_velocity_from_raw((int32_t)smoke->x_vel);
    state->velocity_y = sf_web_velocity_from_raw((int32_t)smoke->y_vel);
    state->velocity_z = sf_web_velocity_from_raw((int32_t)smoke->z_vel);
    state->lifetime_ticks = sf_web_lifetime_from_legacy(smoke->counter);
    state->activated_tick = sf_web_current_tick();
    state->active = 1;
}

static void sf_web_activate_bit_slot(SFWebSimulationSlot *slot,
                                     const bit_stack *bit)
{
    SFWebSimulationKinematics *state;

    if (slot == NULL || bit == NULL) {
        return;
    }
    memset(slot, 0, sizeof(*slot));
    state = &slot->state;
    sf_web_position_from_raw(&state->position, (int32_t)bit->x_pos,
        (int32_t)bit->y_pos, (int32_t)bit->z_pos);
    state->velocity_x = sf_web_velocity_from_raw((int32_t)bit->x_vel);
    state->velocity_y = sf_web_velocity_from_raw((int32_t)bit->y_vel);
    state->velocity_z = sf_web_velocity_from_raw((int32_t)bit->z_vel);
    state->x_radians = sf_legacy_rotation_to_radians(bit->x_rot);
    state->y_radians = sf_legacy_rotation_to_radians(bit->y_rot);
    state->z_radians = sf_legacy_rotation_to_radians(bit->z_rot);
    state->angular_velocity_x = sf_web_angular_velocity_from_raw(
        (int32_t)bit->x_r_vel);
    state->angular_velocity_y = sf_web_angular_velocity_from_raw(
        (int32_t)bit->y_r_vel);
    state->angular_velocity_z = sf_web_angular_velocity_from_raw(
        (int32_t)bit->z_r_vel);
    state->lifetime_ticks = sf_web_lifetime_from_legacy(bit->counter);
    state->activated_tick = sf_web_current_tick();
    state->active = 1;
}

static void sf_web_project_laser_slot(const SFWebSimulationSlot *slot,
                                      laser_stack *laser)
{
    const SFWebSimulationKinematics *state = &slot->state;

    laser->x_pos = sf_web_raw_from_cell_local(state->position.cell_x,
        state->position.local_x);
    laser->y_pos = sf_web_raw_from_cell_local(state->position.cell_y,
        state->position.local_y);
    laser->z_pos = sf_web_raw_from_cell_local(state->position.cell_z,
        state->position.local_z);
    laser->x_pos2 = sf_web_raw_from_cell_local(state->position2.cell_x,
        state->position2.local_x);
    laser->y_pos2 = sf_web_raw_from_cell_local(state->position2.cell_y,
        state->position2.local_y);
    laser->z_pos2 = sf_web_raw_from_cell_local(state->position2.cell_z,
        state->position2.local_z);
    laser->x_vel = sf_web_velocity_to_raw(state->velocity_x);
    laser->y_vel = sf_web_velocity_to_raw(state->velocity_y);
    laser->z_vel = sf_web_velocity_to_raw(state->velocity_z);
    laser->counter = sf_web_lifetime_to_legacy(state->lifetime_ticks);
}

static void sf_web_project_smoke_slot(const SFWebSimulationSlot *slot,
                                      smoke_stack *smoke)
{
    const SFWebSimulationKinematics *state = &slot->state;

    smoke->x_pos = sf_web_raw_from_cell_local(state->position.cell_x,
        state->position.local_x);
    smoke->y_pos = sf_web_raw_from_cell_local(state->position.cell_y,
        state->position.local_y);
    smoke->z_pos = sf_web_raw_from_cell_local(state->position.cell_z,
        state->position.local_z);
    smoke->x_vel = sf_web_velocity_to_raw(state->velocity_x);
    smoke->y_vel = sf_web_velocity_to_raw(state->velocity_y);
    smoke->z_vel = sf_web_velocity_to_raw(state->velocity_z);
    smoke->counter = sf_web_lifetime_to_legacy(state->lifetime_ticks);
}

static void sf_web_project_bit_slot(const SFWebSimulationSlot *slot,
                                    bit_stack *bit)
{
    const SFWebSimulationKinematics *state = &slot->state;

    bit->x_pos = sf_web_raw_from_cell_local(state->position.cell_x,
        state->position.local_x);
    bit->y_pos = sf_web_raw_from_cell_local(state->position.cell_y,
        state->position.local_y);
    bit->z_pos = sf_web_raw_from_cell_local(state->position.cell_z,
        state->position.local_z);
    bit->x_rot = sf_radians_to_legacy_rotation(state->x_radians);
    bit->y_rot = sf_radians_to_legacy_rotation(state->y_radians);
    bit->z_rot = sf_radians_to_legacy_rotation(state->z_radians);
    bit->x_vel = sf_web_velocity_to_raw(state->velocity_x);
    bit->y_vel = sf_web_velocity_to_raw(state->velocity_y);
    bit->z_vel = sf_web_velocity_to_raw(state->velocity_z);
    bit->x_r_vel = sf_web_angular_velocity_to_raw(state->angular_velocity_x);
    bit->y_r_vel = sf_web_angular_velocity_to_raw(state->angular_velocity_y);
    bit->z_r_vel = sf_web_angular_velocity_to_raw(state->angular_velocity_z);
    bit->counter = sf_web_lifetime_to_legacy(state->lifetime_ticks);
}

SFWebSimulationKinematics *sf_web_simulation_ship(ship_stack *ship)
{
    (void)ship;
    return NULL;
}

SFWebSimulationKinematics *sf_web_simulation_laser(laser_stack *laser)
{
    SFWebSimulationSlot *slot;

    if (!simulation_enabled) {
        return NULL;
    }
    /* Beam segments are rebuilt by update_beam_lasers and remain legacy-only. */
    if (laser == NULL || laser->type == SF_WEB_BEAM_LASER_TYPE) {
        return NULL;
    }
    slot = sf_web_laser_slot(laser);
    if (slot != NULL && !slot->state.active) {
        sf_web_activate_laser_slot(slot, laser);
    }
    return slot == NULL ? NULL : &slot->state;
}

SFWebSimulationKinematics *sf_web_simulation_smoke(smoke_stack *smoke)
{
    SFWebSimulationSlot *slot;

    if (!simulation_enabled) {
        return NULL;
    }
    slot = sf_web_smoke_slot(smoke);
    if (slot != NULL && !slot->state.active) {
        sf_web_activate_smoke_slot(slot, smoke);
    }
    return slot == NULL ? NULL : &slot->state;
}

SFWebSimulationKinematics *sf_web_simulation_bit(bit_stack *bit)
{
    SFWebSimulationSlot *slot;

    if (!simulation_enabled) {
        return NULL;
    }
    slot = sf_web_bit_slot(bit);
    if (slot != NULL && !slot->state.active) {
        sf_web_activate_bit_slot(slot, bit);
    }
    return slot == NULL ? NULL : &slot->state;
}

SFWebSimulationKinematics *sf_web_simulation_camera(camera_data *value)
{
    ptrdiff_t index;
    SFWebSimulationSlot *slot;

    if (!simulation_enabled || value == NULL) {
        return NULL;
    }
    index = value - camera;
    if (index < 0 || index >= MAX_CAMERAS) {
        return NULL;
    }
    slot = &camera_slots[index];
    if (!slot->state.active) {
        memset(slot, 0, sizeof(*slot));
        sf_web_position_from_raw(&slot->state.position, (int32_t)value->x_pos,
            (int32_t)value->y_pos, (int32_t)value->z_pos);
        slot->state.x_radians = sf_legacy_rotation_to_radians(value->x_rot);
        slot->state.y_radians = sf_legacy_rotation_to_radians(value->y_rot);
        slot->state.activated_tick = sf_web_current_tick();
        slot->state.active = 1;
    }
    return &slot->state;
}

void sf_web_simulation_spawn_laser(laser_stack *laser)
{
    if (simulation_enabled && laser != NULL && laser->type != SF_WEB_BEAM_LASER_TYPE) {
        sf_web_activate_laser_slot(sf_web_laser_slot(laser), laser);
    }
}

void sf_web_simulation_spawn_smoke(smoke_stack *smoke)
{
    if (!simulation_enabled) {
        return;
    }
    sf_web_activate_smoke_slot(sf_web_smoke_slot(smoke), smoke);
}

void sf_web_simulation_spawn_bit(bit_stack *bit)
{
    if (!simulation_enabled) {
        return;
    }
    sf_web_activate_bit_slot(sf_web_bit_slot(bit), bit);
}

void sf_web_simulation_advance_laser(laser_stack *laser)
{
    SFWebSimulationSlot *slot = sf_web_laser_slot(laser);
    SFWebSimulationKinematics *state = sf_web_simulation_laser(laser);

    if (slot == NULL || state == NULL) {
        return;
    }
    sf_web_integrate_position(&state->position, state->velocity_x,
        state->velocity_y, state->velocity_z);
    sf_web_integrate_position(&state->position2, state->velocity_x,
        state->velocity_y, state->velocity_z);
    sf_web_project_laser_slot(slot, laser);
}

void sf_web_simulation_age_laser(laser_stack *laser)
{
    SFWebSimulationSlot *slot = sf_web_laser_slot(laser);
    SFWebSimulationKinematics *state = sf_web_simulation_laser(laser);

    if (slot == NULL || state == NULL) {
        return;
    }
    --state->lifetime_ticks;
    sf_web_project_laser_slot(slot, laser);
}

void sf_web_simulation_set_laser_lifetime(laser_stack *laser, long counter)
{
    SFWebSimulationSlot *slot;
    SFWebSimulationKinematics *state;

    if (!simulation_enabled) {
        laser->counter = counter;
        return;
    }
    slot = sf_web_laser_slot(laser);
    state = sf_web_simulation_laser(laser);
    if (slot == NULL || state == NULL) {
        return;
    }
    state->lifetime_ticks = sf_web_lifetime_from_legacy(counter);
    sf_web_project_laser_slot(slot, laser);
}

void sf_web_simulation_advance_smoke(smoke_stack *smoke)
{
    SFWebSimulationSlot *slot = sf_web_smoke_slot(smoke);
    SFWebSimulationKinematics *state = sf_web_simulation_smoke(smoke);
    float drag = powf(63.0f / 64.0f, 1.0f / 5.0f);

    if (slot == NULL || state == NULL) {
        return;
    }
    state->velocity_x *= drag;
    state->velocity_y *= drag;
    state->velocity_z *= drag;
    sf_web_integrate_position(&state->position, state->velocity_x,
        state->velocity_y, state->velocity_z);
    --state->lifetime_ticks;
    sf_web_project_smoke_slot(slot, smoke);
}

void sf_web_simulation_clamp_smoke_to_height(smoke_stack *smoke, long height)
{
    SFWebSimulationSlot *slot;
    SFWebSimulationKinematics *state;

    if (!simulation_enabled) {
        smoke->z_pos = height;
        if (smoke->z_vel < 0) {
            smoke->z_vel = -smoke->z_vel;
        }
        return;
    }
    slot = sf_web_smoke_slot(smoke);
    state = sf_web_simulation_smoke(smoke);
    if (slot == NULL || state == NULL) {
        return;
    }
    sf_web_position_set_z_raw(&state->position, (int32_t)height);
    if (state->velocity_z < 0.0f) {
        state->velocity_z = -state->velocity_z;
    }
    sf_web_project_smoke_slot(slot, smoke);
}

void sf_web_simulation_advance_bit(bit_stack *bit,
                                   long gravity_velocity_delta)
{
    SFWebSimulationSlot *slot = sf_web_bit_slot(bit);
    SFWebSimulationKinematics *state = sf_web_simulation_bit(bit);
    float drag = powf(63.0f / 64.0f, 1.0f / 5.0f);

    if (slot == NULL || state == NULL) {
        return;
    }
    sf_web_integrate_position(&state->position, state->velocity_x,
        state->velocity_y, state->velocity_z);
    state->x_radians = sf_normalize_radians(state->x_radians +
        state->angular_velocity_x * SF_WEB_SIMULATION_DT);
    state->y_radians = sf_normalize_radians(state->y_radians +
        state->angular_velocity_y * SF_WEB_SIMULATION_DT);
    state->z_radians = sf_normalize_radians(state->z_radians +
        state->angular_velocity_z * SF_WEB_SIMULATION_DT);
    state->velocity_x *= drag;
    state->velocity_y *= drag;
    state->velocity_z *= drag;
    state->angular_velocity_x *= drag;
    state->angular_velocity_y *= drag;
    state->angular_velocity_z *= drag;
    state->velocity_z += (float)(int32_t)gravity_velocity_delta *
        (SF_WEB_LEGACY_UPDATES_PER_SECOND * SF_WEB_SIMULATION_DT) *
        SF_WEB_LEGACY_UPDATES_PER_SECOND / SF_WEB_WORLD_CELL_RAW;
    sf_web_project_bit_slot(slot, bit);
}

void sf_web_simulation_set_bit_lifetime(bit_stack *bit, long counter)
{
    SFWebSimulationSlot *slot;
    SFWebSimulationKinematics *state;

    if (!simulation_enabled) {
        bit->counter = counter;
        return;
    }
    slot = sf_web_bit_slot(bit);
    state = sf_web_simulation_bit(bit);
    if (slot == NULL || state == NULL) {
        return;
    }
    state->lifetime_ticks = sf_web_lifetime_from_legacy(counter);
    sf_web_project_bit_slot(slot, bit);
}

void sf_web_simulation_adjust_bit_lifetime(bit_stack *bit, long legacy_delta)
{
    SFWebSimulationSlot *slot;
    SFWebSimulationKinematics *state;

    if (!simulation_enabled) {
        bit->counter += legacy_delta;
        return;
    }
    slot = sf_web_bit_slot(bit);
    state = sf_web_simulation_bit(bit);
    if (slot == NULL || state == NULL) {
        return;
    }
    state->lifetime_ticks += sf_web_lifetime_from_legacy(legacy_delta);
    sf_web_project_bit_slot(slot, bit);
}

void sf_web_simulation_bounce_bit(bit_stack *bit,
    long x_velocity_delta, long y_velocity_delta,
    long x_angular_velocity, long y_angular_velocity,
    long z_angular_velocity)
{
    SFWebSimulationSlot *slot;
    SFWebSimulationKinematics *state;

    if (!simulation_enabled) {
        bit->x_vel += x_velocity_delta;
        bit->y_vel += y_velocity_delta;
        bit->z_vel = -bit->z_vel;
        bit->x_r_vel = x_angular_velocity;
        bit->y_r_vel = y_angular_velocity;
        bit->z_r_vel = z_angular_velocity;
        return;
    }
    slot = sf_web_bit_slot(bit);
    state = sf_web_simulation_bit(bit);
    if (slot == NULL || state == NULL) {
        return;
    }
    state->velocity_x += sf_web_velocity_from_raw((int32_t)x_velocity_delta);
    state->velocity_y += sf_web_velocity_from_raw((int32_t)y_velocity_delta);
    state->velocity_z = -state->velocity_z;
    state->angular_velocity_x = sf_web_angular_velocity_from_raw(
        (int32_t)x_angular_velocity);
    state->angular_velocity_y = sf_web_angular_velocity_from_raw(
        (int32_t)y_angular_velocity);
    state->angular_velocity_z = sf_web_angular_velocity_from_raw(
        (int32_t)z_angular_velocity);
    sf_web_project_bit_slot(slot, bit);
}

void sf_web_simulation_age_bit(bit_stack *bit)
{
    SFWebSimulationSlot *slot;
    SFWebSimulationKinematics *state;

    if (!simulation_enabled) {
        bit->counter += sf_web_fixed_step_scale_legacy_delta(-3);
        return;
    }
    slot = sf_web_bit_slot(bit);
    state = sf_web_simulation_bit(bit);
    if (slot == NULL || state == NULL) {
        return;
    }
    state->lifetime_ticks -= 3;
    sf_web_project_bit_slot(slot, bit);
}
void sf_web_simulation_set_camera_pose(camera_data *value, long x_position,
    long y_position, long z_position, long x_rotation, long y_rotation,
    long z_rotation)
{
    SFWebSimulationKinematics *state = sf_web_simulation_camera(value);

    if (state == NULL) {
        return;
    }
    sf_web_position_from_raw(&state->position, (int32_t)x_position,
        (int32_t)y_position, (int32_t)z_position);
    state->x_radians = sf_legacy_rotation_to_radians(x_rotation);
    state->y_radians = sf_legacy_rotation_to_radians(y_rotation);
    state->z_radians = sf_legacy_rotation_to_radians(z_rotation);
}

void sf_web_simulation_project_camera_pose(camera_data *value,
    long *x_position, long *y_position, long *z_position,
    long *x_rotation, long *y_rotation, long *z_rotation)
{
    SFWebSimulationKinematics *state = sf_web_simulation_camera(value);

    if (state == NULL) {
        return;
    }
    *x_position = sf_web_raw_from_cell_local(state->position.cell_x,
        state->position.local_x);
    *y_position = sf_web_raw_from_cell_local(state->position.cell_y,
        state->position.local_y);
    *z_position = sf_web_raw_from_cell_local(state->position.cell_z,
        state->position.local_z);
    *x_rotation = sf_radians_to_legacy_rotation(state->x_radians) >> 10;
    *y_rotation = sf_radians_to_legacy_rotation(state->y_radians) >> 10;
    *z_rotation = sf_radians_to_legacy_rotation(state->z_radians) >> 10;
}

static void sf_web_begin_laser_pool(void)
{
    size_t index;

    for (index = 0; index < MAX_LASERS; ++index) {
        laser_stack *laser = &lasers.laser_item[index];
        SFWebSimulationSlot *slot = &laser_slots[index];

        if (laser->header.status == 1 && laser->type != SF_WEB_BEAM_LASER_TYPE) {
            if (!slot->state.active) {
                sf_web_activate_laser_slot(slot, laser);
            } else {
                sf_web_project_laser_slot(slot, laser);
            }
        } else {
            slot->state.active = 0;
        }
    }
}

static void sf_web_begin_smoke_pool(void)
{
    size_t index;

    for (index = 0; index < MAX_SMOKES; ++index) {
        smoke_stack *smoke = &smokes.smoke_item[index];
        SFWebSimulationSlot *slot = &smoke_slots[index];

        if (smoke->header.status == 1) {
            if (!slot->state.active) {
                sf_web_activate_smoke_slot(slot, smoke);
            } else {
                sf_web_project_smoke_slot(slot, smoke);
            }
        } else {
            slot->state.active = 0;
        }
    }
}

static void sf_web_begin_bit_pool(void)
{
    size_t index;

    for (index = 0; index < MAX_BITS; ++index) {
        bit_stack *bit = &bits.bit_item[index];
        SFWebSimulationSlot *slot = &bit_slots[index];

        if (bit->header.status == 1) {
            if (!slot->state.active) {
                sf_web_activate_bit_slot(slot, bit);
            } else {
                sf_web_project_bit_slot(slot, bit);
            }
        } else {
            slot->state.active = 0;
        }
    }
}

void sf_web_simulation_reset(void)
{
    memset(laser_slots, 0, sizeof(laser_slots));
    memset(smoke_slots, 0, sizeof(smoke_slots));
    memset(bit_slots, 0, sizeof(bit_slots));
    memset(camera_slots, 0, sizeof(camera_slots));
    simulation_enabled = 0;
}

void sf_web_simulation_begin_step(void)
{
    simulation_enabled = 1;
    sf_web_begin_laser_pool();
    sf_web_begin_smoke_pool();
    sf_web_begin_bit_pool();
}

int sf_web_simulation_is_enabled(void)
{
    return simulation_enabled;
}

void sf_web_simulation_project_compatibility_step(void)
{
    size_t index;

    for (index = 0; index < MAX_LASERS; ++index) {
        if (lasers.laser_item[index].header.status == 1 &&
            laser_slots[index].state.active) {
            sf_web_project_laser_slot(&laser_slots[index],
                &lasers.laser_item[index]);
        }
    }
    for (index = 0; index < MAX_SMOKES; ++index) {
        if (smokes.smoke_item[index].header.status == 1 &&
            smoke_slots[index].state.active) {
            sf_web_project_smoke_slot(&smoke_slots[index],
                &smokes.smoke_item[index]);
        }
    }
    for (index = 0; index < MAX_BITS; ++index) {
        if (bits.bit_item[index].header.status == 1 &&
            bit_slots[index].state.active) {
            sf_web_project_bit_slot(&bit_slots[index], &bits.bit_item[index]);
        }
    }
}

uint64_t sf_web_simulation_tick(void)
{
    return sf_web_current_tick();
}

uint64_t sf_web_simulation_deadline_after(uint64_t ticks)
{
    return sf_web_current_tick() + ticks;
}

int sf_web_simulation_deadline_reached(uint64_t deadline)
{
    return sf_web_current_tick() >= deadline;
}

float sf_web_simulation_terrain_height(const SFWebSimulationPosition *position)
{
    int x = position->cell_x & 255;
    int y = position->cell_y & 255;
    float top_left = fmaxf((float)height_map[y][x] - 17.0f, 0.0f);
    float top_right = fmaxf((float)height_map[y][(x + 1) & 255] - 17.0f, 0.0f);
    float bottom_left = fmaxf((float)height_map[(y + 1) & 255][x] - 17.0f, 0.0f);
    float bottom_right = fmaxf((float)height_map[(y + 1) & 255][(x + 1) & 255] - 17.0f,
        0.0f);
    float top = top_left + (top_right - top_left) * position->local_x;
    float bottom = bottom_left + (bottom_right - bottom_left) * position->local_x;

    return top + (bottom - top) * position->local_y;
}