#include "sf_arm_port.h"
#include "../SFlib/SF_ARMLink.h"

#include <assert.h>
#include <stdio.h>

sf_arm_i32 cosine_table[2048];
unsigned char tangent_table[4100];
unsigned char poly_map[128][128];
unsigned char height_map[256][256];
sf_arm_i32 graphics_data[19000];
sf_arm_i32 camera_x_rotation;
sf_arm_i32 camera_y_rotation;
sf_arm_i32 camera_z_rotation;

void arm_addgamecel(void *cel_quad, long cel, long width, long height)
{
    (void)cel_quad;
    (void)cel;
    (void)width;
    (void)height;
}

void rotate_land_node_from_c(void *data);
void rotate_node_from_c(void *data);
long find_rotation(long x, long y);
long find_2d_distance(long x, long y);
long divide(long numerator, long denominator);
int target_finder(void *data);
int scan_poly_map(long x, long y, void *results);
int scan_poly_map_2(long x, long y, void *results);
void arm_randominit(void);
long arm_random(void);
long arm_randomvalue(long maximum);
void armlink_initialise(long maximum_items, void *list_address, long item_size,
                        long sort_word);
void *armlink_sorttolist(void *list_address, long sort_value);
void armlink_deleteitem(void *item_address, void *list_address);
void armlink_suspenditem(void *item_address, void *list_address);
void armlink_releaseitem(void *item_address, void *list_address);
void armlink_resortlist(void *list_address);
void armzsort_create(void *list_address, long maximum_items);
void armzsort_initialise(void);
void armzsort_add(long distance, void *graphic_address, long reference);
void setup_collision_constants(long space_mission);
long find_ground_height(long x_position, long y_position);
long check_collision(long x_position, long y_position, long z_position);

static void set_tangent(unsigned int index, sf_arm_i32 value)
{
    tangent_table[index * 4U] = (unsigned char)(sf_arm_u32)value;
    tangent_table[index * 4U + 1U] = (unsigned char)((sf_arm_u32)value >> 8U);
    tangent_table[index * 4U + 2U] = (unsigned char)((sf_arm_u32)value >> 16U);
    tangent_table[index * 4U + 3U] = (unsigned char)((sf_arm_u32)value >> 24U);
}

static void initialise_tables(void)
{
    cosine_table[0] = 4096;
    cosine_table[256] = 0;
    cosine_table[512] = -4096;
    cosine_table[768] = 0;
    cosine_table[1024] = 4096;
    cosine_table[768] = 0;
    cosine_table[1024] = 4096;
    cosine_table[1280] = 0;
    cosine_table[1536] = -4096;
    cosine_table[1792] = 0;

    /* sine_table starts at cosine_table[768]. */
    cosine_table[768 + 0] = 0;
    cosine_table[768 + 256] = 4096;
    cosine_table[768 + 512] = 0;
    cosine_table[768 + 768] = -4096;
    cosine_table[768 + 1024] = 0;
    set_tangent(0, 0);
    set_tangent(1024, 128 * 1024);
}

static void test_fixed_point_math(void)
{
    sf_arm_vec3 vector = { 4096, 0, 0 };
    sf_arm_i32 node[6] = { 4096, 0, 0, 256 << 10, 0, 0 };
    sf_arm_i32 target[9] = { 0, 0, 0, 16384, 0, 0, 0, 0, 0 };

    sf_arm_rotate_coords_x(&vector, 256);
    assert(vector.x == 0 && vector.y == 4096 && vector.z == 0);

    rotate_land_node_from_c(node);
    assert(node[0] == 0 && node[1] == 4096 && node[2] == 0);

    node[0] = 4096 << 12;
    node[1] = 0;
    node[2] = 0;
    rotate_node_from_c(node);
    assert(node[0] == 0 && node[1] == -(4096 << 12) && node[2] == 0);

    assert(divide(10240, 10) == 1024);
    assert(divide(-10240, 10) == -1024);
    assert((sf_arm_u32)divide(1, 0) == 0x7fffffffU);
    assert((sf_arm_u32)divide(-1, 0) == 0x80000001U);
    assert(find_rotation(0, 1) == 0);
    assert(find_rotation(0, -1) == 512 * 1024);
    assert(find_rotation(-1, 0) == 256 * 1024);
    assert(find_rotation(1, 0) == 256 * 3 * 1024);
    assert(find_rotation(1, 1) == 896 * 1024);
    assert(find_2d_distance(4096, 0) == 4096);

    assert(target_finder(target) == 0);
    assert(target[6] == 256 * 3 * 1024);
    assert(target[7] == 0);
    assert(target[8] == 16384);
}

