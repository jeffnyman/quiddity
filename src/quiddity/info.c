/*
 * info.c: see info.h.
 *
 * This is presentation, not machine: it reads the decoded header and
 * describes it. The field meanings and Version rules it relies on are all
 * in Spec §11.1; the flag names come from the tables in §11.1.2 and
 * §11.1.4.
 */

#include "info.h"

#include "zm/header.h"

static void print_flags1(FILE *out, const zm_header *h)
{
    fprintf(out, "Flags 1:          $%02x", h->flags1);

    if (h->version <= 3) {
        /* Spec §11.1.4, Versions 1 to 3. Bits 1 and 2 are set by the game;
         * the rest describe the interpreter and are blank in a file on
         * disk, so they are only worth listing when set. */
        fprintf(out, "  status line: %s",
                (h->flags1 & ZM_FLAGS1_V3_STATUS_TIME) ? "hours:mins" : "score/turns");
        if (h->flags1 & ZM_FLAGS1_V3_SPLIT_FILE)       fputs(", split across two discs", out);
        if (h->flags1 & ZM_FLAGS1_V3_TANDY)            fputs(", Tandy bit", out);
        if (h->flags1 & ZM_FLAGS1_V3_NO_STATUS_LINE)   fputs(", no status line", out);
        if (h->flags1 & ZM_FLAGS1_V3_SCREEN_SPLITTING) fputs(", screen splitting", out);
        if (h->flags1 & ZM_FLAGS1_V3_VARIABLE_PITCH)   fputs(", variable-pitch default", out);
    } else {
        /* Spec §11.1.4, Version 4 and later. All of these are the
         * interpreter's to set, so a file on disk normally has none. The
         * exception is bit 1: it means "pictures available" only in
         * Version 6. In other Versions the §11 Remarks note that Inform
         * games set it to ask for an hours:mins status line, as in
         * Version 3, and interpreters should otherwise ignore it. */
        const char *sep = "  ";
        if (h->flags1 & ZM_FLAGS1_COLOURS)     { fprintf(out, "%scolours", sep);     sep = ", "; }
        if (h->flags1 & ZM_FLAGS1_PICTURES) {
            fprintf(out, "%s%s", sep, h->version == 6 ? "pictures" : "status line: hours:mins (Inform convention)");
            sep = ", ";
        }
        if (h->flags1 & ZM_FLAGS1_BOLD)        { fprintf(out, "%sbold", sep);        sep = ", "; }
        if (h->flags1 & ZM_FLAGS1_ITALIC)      { fprintf(out, "%sitalic", sep);      sep = ", "; }
        if (h->flags1 & ZM_FLAGS1_FIXED_PITCH) { fprintf(out, "%sfixed-pitch", sep); sep = ", "; }
        if (h->flags1 & ZM_FLAGS1_SOUND)       { fprintf(out, "%ssound", sep);       sep = ", "; }
        if (h->flags1 & ZM_FLAGS1_TIMED_INPUT) { fprintf(out, "%stimed input", sep); }
    }

    fputc('\n', out);
}

static void print_flags2(FILE *out, const zm_header *h)
{
    const char *sep = "  ";

    fprintf(out, "Flags 2:          $%04x", h->flags2);

    /* Spec §11.1.2. Most of these are the game saying what it would like
     * to use; the interpreter clears the ones it cannot provide. */
    if (h->flags2 & ZM_FLAGS2_TRANSCRIPT)  { fprintf(out, "%stranscript on", sep); sep = ", "; }
    if (h->flags2 & ZM_FLAGS2_FORCE_FIXED) { fprintf(out, "%sforce fixed-pitch", sep); sep = ", "; }
    if (h->flags2 & ZM_FLAGS2_REDRAW)      { fprintf(out, "%sredraw requested", sep); sep = ", "; }
    if (h->flags2 & ZM_FLAGS2_PICTURES)    { fprintf(out, "%swants pictures", sep); sep = ", "; }
    if (h->flags2 & ZM_FLAGS2_UNDO)        { fprintf(out, "%swants undo", sep); sep = ", "; }
    if (h->flags2 & ZM_FLAGS2_MOUSE)       { fprintf(out, "%swants mouse", sep); sep = ", "; }
    if (h->flags2 & ZM_FLAGS2_COLOURS)     { fprintf(out, "%swants colours", sep); sep = ", "; }
    if (h->flags2 & ZM_FLAGS2_SOUND)       { fprintf(out, "%swants sound", sep); sep = ", "; }
    if (h->flags2 & ZM_FLAGS2_MENUS)       { fprintf(out, "%swants menus", sep); }

    fputc('\n', out);
}

