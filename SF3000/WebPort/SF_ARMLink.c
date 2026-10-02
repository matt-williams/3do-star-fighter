#include "sf_arm_port.h"
#include "../SFlib/SF_ARMLink.h"

#include <stddef.h>

#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(link_header) == 12, "link_header must retain its ARM ABI");
_Static_assert(sizeof(linked_list) == 12, "linked_list must retain its ARM ABI");
_Static_assert(offsetof(link_header, status) == 8, "link status offset changed");
#endif

static sf_arm_i32 sf_link_sort_value(const linked_list *list, const link_header *item)
{
    return sf_arm_load_i32((const unsigned char *)item + (size_t)list->sortword);
}

void armlink_initialise(long maximum_items, void *list_address, long item_size,
                        long sort_word)
{
    linked_list *list = (linked_list *)list_address;
    unsigned char *item = (unsigned char *)list_address + sizeof(*list);
    long index;

    if (maximum_items <= 0) {
        return;
    }

    list->start_address = item;
    list->next_freeaddress = item;
    list->sortword = sort_word + (long)sizeof(link_header);

    for (index = 0; index < maximum_items; ++index, item += item_size) {
        link_header *header = (link_header *)item;

        header->prev_address = index == 0 ? NULL : item - item_size;
        header->next_address = index + 1 == maximum_items ? NULL : item + item_size;
        header->status = index + 1 == maximum_items ? 2 : 0;
    }
}

void *armlink_addtolist(void *list_address)
{
    linked_list *list = (linked_list *)list_address;
    link_header *item = (link_header *)list->next_freeaddress;

    if (item == NULL || item->status == 2) {
        return NULL;
    }

    item->status = 1;
    list->next_freeaddress = item->next_address;
    return item;
}

void *armlink_sorttolist(void *list_address, long sort_value)
{
    linked_list *list = (linked_list *)list_address;
    link_header *item = (link_header *)list->next_freeaddress;
    link_header *free_previous;
    link_header *free_next;
    link_header *previous = NULL;
    link_header *current;
    sf_arm_i32 requested = (sf_arm_i32)sort_value;

    if (item == NULL || item->status == 2) {
        return NULL;
    }

    free_previous = (link_header *)item->prev_address;
    free_next = (link_header *)item->next_address;
    list->next_freeaddress = free_next;
    if (item == (link_header *)list->start_address) {
        list->start_address = free_next;
    }
    if (free_previous != NULL) {
        free_previous->next_address = free_next;
    }
    if (free_next != NULL) {
        free_next->prev_address = free_previous;
    }

    current = (link_header *)list->start_address;
    item->status = 1;
    while (current != NULL && current->status == 1 &&
           sf_link_sort_value(list, current) <= requested) {
        previous = current;
        current = (link_header *)current->next_address;
    }

    item->prev_address = previous;
    item->next_address = current;
    if (previous != NULL) {
        previous->next_address = item;
    } else {
        list->start_address = item;
    }
    if (current != NULL) {
        current->prev_address = item;
    }

    return item;
}

void armlink_resortlist(void *list_address)
{
    linked_list *list = (linked_list *)list_address;
    link_header *current = (link_header *)list->start_address;
    link_header *sorted = NULL;
    link_header *sorted_tail = NULL;

    if (current == NULL || current->status != 1) {
        return;
    }

    while (current != NULL && current->status == 1) {
        link_header *next = (link_header *)current->next_address;
        link_header *previous = NULL;
        link_header *position = sorted;
        sf_arm_i32 value = sf_link_sort_value(list, current);

        while (position != NULL && position->status == 1 &&
               sf_link_sort_value(list, position) <= value) {
            previous = position;
            position = (link_header *)position->next_address;
        }

        current->prev_address = previous;
        current->next_address = position;
        if (previous != NULL) {
            previous->next_address = current;
        } else {
            sorted = current;
        }
        if (position != NULL) {
            position->prev_address = current;
        } else {
            sorted_tail = current;
        }

        current = next;
    }

    list->start_address = sorted;
    sorted->prev_address = NULL;
    if (sorted_tail != NULL) {
        sorted_tail->next_address = current;
    }
    if (current != NULL) {
        current->prev_address = sorted_tail;
    }
}