static void test_random_and_map_scans(void)
{
    sf_arm_i32 results[1025];

    arm_randominit();
    assert((sf_arm_u32)arm_random() == 0x00000c01U);
    assert((sf_arm_u32)arm_random() == 0x00001800U);
    assert((sf_arm_u32)arm_random() == 0x00001c03U);
    arm_randominit();
    assert(arm_randomvalue(257) == 1);

    poly_map[127][124] = 8;
    poly_map[0][0] = 9;
    assert(scan_poly_map(126, 127, results) == 0);
    assert(results[0] == 127 * 128 + 124);
    assert(results[1] == -1);
    assert(scan_poly_map_2(126, 127, results) == 0);
    assert(results[0] == 127 * 128 + 124);
    assert(results[1] == 0);
    assert(results[2] == -1);
}

typedef struct test_link_item {
    link_header header;
    long sort_key;
} test_link_item;

typedef struct test_link_list {
    linked_list list;
    test_link_item item[4];
} test_link_list;

static void test_link_routines(void)
{
    test_link_list list = { 0 };
    test_link_item *first;
    test_link_item *second;
    test_link_item *third;
    zsort_list sorted = { 0 };
    zsort_item *entry;

    armlink_initialise(4, &list, sizeof(list.item[0]), 0);
    first = (test_link_item *)armlink_sorttolist(&list, 10);
    second = (test_link_item *)armlink_sorttolist(&list, 5);
    third = (test_link_item *)armlink_sorttolist(&list, 10);
    first->sort_key = 10;
    second->sort_key = 5;
    third->sort_key = 10;
    /*
     * The historic caller writes the sort word after allocation.  Re-sort
     * once to reflect those writes, just as collision_update does.
     */
    armlink_resortlist(&list);
    assert(list.list.start_address == second);
    assert(second->header.next_address == first);
    assert(first->header.next_address == third);

    armlink_deleteitem(first, &list);
    assert(second->header.next_address == third);
    armlink_suspenditem(third, &list);
    assert(list.list.start_address == second);
    armlink_releaseitem(third, &list);
    armlink_resortlist(&list);
    assert(list.list.start_address == second);
    assert(second->header.next_address == third);

    armzsort_create(&sorted, 3);
    armzsort_initialise();
    armzsort_add(0x01000010L, first, 1);
    armzsort_add(0x01000020L, second, 2);
    armzsort_add(0x01000018L, third, 3);
    entry = (zsort_item *)sorted.offset[1];
    assert(entry->distance == 0x01000020L && entry->ref == 2);
    entry = (zsort_item *)entry->next_address;
    assert(entry->distance == 0x01000018L && entry->ref == 3);
    entry = (zsort_item *)entry->next_address;
    assert(entry->distance == 0x01000010L && entry->ref == 1);
    assert(entry->next_address == NULL);
}

static void test_ground_collision(void)
{
    memset(poly_map, 0, sizeof(poly_map));
    height_map[0][0] = 18;
    height_map[0][1] = 18;
    height_map[1][0] = 18;
    height_map[1][1] = 18;

    setup_collision_constants(0);
    assert(find_ground_height(0, 0) == (1 << 21));
    assert(check_collision(0, 0, 0) == ~(1 << 21));
    assert(check_collision(0, 0, 1 << 21) == 0);
    setup_collision_constants(1);
    assert(check_collision(0, 0, 0) == 0);
}

int main(void)
{
    initialise_tables();
    test_fixed_point_math();
    test_random_and_map_scans();
    test_link_routines();
    test_ground_collision();
    puts("portable ARM routine tests passed");
    return 0;
}
