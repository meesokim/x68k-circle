#ifndef HANGUL16_H
#define HANGUL16_H

#include <stdint.h>

// 16x16 MSB-left bitmap, 32 bytes. NULL when cp is not Hangul.
const uint8_t *HangulGlyph16(uint32_t cp);

// CP949 lead/trail to Unicode. 0 when the pair is not a character.
uint32_t DecodeCp949Pair(unsigned lead, unsigned trail);

#endif