void armlink_deleteitem(void *item_address, void *list_address)
{
    linked_list *list = (linked_list *)list_address;
    link_header *item = (link_header *)item_address;
    link_header *previous = (link_header *)item->prev_address;
    link_header *next = (link_header *)item->next_address;
    link_header *free_item = (link_header *)list->next_freeaddress;
    link_header *free_previous;

    if (item != (link_header *)list->start_address) {
        if (previous != NULL) {
            previous->next_address = next;
        }
        if (next != NULL) {
            next->prev_address = previous;
        }
    } else {
        item->prev_address = previous;
        if (next != NULL && next->status == 1) {
            next->prev_address = previous;
            list->start_address = next;
        }
    }

    list->next_freeaddress = item;
    free_previous = free_item == NULL ? NULL : (link_header *)free_item->prev_address;
    if (free_previous != NULL) {
        free_previous->next_address = item;
    }
    item->prev_address = free_previous;
    item->next_address = free_item;
    if (free_item != NULL) {
        free_item->prev_address = item;
    }
    item->status = 0;

    if (list->start_address != NULL) {
        ((link_header *)list->start_address)->prev_address = NULL;
    }
}

void armlink_suspenditem(void *item_address, void *list_address)
{
    linked_list *list = (linked_list *)list_address;
    link_header *item = (link_header *)item_address;
    link_header *previous = (link_header *)item->prev_address;
    link_header *next = (link_header *)item->next_address;

    if (next != NULL) {
        next->prev_address = previous;
    }
    if (previous != NULL) {
        previous->next_address = next;
    } else {
        list->start_address = next;
    }
}

void armlink_releaseitem(void *item_address, void *list_address)
{
    linked_list *list = (linked_list *)list_address;
    link_header *item = (link_header *)item_address;
    link_header *old_start = (link_header *)list->start_address;

    list->start_address = item;
    item->prev_address = NULL;
    item->next_address = old_start;
    if (old_start != NULL) {
        old_start->prev_address = item;
    }
}

static zsort_list *sf_zsort_list;
static zsort_item *sf_zsort_next;
static sf_arm_i32 sf_zsort_count;
static sf_arm_i32 sf_zsort_maximum;

void armzsort_create(void *list_address, long maximum_items)
{
    unsigned int index;

    sf_zsort_list = (zsort_list *)list_address;
    sf_zsort_count = (sf_arm_i32)maximum_items;
    sf_zsort_maximum = (sf_arm_i32)maximum_items;
    sf_zsort_next = NULL;
    for (index = 0; index < 128U; ++index) {
        sf_zsort_list->offset[index] = NULL;
    }
}

void armzsort_initialise(void)
{
    if (sf_zsort_list == NULL) {
        return;
    }

    sf_zsort_next = &sf_zsort_list->graphic[0];
    sf_zsort_count = sf_zsort_maximum;
}

void armzsort_add(long distance, void *graphic_address, long reference)
{
    sf_arm_i32 sorted_distance = (sf_arm_i32)distance;
    unsigned int bucket;
    zsort_item *item;
    zsort_item *current;

    if (sf_zsort_list == NULL || sf_zsort_next == NULL || sf_zsort_count <= 0) {
        return;
    }
    --sf_zsort_count;

    bucket = ((sf_arm_u32)sorted_distance) >> 24;
    item = sf_zsort_next++;
    current = (zsort_item *)sf_zsort_list->offset[bucket];

    item->distance = (long)sorted_distance;
    item->graphic_address = graphic_address;
    item->ref = reference;
    if (current == NULL || sorted_distance >= (sf_arm_i32)current->distance) {
        item->next_address = current;
        sf_zsort_list->offset[bucket] = item;
        return;
    }

    while (current->next_address != NULL &&
           sorted_distance < (sf_arm_i32)((zsort_item *)current->next_address)->distance) {
        current = (zsort_item *)current->next_address;
    }
    item->next_address = current->next_address;
    current->next_address = item;
}

typedef struct sf_arm_collision_ship {
    link_header header;
    long x_pos;
    long y_pos;
    long z_pos;
    long x_rot;
    long y_rot;
    long z_rot;
    long type;
    long collision_size;
    long who_owns_me;
    long what_hit_me;
} sf_arm_collision_ship;

typedef struct sf_arm_collision_laser {
    link_header header;
    long x_pos;
    long y_pos;
    long z_pos;
    long x_pos2;
    long y_pos2;
    long z_pos2;
    long type;
    long counter;
    long who_owns_me;
} sf_arm_collision_laser;

static int sf_arm_owner_matches(long owner, const void *item)
{
#if UINTPTR_MAX > UINT32_MAX
    return (uintptr_t)(unsigned long)owner == (uintptr_t)item;
#else
    return (sf_arm_u32)owner == sf_arm_pointer32(item);
#endif
}

