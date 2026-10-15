// A game's name from its NACP (the 0x4000 bytes of control data the console hands over).
//
// Games from firmware 21 on keep their names DEFLATE-compressed (TitlesDataFormat == 1 at 0x3215); a toolkit that doesn't know
// that reads the compressed bytes as text and shows rubbish. This understands both layouts.
#ifndef NACP_NAME_H
#define NACP_NAME_H

#include <stddef.h>
#include <stdint.h>

#define NACP_SIZE 0x4000

// The name in American English, else British English, else the first language that has one. 0 = found, -1 = none.
int nacp_name(const uint8_t *nacp, char *out, size_t cap);
// 1 if the names in this NACP are the compressed kind
int nacp_names_compressed(const uint8_t *nacp);

#endif
