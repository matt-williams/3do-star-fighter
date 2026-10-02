#ifndef SF_ARM_PORT_H
#define SF_ARM_PORT_H

#include <stdint.h>
#include <string.h>

/*
 * The original routines operate on 32-bit ARM registers.  Keeping every
 * intermediate in an explicit 32-bit type avoids undefined signed overflow
 * and makes their fixed-point results independent of the host C ABI.
 */
typedef int32_t sf_arm_i32;
typedef uint32_t sf_arm_u32;

typedef struct sf_arm_vec3 {
    sf_arm_i32 x;
    sf_arm_i32 y;
    sf_arm_i32 z;
} sf_arm_vec3;

static inline sf_arm_i32 sf_arm_add(sf_arm_i32 left, sf_arm_i32 right)
{
    return (sf_arm_i32)((sf_arm_u32)left + (sf_arm_u32)right);
}

static inline sf_arm_i32 sf_arm_sub(sf_arm_i32 left, sf_arm_i32 right)
{
    return (sf_arm_i32)((sf_arm_u32)left - (sf_arm_u32)right);
}

static inline sf_arm_i32 sf_arm_neg(sf_arm_i32 value)
{
    return (sf_arm_i32)(0U - (sf_arm_u32)value);
}

static inline sf_arm_i32 sf_arm_mul(sf_arm_i32 left, sf_arm_i32 right)
{
    return (sf_arm_i32)((sf_arm_u32)left * (sf_arm_u32)right);
}

static inline sf_arm_i32 sf_arm_lsl(sf_arm_i32 value, unsigned int shift)
{
    return shift >= 32U ? 0 : (sf_arm_i32)((sf_arm_u32)value << shift);
}

static inline sf_arm_i32 sf_arm_asr(sf_arm_i32 value, unsigned int shift)
{
    sf_arm_u32 bits = (sf_arm_u32)value;

    if (shift == 0U) {
        return value;
    }
    if (shift >= 32U) {
        return value < 0 ? -1 : 0;
    }

    bits >>= shift;
    if (value < 0) {
        bits |= ~(UINT32_MAX >> shift);
    }
    return (sf_arm_i32)bits;
}

static inline sf_arm_u32 sf_arm_ror(sf_arm_u32 value, sf_arm_u32 shift)
{
    shift &= 31U;
    return shift == 0U ? value : (value >> shift) | (value << (32U - shift));
}

static inline sf_arm_i32 sf_arm_abs(sf_arm_i32 value)
{
    return value < 0 ? sf_arm_neg(value) : value;
}

static inline sf_arm_i32 sf_arm_load_i32(const void *address)
{
    sf_arm_i32 value;
    memcpy(&value, address, sizeof(value));
    return value;
}

static inline void sf_arm_store_i32(void *address, sf_arm_i32 value)
{
    memcpy(address, &value, sizeof(value));
}

static inline void *sf_arm_address32(sf_arm_u32 address)
{
    return (void *)(uintptr_t)address;
}

static inline sf_arm_u32 sf_arm_pointer32(const void *address)
{
    return (sf_arm_u32)(uintptr_t)address;
}

void sf_arm_rotate_coords_x(sf_arm_vec3 *coords, sf_arm_i32 rotation);
void sf_arm_rotate_coords_y(sf_arm_vec3 *coords, sf_arm_i32 rotation);
void sf_arm_rotate_coords_z(sf_arm_vec3 *coords, sf_arm_i32 rotation);
void sf_arm_rotate_camera(sf_arm_vec3 *coords);
void sf_armburn_bind_buffers(void *sprites4x4, void *map512, void *cache);

#endif
