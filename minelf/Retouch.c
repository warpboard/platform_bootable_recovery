/*
 * Copyright 2010 The Android Open Source Project
 */

#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "Retouch.h"

typedef struct {
    int32_t mmap_addr;
    char tag[4]; /* 'P', 'R', 'E', ' ' */
} prelink_info_t __attribute__((packed));

static bool check_prelinked(int fd) {
    if (sizeof(prelink_info_t) != 8) return false;
    off_t end = lseek(fd, 0, SEEK_END);
    int nr = sizeof(prelink_info_t);
    off_t sz = lseek(fd, -nr, SEEK_CUR);
    if ((long)(end - sz) != (long)nr || sz == (off_t)-1) return false;

    prelink_info_t info;
    int num_read = read(fd, &info, nr);
    if (num_read < 0 ||
        num_read != sizeof(info) ||
        strncmp(info.tag, "PRE ", 4)) return false;
    return true;
}

static bool set_prelink_info(int fd, int value) {
    int nr = sizeof(prelink_info_t);
    if (nr != 8) return false;

    off_t end = lseek(fd, 0, SEEK_END);
    off_t sz = lseek(fd, -nr, SEEK_CUR);
    if ((long)(end - sz) != (long)nr || sz == (off_t)-1) return false;

    prelink_info_t info;
    int num_read = read(fd, &info, nr);
    if (num_read < 0 || num_read != sizeof(info)) return false;

    info.mmap_addr = value;
    strncpy(info.tag, "PRE ", 4);

    end = lseek(fd, 0, SEEK_END);
    sz = lseek(fd, -nr, SEEK_CUR);
    if ((long)(end - sz) != (long)nr || sz == (off_t)-1) return false;

    int num_written = write(fd, &info, sizeof(info));
    if (num_written < 0 || sizeof(info) != num_written) return false;
    return true;
}

// Note we are working with 32-bit numbers explicitly. This should
// change at some point.
static bool set_relocation(int fd, int64_t offset, uint32_t value) {
    if (lseek(fd, offset, SEEK_SET) != offset ||
        write(fd, &value, 4) != 4) return false;
    return true;
}

#define false 0
#define true 1

static int32_t offs_prev;
static uint32_t cont_prev;

static void init_compression_state(void) {
    offs_prev = 0;
    cont_prev = 0;
}

// For details on the encoding used for relocation lists, please
// refer to build/tools/retouch/retouch-prepare.c. The intent is to
// save space by removing most of the inherent redundancy.

static bool decode(FILE *f_in, int32_t *offset, uint32_t *contents) {
    int one_char, input_size, charIx;
    uint8_t input[8];

    one_char = fgetc(f_in);
    if (one_char == EOF) return false;
    input[0] = (uint8_t)one_char;
    if (input[0] & 0x80)
        input_size = 2;
    else if (input[0] & 0x40)
        input_size = 3;
    else
        input_size = 8;

    // we already read one byte..
    charIx = 1;
    while (charIx < input_size) {
        one_char = fgetc(f_in);
        if (one_char == EOF) return false;
        input[charIx++] = (uint8_t)one_char;
    }

    if (input_size == 2) {
        *offset = offs_prev + (((input[0]&0x60)>>5)+1)*4;

        // if the original was negative, we need to 1-pad before applying delta
        int32_t tmp = (((input[0] & 0x0000001f) << 8) | input[1]);
        if (tmp & 0x1000) tmp = 0xffffe000 | tmp;
        *contents = cont_prev + tmp;
    } else if (input_size == 3) {
        *offset = offs_prev + (((input[0]&0x30)>>4)+1)*4;

        // if the original was negative, we need to 1-pad before applying delta
        int32_t tmp = (((input[0] & 0x0000000f) << 16) |
                       (input[1] << 8) |
                       input[2]);
        if (tmp & 0x80000) tmp = 0xfff00000 | tmp;
        *contents = cont_prev + tmp;
    } else {
        *offset =
          (input[0]<<24) |
          (input[1]<<16) |
          (input[2]<<8) |
          input[3];
        if (*offset == 0x3fffffff) *offset = -1;
        *contents =
          (input[4]<<24) |
          (input[5]<<16) |
          (input[6]<<8) |
          input[7];
    }

    offs_prev = *offset;
    cont_prev = *contents;

    return true;
}

bool retouch_one_library(const char *lib_name,
                         const char *lib_retouch_name,
                         int32_t offset) {
    FILE *file_retouch = NULL;
    int fd_elf_rw = -1;
    int32_t retouch_offset;
    uint32_t retouch_original_value;
    bool success = true;

    // open the library
    if ((fd_elf_rw = open(lib_name, O_RDWR, 0) < 0)) {
        success = false;
        goto out;
    }

    // Sometimes there is a glitch, where we need to reopen the file for
    // the contents to really show. Don't know why this happens, but our
    // fix for it is to close and reopen the file, and check_prelinked again.
    if (!check_prelinked(fd_elf_rw)) {
        close(fd_elf_rw);
        fd_elf_rw = open(lib_name, O_RDWR, 0);
    }
    if (!check_prelinked(fd_elf_rw)) {
        // nothing to do
        goto out;
    }

     // open the retouch list (associated with this library)
    if ((file_retouch = fopen(lib_retouch_name, "rb")) == NULL) {
        success = false;
        goto out;
    }

    // loop over all retouch entries
    init_compression_state();
    while (!feof(file_retouch)) {
        // read one retouch entry
        if (!decode(file_retouch, &retouch_offset, &retouch_original_value)) {
            if (!feof(file_retouch)) success = false;
            break;
        }

        if (retouch_offset == -1) {
            success = success &&
                set_prelink_info(fd_elf_rw,
                                 retouch_original_value + offset);
        } else {
            success = success &&
                set_relocation(fd_elf_rw,
                               retouch_offset,
                               retouch_original_value + offset);
        }
    }

  out:
    // clean up
    if (fd_elf_rw >= 0) { close(fd_elf_rw); fd_elf_rw = -1; }
    if (file_retouch) { fclose(file_retouch); file_retouch = NULL; }

    return success;
}
