/*
 * Copyright 2010 The Android Open Source Project
 */

#ifndef _MINELF_RETOUCH
#define _MINELF_RETOUCH

#include <stdbool.h>
#include <sys/types.h>

bool retouch_one_library(const char *lib_name,
                         const char *lib_retouch_name,
                         int32_t offset);

#endif
