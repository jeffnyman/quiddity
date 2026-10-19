/*
 * object.c: see zm/object.h. Implements Spec §12.
 *
 * Nothing here is cached. The object table's address is read from the
 * header on every call because a game may, legally if unwisely, alter
 * the header word; and an object's property table address is read from
 * its entry because Spec §12.4 lets a game change it in play.
 */

#include "zm/object.h"

#include "zm/fatal.h"
#include "zm/header.h"

/* Spec §12.2: the property defaults table has 31 words in Versions 1 to
 * 3 and 63 after. */
static unsigned default_count(const zm_memory *mem)
{
    return mem->version <= 3 ? 31u : 63u;
}

/* Spec §12.3.1, §12.3.2: entries are 9 bytes in Versions 1 to 3 and 14
 * after. */
static unsigned entry_size(const zm_memory *mem)
{
    return mem->version <= 3 ? 9u : 14u;
}

static unsigned attribute_count(const zm_memory *mem)
{
    return mem->version <= 3 ? 32u : 48u;
}

static uint32_t table_base(const zm_memory *mem)
{
    return zm_read_word(mem, ZM_HEADER_OBJECTS);
}

uint16_t zm_object_max(const zm_memory *mem)
{
    return mem->version <= 3 ? 255u : 65535u;
}

uint32_t zm_object_address(const zm_memory *mem, uint16_t object)
{
    if (object == 0 || object > zm_object_max(mem)) {
        zm_fatal("object %u does not exist", (unsigned)object);
    }

    /* Spec §12.3: the tree follows the defaults table, with objects
     * numbered from 1. Spec §1.2.1: an entry's address is a byte
     * address, which is 16 bits wide, so a number past the end of the
     * table wraps around, as it did on the interpreters the games were
     * written against. Beyond Zork depends on that: when the circlet's
     * film is used up it hands a dictionary word to a routine that
     * wants an object, and whatever the word's address lands on, 16
     * bits around, is read as that object's entry and found to have no
     * synonyms to replace. Without the wrap the read is outside memory
     * and the game stops mid-sentence. */
    return (table_base(mem) + 2u * default_count(mem) + (uint32_t)(object - 1u) * entry_size(mem)) & 0xffffu;
}

/*
 * Attributes (Spec §12.3.1): attribute 0 is bit 7 of the first byte of
 * the entry, so attribute n is bit (7 - n % 8) of byte n / 8.
 */

static uint32_t attribute_byte(const zm_memory *mem, uint16_t object, uint16_t attribute, uint8_t *mask)
{
    if (attribute >= attribute_count(mem)) {
        zm_fatal("attribute %u does not exist in Version %u", (unsigned)attribute, (unsigned)mem->version);
    }
    *mask = (uint8_t)(0x80u >> (attribute % 8u));
    return zm_object_address(mem, object) + attribute / 8u;
}

int zm_object_test_attribute(const zm_memory *mem, uint16_t object, uint16_t attribute)
{
    uint8_t mask;
    uint32_t address = attribute_byte(mem, object, attribute, &mask);

    return (zm_read_byte(mem, address) & mask) != 0;
}

void zm_object_set_attribute(zm_memory *mem, uint16_t object, uint16_t attribute)
{
    uint8_t mask;
    uint32_t address = attribute_byte(mem, object, attribute, &mask);

    zm_write_byte(mem, address, (uint8_t)(zm_read_byte(mem, address) | mask));
}

void zm_object_clear_attribute(zm_memory *mem, uint16_t object, uint16_t attribute)
{
    uint8_t mask;
    uint32_t address = attribute_byte(mem, object, attribute, &mask);

    zm_write_byte(mem, address, (uint8_t)(zm_read_byte(mem, address) & (uint8_t)~mask));
}

/*
 * The tree (Spec §12.3.1, §12.3.2). In Versions 1 to 3 the three links
 * are bytes after the 4 attribute bytes; later they are words after the
 * 6 attribute bytes. The property table address is a word in both.
 */

enum link { LINK_PARENT, LINK_SIBLING, LINK_CHILD };

static uint16_t read_link(const zm_memory *mem, uint16_t object, enum link which)
{
    uint32_t entry = zm_object_address(mem, object);

    if (mem->version <= 3) {
        return zm_read_byte(mem, entry + 4u + (uint32_t)which);
    }
    return zm_read_word(mem, entry + 6u + 2u * (uint32_t)which);
}

static void write_link(zm_memory *mem, uint16_t object, enum link which, uint16_t value)
{
    uint32_t entry = zm_object_address(mem, object);

    if (mem->version <= 3) {
        if (value > 255) {
            zm_fatal("object %u cannot be linked to object %u in Version %u",
                     (unsigned)object, (unsigned)value, (unsigned)mem->version);
        }
        zm_write_byte(mem, entry + 4u + (uint32_t)which, (uint8_t)value);
    } else {
        zm_write_word(mem, entry + 6u + 2u * (uint32_t)which, value);
    }
}

uint16_t zm_object_parent(const zm_memory *mem, uint16_t object)
{
    return read_link(mem, object, LINK_PARENT);
}

uint16_t zm_object_sibling(const zm_memory *mem, uint16_t object)
{
    return read_link(mem, object, LINK_SIBLING);
}

uint16_t zm_object_child(const zm_memory *mem, uint16_t object)
{
    return read_link(mem, object, LINK_CHILD);
}

void zm_object_set_parent(zm_memory *mem, uint16_t object, uint16_t parent)
{
    write_link(mem, object, LINK_PARENT, parent);
}

void zm_object_set_sibling(zm_memory *mem, uint16_t object, uint16_t sibling)
{
    write_link(mem, object, LINK_SIBLING, sibling);
}

