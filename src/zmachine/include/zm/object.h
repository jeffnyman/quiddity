/*
 * zm/object.h: the object table.
 *
 * Implements Spec §12. The objects of a game (rooms, things, the player)
 * live in a table in dynamic memory whose address the header gives at
 * $0a. Each object has attributes (flags numbered from 0), a place in a
 * tree (parent, sibling, child), a short name, and properties (small
 * variables numbered from 1, each with 1 to 64 bytes of data). This file
 * knows the layout of all that and nothing else; the opcodes that use it
 * are in machine.c.
 *
 * The layout differs between Versions 1 to 3 and 4 and later in every
 * part (Spec §12.3.1, §12.3.2, §12.4.1, §12.4.2), so most functions here
 * are small and branch on the Version once.
 *
 * Object 0 means "nothing" (Spec §12.3) and has no entry, so every
 * function that takes an object number faults on 0. What the machine
 * does when a game passes 0 is its decision, made in machine.c.
 */

#ifndef ZM_OBJECT_H
#define ZM_OBJECT_H

#include <stdint.h>

#include "zm/memory.h"

/* One property of an object, as found by the functions below. */
typedef struct zm_property {
    /* 1 to 31 in Versions 1 to 3, 1 to 63 after; 0 marks the end of an
     * object's list. */
    uint8_t number;

    /* Data bytes, 1 to 8 in Versions 1 to 3 and 1 to 64 after. */
    uint8_t length;

    /* Byte address of the first data byte. */
    uint32_t address;

    /* Byte address of the size byte that follows this property's data,
     * which starts the next property or ends the list. */
    uint32_t next;
} zm_property;

/* The largest object number the Version allows: 255 in Versions 1 to 3,
 * 65535 after (Spec §12.3.1, §12.3.2). The table itself does not record
 * how many objects it holds. */
uint16_t zm_object_max(const zm_memory *mem);

/* Byte address of an object's entry in the tree (Spec §12.3). Faults if
 * the number is 0 or larger than the Version allows. */
uint32_t zm_object_address(const zm_memory *mem, uint16_t object);

/* Attributes, numbered 0 to 31 in Versions 1 to 3 and 0 to 47 after,
 * stored top bit first (Spec §12.3.1). An attribute number out of range
 * is a fault. */
int zm_object_test_attribute(const zm_memory *mem, uint16_t object, uint16_t attribute);
void zm_object_set_attribute(zm_memory *mem, uint16_t object, uint16_t attribute);
void zm_object_clear_attribute(zm_memory *mem, uint16_t object, uint16_t attribute);

/* The tree links. 0 means none. */
uint16_t zm_object_parent(const zm_memory *mem, uint16_t object);
uint16_t zm_object_sibling(const zm_memory *mem, uint16_t object);
uint16_t zm_object_child(const zm_memory *mem, uint16_t object);
void zm_object_set_parent(zm_memory *mem, uint16_t object, uint16_t parent);
void zm_object_set_sibling(zm_memory *mem, uint16_t object, uint16_t sibling);
void zm_object_set_child(zm_memory *mem, uint16_t object, uint16_t child);

/* Detach an object from its parent and siblings; its own children stay
 * with it (Spec §15 remove_obj). An object with no parent is left as it
 * is. */
void zm_object_remove(zm_memory *mem, uint16_t object);

/* Move an object to be the first child of a destination (Spec §15
 * insert_obj), detaching it from wherever it was first. */
void zm_object_insert(zm_memory *mem, uint16_t object, uint16_t destination);

/* Byte address of an object's property table (Spec §12.4), which begins
 * with the short name. */
uint32_t zm_object_properties(const zm_memory *mem, uint16_t object);

/* Byte address of an object's short name, a Z-string for zm_text_decode()
 * (Spec §12.4). An object with an empty name has a text-length of 0 and
 * the address returned then points at its first property; callers should
 * check zm_object_name_length() first. */
uint32_t zm_object_name(const zm_memory *mem, uint16_t object);

/* Length of the short name in 2-byte words (Spec §12.4). */
uint8_t zm_object_name_length(const zm_memory *mem, uint16_t object);

/* Byte address of the first size byte in an object's property list, just
 * after the short name. */
uint32_t zm_object_first_property(const zm_memory *mem, uint16_t object);

/* Decode the property whose size byte is at the given address (Spec
 * §12.4.1, §12.4.2) and return the address of the next one. At the end
 * of a list the property's number is 0 and nothing else in it is
 * meaningful. */
uint32_t zm_property_decode(const zm_memory *mem, uint32_t address, zm_property *out);

/* Look up a property of an object. Returns 1 and fills in the property
 * if the object provides it, else 0. The list is in descending order
 * (Spec §12.4), so the search stops early. */
int zm_object_find_property(const zm_memory *mem, uint16_t object, uint16_t number, zm_property *out);

/* The default value of a property, from the table at the start of the
 * object table (Spec §12.2). Faults on 0 or a number past the table. */
uint16_t zm_property_default(const zm_memory *mem, uint16_t number);

/* The length of the property whose data begins at the given address,
 * worked out from the size byte(s) just before it (Spec §12.4.2
 * Remarks). This is what get_prop_len needs, and it has no way to check
 * that the address really is a property's. Address 0 gives 0 (Spec §15
 * get_prop_len). */
uint8_t zm_property_length_at(const zm_memory *mem, uint32_t address);

#endif /* ZM_OBJECT_H */
