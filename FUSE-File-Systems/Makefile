# This code is provided solely for the personal and private use of students
# taking the CSC369H course at the University of Toronto. Copying for purposes
# other than this use is expressly prohibited. All forms of distribution of
# this code, including but not limited to public repositories on GitHub,
# GitLab, Bitbucket, or any other online platform, whether as given or with
# any changes, are expressly prohibited.
#
# Authors: Alexey Khrabrov, Karen Reid, Angela Demke Brown
#
# All of the files in this directory and all subdirectories are:
# Copyright (c) 2022 Angela Demke Brown

CC = gcc
CFLAGS  := $(shell pkg-config fuse --cflags) -g3 -Wall -Wextra -Werror $(CFLAGS)
LDFLAGS := $(shell pkg-config fuse --libs) $(LDFLAGS)

.PHONY: all clean

all: vsfs mkfs.vsfs

vsfs: vsfs.o fs_ctx.o options.o bitmap.o map.o
	$(CC) $^ -o $@ $(LDFLAGS)

mkfs.vsfs: mkfs.o bitmap.o map.o
	$(CC) $^ -o $@ $(LDFLAGS)

SRC_FILES = $(wildcard *.c)
OBJ_FILES = $(SRC_FILES:.c=.o)

-include $(OBJ_FILES:.o=.d)

%.o: %.c
	$(CC) $< -o $@ -c -MMD $(CFLAGS)

clean:
	rm -f $(OBJ_FILES) $(OBJ_FILES:.o=.d) vsfs mkfs.vsfs

realclean:
	rm -f $(OBJ_FILES) $(OBJ_FILES:.o=.d) vsfs mkfs.vsfs *~
