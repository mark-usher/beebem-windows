/****************************************************************
BeebEm - BBC Micro and Master 128 Emulator

Simple file parser, from Pi1MHz (rpi/fileparser.h)
https://github.com/dp111/Pi1MHz

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 3
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
****************************************************************/

#ifndef FILEPARSER_H
#define FILEPARSER_H

#include <stddef.h>
#include <stdint.h>

enum parserkeyvaluetype {
    NUMSTRING,
    STRING,
    INTEGER
};

typedef struct parserkey {
    const char * key;
    int min;
    int max;
    enum parserkeyvaluetype type;
} parserkey;

typedef struct {
    size_t length;
    union {
        int * integer;
        char * string;
    } v;
} parserkeyvalue;

int parse_findindex( const char * searchkey, const parserkey array[]);
int parse_readfile( const char * filename , const char * outfile, const parserkey keyv[], parserkeyvalue values[]);

/* Frees every non-NULL v.string in the array - which is every value the
   parser allocated on a read.  Do NOT call it on an array whose values the
   caller filled in for a rewrite: those pointers are the caller's, and
   freeing a string literal or a stack buffer is what it sounds like. */
void parse_releasekeyvalues( parserkeyvalue values[], int numberofkeys );

#endif