void zm_object_set_child(zm_memory *mem, uint16_t object, uint16_t child)
{
    write_link(mem, object, LINK_CHILD, child);
}

void zm_object_remove(zm_memory *mem, uint16_t object)
{
    uint16_t parent = zm_object_parent(mem, object);
    uint16_t sibling = zm_object_sibling(mem, object);

    if (parent == 0) {
        return;
    }

    /* Spec §12.5 (b): the object is somewhere in its parent's child
     * list. Unlink it from whichever link points at it. The walk is
     * bounded so that an ill-founded tree with a cycle in it cannot
     * hang the machine. */
    if (zm_object_child(mem, parent) == object) {
        zm_object_set_child(mem, parent, sibling);
    } else {
        uint16_t previous = zm_object_child(mem, parent);
        uint32_t steps = 0;

        while (previous != 0 && zm_object_sibling(mem, previous) != object) {
            previous = zm_object_sibling(mem, previous);
            if (++steps > zm_object_max(mem)) {
                zm_fatal("the children of object %u form a cycle", (unsigned)parent);
            }
        }
        if (previous == 0) {
            zm_fatal("object %u claims parent %u, which does not list it as a child",
                     (unsigned)object, (unsigned)parent);
        }
        zm_object_set_sibling(mem, previous, sibling);
    }

    zm_object_set_parent(mem, object, 0);
    zm_object_set_sibling(mem, object, 0);
}

void zm_object_insert(zm_memory *mem, uint16_t object, uint16_t destination)
{
    /* Check the destination exists before changing anything. */
    (void)zm_object_address(mem, destination);

    zm_object_remove(mem, object);

    /* Spec §15 insert_obj: the object becomes the first child, and the
     * old first child becomes its sibling. */
    zm_object_set_parent(mem, object, destination);
    zm_object_set_sibling(mem, object, zm_object_child(mem, destination));
    zm_object_set_child(mem, destination, object);
}

/*
 * Properties (Spec §12.4).
 */

uint32_t zm_object_properties(const zm_memory *mem, uint16_t object)
{
    return zm_read_word(mem, zm_object_address(mem, object) + entry_size(mem) - 2u);
}

uint8_t zm_object_name_length(const zm_memory *mem, uint16_t object)
{
    return zm_read_byte(mem, zm_object_properties(mem, object));
}

uint32_t zm_object_name(const zm_memory *mem, uint16_t object)
{
    return zm_object_properties(mem, object) + 1u;
}

uint32_t zm_object_first_property(const zm_memory *mem, uint16_t object)
{
    uint32_t table = zm_object_properties(mem, object);

    return table + 1u + 2u * zm_read_byte(mem, table);
}

uint32_t zm_property_decode(const zm_memory *mem, uint32_t address, zm_property *out)
{
    uint8_t size = zm_read_byte(mem, address);

    if (size == 0) {
        /* The end of the list, in every Version. */
        out->number = 0;
        out->length = 0;
        out->address = address + 1u;
        out->next = address + 1u;
        return out->next;
    }

    if (mem->version <= 3) {
        /* Spec §12.4.1: 32 times (length - 1) plus the number. */
        out->number = size & 31u;
        out->length = (uint8_t)(size / 32u + 1u);
        out->address = address + 1u;
    } else if (size & 0x80u) {
        /* Spec §12.4.2.1: two size bytes; the length is in the bottom
         * six bits of the second, with 0 meaning 64 (Spec §12.4.2.1.1). */
        uint8_t second = zm_read_byte(mem, address + 1u);

        out->number = size & 63u;
        out->length = (uint8_t)(second & 63u);
        if (out->length == 0) {
            out->length = 64;
        }
        out->address = address + 2u;
    } else {
        /* Spec §12.4.2.2: one size byte; bit 6 chooses a length of 1 or
         * 2. */
        out->number = size & 63u;
        out->length = (uint8_t)((size & 0x40u) ? 2u : 1u);
        out->address = address + 1u;
    }

    out->next = out->address + out->length;
    return out->next;
}

int zm_object_find_property(const zm_memory *mem, uint16_t object, uint16_t number, zm_property *out)
{
    uint32_t address = zm_object_first_property(mem, object);

    for (;;) {
        zm_property_decode(mem, address, out);
        if (out->number == number) {
            return 1;
        }
        /* Spec §12.4: descending order, so once the numbers drop below
         * the one wanted it is not there. The terminator's 0 ends every
         * search this way. */
        if (out->number < number) {
            return 0;
        }
        address = out->next;
    }
}

uint16_t zm_property_default(const zm_memory *mem, uint16_t number)
{
    if (number == 0 || number > default_count(mem)) {
        zm_fatal("property %u does not exist in Version %u", (unsigned)number, (unsigned)mem->version);
    }
    return zm_read_word(mem, table_base(mem) + 2u * (number - 1u));
}

uint8_t zm_property_length_at(const zm_memory *mem, uint32_t address)
{
    uint8_t size;

    if (address == 0) {
        return 0;
    }

    /* The byte before the data is the size byte in Versions 1 to 3 and
     * the only or second size byte after. Spec §12.4.2 Remarks: the
     * second byte has its top bit set precisely so it can be told from a
     * lone one. */
    size = zm_read_byte(mem, address - 1u);
    if (mem->version <= 3) {
        return (uint8_t)(size / 32u + 1u);
    }
    if (size & 0x80u) {
        return (uint8_t)((size & 63u) ? (size & 63u) : 64u);
    }
    return (uint8_t)((size & 0x40u) ? 2u : 1u);
}
