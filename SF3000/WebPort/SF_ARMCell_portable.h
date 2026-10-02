#ifndef SF3000_WEBPORT_SF_ARMCELL_PORTABLE_H
#define SF3000_WEBPORT_SF_ARMCELL_PORTABLE_H

#include <stddef.h>
#include <stdint.h>

#include "sf_web_renderer.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SF_ARMCELL_MAX_TEMP_CELS 1024u

/*
 * The legacy cel_celdata ABI uses 32-bit long values and this exact field
 * sequence.  It is layout-compatible with cel_celdata on the wasm32 target;
 * the CCB plotlist remains only an opaque compatibility field.
 */
typedef struct SFArmCellData {
    int32_t temp_cels;
    int32_t x_pos0;
    int32_t y_pos0;
    int32_t x_pos1;
    int32_t y_pos1;
    int32_t x_pos2;
    int32_t y_pos2;
    int32_t x_pos3;
    int32_t y_pos3;
    void *plotlist;
    int32_t shade;
    uint8_t *cel_palette;
    uint8_t *cel_creation;
    uint8_t *cels4x4;
    uint8_t *cel_list16;
    uint8_t *cel_list32;
    uint8_t *cel_game;
    uint8_t *cel_map512;
    uint8_t *cel_map128;
    uint8_t *cache_lookup;
    uint8_t *cache_free;
    int32_t cache_freecount;
    uint8_t *screen_address0;
    uint8_t *screen_address1;
    uint8_t *cel_map32;
    uint8_t *cel_codedpalette;
    uint8_t *free;
} SFArmCellData;

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(offsetof(SFArmCellData, temp_cels) == 0,
               "cel_celdata.temp_cels offset changed");
_Static_assert(offsetof(SFArmCellData, x_pos0) == 4,
               "cel_celdata quad offset changed");
_Static_assert(offsetof(SFArmCellData, y_pos3) == 32,
               "cel_celdata quad offset changed");
#if UINTPTR_MAX == UINT32_MAX
_Static_assert(offsetof(SFArmCellData, plotlist) == 36,
               "wasm32 cel_celdata.plotlist offset changed");
_Static_assert(offsetof(SFArmCellData, shade) == 40,
               "wasm32 cel_celdata.shade offset changed");
_Static_assert(offsetof(SFArmCellData, cel_codedpalette) == 100,
               "wasm32 cel_celdata.cel_codedpalette offset changed");
#endif
#endif

/* Legacy SF_ARMCell.s ABI. */
void arm_addpolycel16(void *, long);
void arm_addpolycel32(void *, long);
void arm_setpolycel32palette(void *, long);
void arm_add4cel4(void *, void *, long, long);
void arm_addgamecel(void *, long, long, long);
void arm_celinitialisecreation(void *, long);
void arm_generatemaps(void *, void *);
void arm_addcelfrom512map(void *, long, long, long);
void arm_addcelfrom128map(void *, long, long, long);
void arm_addcelfrom32map(void *, long, long, long);
void arm_initialisecache(void *);
void arm_updatecache(void *);
void arm_addmonocel(void *, long, long, long);
void arm_setcel_hw(void *, long, long);
void arm_interceptplot(void *);
void arm_setgamecelpalette(void *, long);

#ifdef __cplusplus
}
#endif

#endif
