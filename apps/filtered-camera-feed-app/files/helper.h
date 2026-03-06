#ifndef _HELPER_H
#define _HELPER_H 1

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <stdint.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdbool.h>
#include <ctype.h>
#include <signal.h>

#define FRAME_SIZE_IN_BYTES 2048
#define BYTES_PER_RECEIVE_TRANSMISSION FRAME_SIZE_IN_BYTES * 2
#define LINES_PER_CHUNK 128
#define BYTES_PER_LINE 8
#define HEXCHARS_PER_LINE 16
#define CHUNK_BYTES (LINES_PER_CHUNK * BYTES_PER_LINE)

int hexchar_to_uint8(char c, uint8_t *out);
int parseLine(const char s[HEXCHARS_PER_LINE], uint8_t out[BYTES_PER_LINE]);
int readNextLine(FILE *f, char hex16[HEXCHARS_PER_LINE], uint64_t *lineno);
void sleep_ms(int milliseconds);
#endif