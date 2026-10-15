// The console's own profile pictures, read from its files, so none of Nintendo's artwork needs to be in this repository (docs/profile-pictures.md).
// The console keeps them as system data 010000000000080A ("avatar"): chara/<id as 8 hex digits>.szs is one picture, a Yaz0-packed 256 x 256 image
// of RGBA bytes (straight alpha, top row first), and chara/table.bin is the ids as 32-bit little-endian numbers in the order the console shows
// them. Id 1 is picture 0: the account's icon id is the picture's number plus one. Pure C (stdio and malloc), so the host tests run it on a folder.
#ifndef AVATAR_SRC_H
#define AVATAR_SRC_H

#include <stddef.h>
#include <stdint.h>

#define AVSRC_SIZE 256 // pictures are AVSRC_SIZE x AVSRC_SIZE pixels

typedef struct AvatarSrc AvatarSrc;

// Opens the pictures under `dir` ("avatar:/" on the console, a folder on a PC) by reading chara/table.bin. NULL if it isn't there or names no picture.
AvatarSrc *avsrc_open(const char *dir);
void avsrc_close(AvatarSrc *s);
// The number of pictures: the highest id the table names (picture numbers 0 .. count-1).
int avsrc_count(const AvatarSrc *s);
// The picture number shown at grid position `pos` (the console's order; an id the table leaves out follows, in id order); -1 past the end.
int avsrc_at(const AvatarSrc *s, int pos);
// One picture as AVSRC_SIZE x AVSRC_SIZE RGBA bytes (malloc'd; free() it). NULL when that file is missing or damaged.
uint8_t *avsrc_load(const AvatarSrc *s, int pic);

// Yaz0, the console's packing: unpacks exactly `dst_len` bytes. 0 on success; -1 if the data isn't Yaz0, says another size, or runs out or
// points outside what it has made so far (nothing is read or written beyond src[0..src_len) and dst[0..dst_len)).
int yaz0_unpack(const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_len);

#endif