void print_story_info(FILE *out, const char *path, const zm_memory *mem)
{
    zm_header h;
    uint16_t computed;

    zm_header_read(mem, &h);

    fprintf(out, "File:             %s\n", path);
    fprintf(out, "Version:          %u\n", h.version);
    fprintf(out, "Release:          %u\n", h.release);
    fprintf(out, "Serial:           %s\n", h.serial);

    /* Spec §11.1: early Version 3 files carry neither length nor
     * checksum, shown here as absent rather than as zero. */
    if (h.file_length == 0) {
        fprintf(out, "Length:           not recorded (file is %u bytes)\n", mem->size);
        fprintf(out, "Checksum:         not recorded\n");
    } else {
        fprintf(out, "Length:           %u bytes", h.file_length);
        if (h.file_length != mem->size) {
            fprintf(out, " (file is %u bytes)", mem->size);
        }
        fputc('\n', out);

        computed = zm_header_compute_checksum(mem);
        fprintf(out, "Checksum:         $%04x", h.checksum);
        if (computed == h.checksum) {
            fputs(" (verified)\n", out);
        } else {
            fprintf(out, " (computed $%04x: MISMATCH)\n", computed);
        }
    }

    if (h.inform_version[0] >= '0' && h.inform_version[0] <= '9') {
        fprintf(out, "Compiler:         Inform %s\n", h.inform_version);
    }

    if (h.standard_major != 0 || h.standard_minor != 0) {
        fprintf(out, "Standard:         %u.%u\n", h.standard_major, h.standard_minor);
    }

    fputc('\n', out);

    /* Spec §1.1: the three regions. Static memory ends at the lower of the
     * file end and $ffff; high memory may overlap it. */
    fprintf(out, "Dynamic memory:   $00000 - $%05x  (%u bytes)\n",
            mem->static_base - 1, mem->static_base);
    fprintf(out, "Static memory:    $%05x - $%05x\n",
            mem->static_base, (mem->size < 0x10000u ? mem->size : 0x10000u) - 1);
    fprintf(out, "High memory:      $%05x - $%05x\n", mem->high_base, mem->size - 1);

    fputc('\n', out);

    if (h.version == 6) {
        /* Spec §11.1, §5.5: Version 6 starts by calling a routine. */
        fprintf(out, "Main routine:     packed $%04x (byte address $%05x)\n",
                h.initial_pc, zm_unpack_routine(mem, h.initial_pc));
    } else {
        fprintf(out, "Initial PC:       $%05x\n", h.initial_pc);
    }
    fprintf(out, "Dictionary:       $%05x\n", h.dictionary);
    fprintf(out, "Object table:     $%05x\n", h.objects);
    fprintf(out, "Global variables: $%05x\n", h.globals);
    fprintf(out, "Abbreviations:    $%05x\n", h.abbreviations);

    if (h.version >= 5) {
        fprintf(out, "Alphabet table:   %s\n",
                h.alphabet_table ? "custom" : "default");
        if (h.alphabet_table) {
            fprintf(out, "                  $%05x\n", h.alphabet_table);
        }
        fprintf(out, "Terminating chars: %s\n", h.terminating_chars ? "custom" : "none");
        fprintf(out, "Header extension: %s\n", h.extension_table ? "present" : "absent");
        if (h.extension_table) {
            fprintf(out, "                  $%05x, %u word(s), Unicode table %s\n",
                    h.extension_table,
                    zm_header_extension_word(mem, ZM_HEADER_EXT_COUNT),
                    zm_header_extension_word(mem, ZM_HEADER_EXT_UNICODE_TABLE) ? "present" : "absent");
        }
    }

    if (h.version == 6 || h.version == 7) {
        fprintf(out, "Routines offset:  $%04x (x8 = $%05x)\n",
                h.routines_offset, 8u * h.routines_offset);
        fprintf(out, "Strings offset:   $%04x (x8 = $%05x)\n",
                h.strings_offset, 8u * h.strings_offset);
    }

    fputc('\n', out);

    print_flags1(out, &h);
    print_flags2(out, &h);
}
