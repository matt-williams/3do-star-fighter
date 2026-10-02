#ifndef SF_WEB_SIMULATION_H
#define SF_WEB_SIMULATION_H

#include <stdint.h>

typedef struct SFWebSimulationPosition {
    int32_t cell_x;
    int32_t cell_y;
    int32_t cell_z;
    float local_x;
    float local_y;
    float local_z;
} SFWebSimulationPosition;

typedef struct SFWebSimulationKinematics {
    SFWebSimulationPosition position;
    SFWebSimulationPosition position2;
    float velocity_x;
    float velocity_y;
    float velocity_z;
    float x_radians;
    float y_radians;
    float z_radians;
    float angular_velocity_x;
    float angular_velocity_y;
    float angular_velocity_z;
    int64_t lifetime_ticks;
    uint64_t activated_tick;
    uint8_t active;
} SFWebSimulationKinematics;

struct ship_stack;
struct laser_stack;
struct smoke_stack;
struct bit_stack;
struct camera_data;

/*
 * Browser simulation owns these sidecars.  Entity layouts retain their
 * historic raw prefix; legacy fields are compatibility projections only.
 */
void sf_web_simulation_reset(void);
void sf_web_simulation_begin_step(void);
int sf_web_simulation_is_enabled(void);
void sf_web_simulation_project_compatibility_step(void);
uint64_t sf_web_simulation_tick(void);
uint64_t sf_web_simulation_deadline_after(uint64_t ticks);
int sf_web_simulation_deadline_reached(uint64_t deadline);

SFWebSimulationKinematics *sf_web_simulation_ship(struct ship_stack *ship);
SFWebSimulationKinematics *sf_web_simulation_laser(struct laser_stack *laser);
SFWebSimulationKinematics *sf_web_simulation_smoke(struct smoke_stack *smoke);
SFWebSimulationKinematics *sf_web_simulation_bit(struct bit_stack *bit);
SFWebSimulationKinematics *sf_web_simulation_camera(struct camera_data *camera);

void sf_web_simulation_spawn_laser(struct laser_stack *laser);
void sf_web_simulation_spawn_smoke(struct smoke_stack *smoke);
void sf_web_simulation_spawn_bit(struct bit_stack *bit);
void sf_web_simulation_advance_laser(struct laser_stack *laser);
void sf_web_simulation_age_laser(struct laser_stack *laser);
void sf_web_simulation_set_laser_lifetime(struct laser_stack *laser,
    long counter);

void sf_web_simulation_advance_smoke(struct smoke_stack *smoke);
void sf_web_simulation_clamp_smoke_to_height(struct smoke_stack *smoke,
    long height);
void sf_web_simulation_advance_bit(struct bit_stack *bit,
    long gravity_velocity_delta);
void sf_web_simulation_set_bit_lifetime(struct bit_stack *bit, long counter);
void sf_web_simulation_adjust_bit_lifetime(struct bit_stack *bit,
    long legacy_delta);
void sf_web_simulation_bounce_bit(struct bit_stack *bit,
    long x_velocity_delta, long y_velocity_delta,
    long x_angular_velocity, long y_angular_velocity,
    long z_angular_velocity);
void sf_web_simulation_age_bit(struct bit_stack *bit);

void sf_web_simulation_set_camera_pose(struct camera_data *camera,
    long x_position, long y_position, long z_position,
    long x_rotation, long y_rotation, long z_rotation);
void sf_web_simulation_project_camera_pose(struct camera_data *camera,
    long *x_position, long *y_position, long *z_position,
    long *x_rotation, long *y_rotation, long *z_rotation);

float sf_web_simulation_terrain_height(const SFWebSimulationPosition *position);

#endif