static sf_arm_collision_ship *sf_arm_owner_ship(long owner)
{
#if UINTPTR_MAX > UINT32_MAX
    return (sf_arm_collision_ship *)(uintptr_t)(unsigned long)owner;
#else
    return (sf_arm_collision_ship *)sf_arm_address32((sf_arm_u32)owner);
#endif
}

static int sf_arm_in_collision_box(sf_arm_i32 left, sf_arm_i32 right, sf_arm_i32 limit)
{
    return sf_arm_abs(sf_arm_sub(left, right)) <= limit;
}

void *armcol_collisioncheck(void *collision_buffer, void *laser_list_address,
                            void *ship_list_address)
{
    linked_list *laser_list = (linked_list *)laser_list_address;
    linked_list *ship_list = (linked_list *)ship_list_address;
    sf_arm_collision_laser *laser =
        (sf_arm_collision_laser *)laser_list->start_address;
    sf_arm_collision_ship *ship =
        (sf_arm_collision_ship *)ship_list->start_address;
    void **output = (void **)collision_buffer;

    while (ship != NULL && ship->header.status == 1) {
        sf_arm_collision_ship *next_ship =
            (sf_arm_collision_ship *)ship->header.next_address;

        while (next_ship != NULL && next_ship->header.status == 1) {
            sf_arm_i32 catchment = sf_arm_add((sf_arm_i32)ship->collision_size,
                                              (sf_arm_i32)next_ship->collision_size);

            if (!sf_arm_in_collision_box((sf_arm_i32)next_ship->x_pos,
                                         (sf_arm_i32)ship->x_pos, catchment)) {
                break;
            }
            if (sf_arm_in_collision_box((sf_arm_i32)next_ship->y_pos,
                                        (sf_arm_i32)ship->y_pos, catchment) &&
                sf_arm_in_collision_box((sf_arm_i32)next_ship->z_pos,
                                        (sf_arm_i32)ship->z_pos, catchment) &&
                !sf_arm_owner_matches(next_ship->who_owns_me, ship) &&
                !sf_arm_owner_matches(ship->who_owns_me, next_ship) &&
                (sf_arm_i32)next_ship->who_owns_me !=
                    (sf_arm_i32)ship->who_owns_me) {
                next_ship->what_hit_me = (long)(intptr_t)ship;
                ship->what_hit_me = (long)(intptr_t)next_ship;
            }
            next_ship = (sf_arm_collision_ship *)next_ship->header.next_address;
        }

        while (laser != NULL && laser->header.status == 1) {
            sf_arm_i32 catchment = (sf_arm_i32)ship->collision_size;

            if (!sf_arm_in_collision_box((sf_arm_i32)laser->x_pos,
                                         (sf_arm_i32)ship->x_pos, catchment)) {
                if ((sf_arm_i32)laser->x_pos > (sf_arm_i32)ship->x_pos) {
                    break;
                }
                laser = (sf_arm_collision_laser *)laser->header.next_address;
                continue;
            }

            next_ship = ship;
            while (next_ship != NULL && next_ship->header.status == 1) {
                catchment = (sf_arm_i32)next_ship->collision_size;
                if (!sf_arm_in_collision_box((sf_arm_i32)laser->x_pos,
                                             (sf_arm_i32)next_ship->x_pos, catchment)) {
                    break;
                }
                if (sf_arm_in_collision_box((sf_arm_i32)laser->y_pos,
                                            (sf_arm_i32)next_ship->y_pos, catchment) &&
                    sf_arm_in_collision_box((sf_arm_i32)laser->z_pos,
                                            (sf_arm_i32)next_ship->z_pos, catchment)) {
                    int register_collision = (sf_arm_i32)laser->who_owns_me <= 16384;

                    if (!register_collision &&
                        !sf_arm_owner_matches(laser->who_owns_me, next_ship) &&
                        !sf_arm_owner_matches(next_ship->who_owns_me,
                                              sf_arm_owner_ship(laser->who_owns_me)) &&
                        !sf_arm_owner_matches(
                            sf_arm_owner_ship(laser->who_owns_me)->who_owns_me,
                            next_ship)) {
                        register_collision = 1;
                    }
                    if (register_collision) {
                        *output++ = laser;
                        *output++ = next_ship;
                    }
                }
                next_ship =
                    (sf_arm_collision_ship *)next_ship->header.next_address;
            }
            laser = (sf_arm_collision_laser *)laser->header.next_address;
        }
        ship = (sf_arm_collision_ship *)ship->header.next_address;
    }

    return output;
}
