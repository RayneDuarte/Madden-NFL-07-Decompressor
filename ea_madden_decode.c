/*
 * EA Madden decompressor -- clean C reconstruction
 *
 * Reconstructed from the x86 EA_Madden_dump[] supplied by the user.
 *
 * Important details reproduced from the dump:
 *   - each compressed block starts with a 1-bit block mode;
 *   - mode 0 contains 285 + 30 four-bit Huffman code lengths;
 *   - the code-generation table has the dump's unusual one-entry shift:
 *       table[L+1] = (table[L] + count[L]) << 1
 *     and count[0] is explicitly cleared after reading the lengths;
 *   - Huffman codes are inserted into a binary tree MSB-first;
 *   - symbol 0x100 is NOT a literal and NOT EOF: it is the end of the
 *     current compressed block, after which the next block header follows;
 *   - 0x00..0x0FF are literal bytes;
 *   - 0x101..0x11C are LZ length symbols;
 *   - the dictionary is a 0x8000-byte circular window.
 *
 * The implementation below intentionally avoids the executable dump,
 * PATCHIT(), absolute addresses and the original state-machine wrapper.
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define EA_MADDEN_WINDOW       0x8000u
#define EA_MADDEN_MAIN_SYMBOLS 0x11Du
#define EA_MADDEN_DIST_SYMBOLS  0x1Eu
#define EA_MADDEN_MAX_CODE_BITS 15u
#define HUFF_NODE_SIZE 3

/* ------------------------------------------------------------------------- */
/* Bit reader: logical equivalent of 0x757D20/0x757B00 for the compressed
 * bitstream. Bits are consumed MSB-first. */

//some dedicated macros to extract bits

#define GETBIT(qs, bytepos, bitpos, bit)\
{\
	bit = (qs[bytepos] >> (7 - bitpos)) & 1;\
	bitpos++;\
	if (bitpos == 8u)\
	{\
		bitpos = 0;\
		++bytepos;\
	}\
}

#define GETFOURBITS(qs, bytepos, bitpos, bit)\
do{\
    if (bitpos <= 4)\
	{\
        bit = (qs[bytepos] >> (4 - bitpos)) & 0x0F;\
        bitpos += 4;\
        if (bitpos == 8)\
		{\
            bitpos = 0;\
            ++bytepos;\
        }\
    }\
	else\
	{\
		uint32_t a = (uint32_t)qs[bytepos] << 8;\
		uint32_t b = a | qs[bytepos + 1];\
		uint32_t c = b >> (12 - bitpos);\
		bit = c & 0x0F;\
        bitpos -= 4;\
        ++bytepos;\
    }\
} while (0)

#define GET_BITS(qs, bytepos, bitpos, n, value)\
do{\
    uint32_t v = 0;\
	uint32_t nbits = n;\
    while (nbits != 0)\
	{\
        unsigned available;\
        unsigned take;\
        unsigned shift;\
        uint32_t mask;\
        available = 8u - bitpos;\
        take = nbits < available ? nbits : available;\
        shift = available - take;\
        mask = (1u << take) - 1u;\
		v = v << take;\
		v |= ((uint32_t)(qs[bytepos] >> shift) & mask);\
        bitpos += take;\
        if (bitpos == 8u)\
		{\
            bitpos = 0;\
            ++bytepos;\
        }\
        nbits -= take;\
    }\
    value = v;\
}while(0)

#define EA_READ_LENGTHS(qs, bytepos, bitpos, qd, count_)\
{\
	unsigned i;\
	for (i = 0; i < count_; ++i)\
	{\
		uint32_t v;\
		GETFOURBITS(qs, bytepos, bitpos, v);\
		qd[i] = (uint8_t)v;\
	}\
}

#define EA_HUFF_DECODE(qs, bytepos, bitpos, Hnode, h_node_count, h_max_bits, Symbol) \
do{ \
    unsigned depth; \
    int node = 0; \
    for (depth = 0; depth < h_max_bits; ++depth) \
    { \
        uint32_t bit; \
        int next; \
        GETBIT(qs, bytepos, bitpos, bit); \
        next = Hnode[node * 3 + (bit & 1u)]; \
        node = next; \
        if (Hnode[node * 3 + 2] >= 0) \
        { \
            Symbol = (uint16_t)Hnode[node * 3 + 2]; \
            break;\
        } \
    } \
} while (0)

#define EA_HUFF_BUILD(h_length, h_code, Hnode, h_node_count, h_max_bits, lengths, symbol_count)\
do{\
    unsigned count[EA_MADDEN_MAX_CODE_BITS + 1];\
    uint16_t table[EA_MADDEN_MAX_CODE_BITS + 1];\
    uint16_t next[EA_MADDEN_MAX_CODE_BITS + 1];\
    uint32_t code = 0;\
    unsigned max_bits = 0;\
    unsigned i;\
    memset(count, 0, sizeof(count));\
    memset(table, 0, sizeof(table));\
    memset(next, 0, sizeof(next));\
   for (i = 0; i < symbol_count; ++i)\
   {\
   		h_length[i] = lengths[i];\
   		if (lengths[i] != 0)\
   		{\
			uint8_t v = lengths[i];\
			++count[v];\
		}\
        if (lengths[i] > max_bits)\
        {\
            max_bits = lengths[i];\
    	}\
    }\
    count[0] = 0;\
    for (i = 0; i < EA_MADDEN_MAX_CODE_BITS; ++i)\
	{\
        code = (code + count[i]) << 1;\
        table[i + 1] = (uint16_t)code;\
    }\
    memcpy(next, table, sizeof(next));\
    for (i = 0; i < symbol_count; ++i)\
	{\
        unsigned len = h_length[i];\
        if (len != 0)\
		{\
            h_code[i] = next[len];\
            next[len] = (uint16_t)(next[len] + 1u);\
        }\
    }\
	h_node_count = 1;\
	Hnode[0] = -1;\
	Hnode[1] = -1;\
	Hnode[2] = -1;\
    h_max_bits = max_bits;\
   for (i = 0; i < symbol_count; ++i)\
   {\
        unsigned len = h_length[i];\
        unsigned bit_index;\
        int current_node;\
        if (len == 0)\
		{\
			continue;\
		}\
        current_node = 0;\
        for (bit_index = len; bit_index != 0; --bit_index)\
		{\
            unsigned bit = (h_code[i] >> (bit_index - 1u)) & 1u;\
            int next_node = Hnode[current_node * 3 + bit];\
            if (next_node < 0)\
			{\
                next_node = (int)h_node_count++;\
                Hnode[next_node * 3] = -1;\
                Hnode[next_node * 3 + 1] = -1;\
                Hnode[next_node * 3 + 2] = -1;\
                Hnode[current_node * 3 + bit] = next_node;\
            }\
            current_node = next_node;\
        }\
		Hnode[current_node * 3 + 2] = (int)i;\
    }\
}while(0)

#define EA_DECODE_LENGTH(qs, bytepos, bitpos, code, length)\
do{\
	unsigned extra_bits;\
	uint32_t extra = 0;\
    if (code < 8u)\
	{\
        length = code + 3u;\
        break;\
    }\
    extra_bits = ea_length_extra[code];\
	GET_BITS(qs, bytepos, bitpos, extra_bits, extra);\
    if (extra_bits == 0)\
	{\
       length = 0xE3u;\
    }\
	else\
	{\
        length = (unsigned)ea_length_base[code] + (unsigned)extra;\
    }\
}while(0)

#define EA_DECODE_DISTANCE(qs, bytepos, bitpos, code, distance)\
do{\
    unsigned extra_bits;\
    uint32_t extra = 0;\
    if (code < 4u)\
	{\
        distance = code + 1u;\
        break;\
    }\
    extra_bits = ea_distance_extra[code];\
	GET_BITS(qs, bytepos, bitpos, extra_bits, extra);\
    distance = (unsigned)ea_distance_base[code] + (unsigned)extra;\
}while(0)

/* ------------------------------------------------------------------------- */
/* Huffman tree.  The original routines at 0x758130/0x758220 construct a
 * binary tree from the generated codes.  A fixed array is enough: a binary
 * tree containing N leaves needs at most 2*N-1 nodes. */

/*
static void ea_huff_reset(struct EA_Huff *h, unsigned symbol_count)
{
    memset(h, 0, sizeof(*h));
    h->symbol_count = symbol_count;
}
*/

/*
 * This is the exact arithmetic performed by 0x7580A0.
 *
 * 0x758040 reads all code lengths and increments count[length].  It then
 * explicitly writes zero to count[0].
 *
 * 0x7580A0 does 15 iterations.  The first result is stored in table[1],
 * not table[0].  Therefore the code used for a symbol of length L is:
 *
 *     table[L]
 *
 * where table[L] was generated from count[L-1].
 */

/* ------------------------------------------------------------------------- */
/* Exact tables from the dump's 0x7CB060/7CB07C/7CB0B8/7CB0D8. */

static const uint8_t ea_length_extra[28] =
{
    0,0,0,0,0,0,0,0,
    1,1,1,1,
    2,2,2,2,
    3,3,3,3,
    4,4,4,4,
    5,5,5,0
};

static const uint16_t ea_length_base[28] =
{
      3,  4,  5,  6,  7,  8,  9, 10,
     11, 13, 15, 17, 19, 23, 27, 31,
     35, 43, 51, 59, 67, 83, 99,115,
    131,163,195,227
};

static const uint8_t ea_distance_extra[30] =
{
    0,0,0,0,
    1,1,2,2,3,3,4,4,5,5,6,6,
    7,7,8,8,9,9,10,10,11,11,
    12,12,13,13
};

static const uint16_t ea_distance_base[30] =
{
       1,    2,    3,    4,    5,    7,    9,   13,
      17,   25,   33,   49,   65,   97,  129,  193,
     257,  385,  513,  769, 1025, 1537, 2049, 3073,
    4097, 6145, 8193,12289,16385,24577
};

/* ------------------------------------------------------------------------- */

int EA_Madden_Decode(const void *input,
                         size_t input_size,
                         void *output,
                         size_t output_capacity)
{
	//sanity check
    if (!input || !output) return -1;
        
    uint8_t main_lengths[EA_MADDEN_MAIN_SYMBOLS];
    uint8_t dist_lengths[EA_MADDEN_DIST_SYMBOLS];
    uint8_t window[EA_MADDEN_WINDOW];

	//variables and arrays for the main Huff tree
	unsigned Hnode_count, Hsymbol_count, Hmax_bits;
    uint8_t Hlengths[EA_MADDEN_MAIN_SYMBOLS];
    uint16_t Hcodes[EA_MADDEN_MAIN_SYMBOLS];
    int Hnodes[EA_MADDEN_MAIN_SYMBOLS * 2 * 3];

	//variables and arrays for the Distance Huff tree
	unsigned Dnode_count, Dsymbol_count, Dmax_bits;
    uint8_t Dlengths[EA_MADDEN_DIST_SYMBOLS];
    uint16_t Dcodes[EA_MADDEN_DIST_SYMBOLS];
    int Dnodes[EA_MADDEN_MAIN_SYMBOLS * 2 * 3];

	//variables to represent the state of the decompressed data
    uint8_t *dst = (uint8_t *)output;
    size_t produced = 0;
    size_t window_pos = 0;

	//variables to represent the state of the compressed payload
	unsigned char *src = (uint8_t*)input;
	int size = input_size;
	int byte_pos = 0;
	unsigned bit_pos = 0;

    memset(window, 0, sizeof(window));

    /*
     * 0x758E50 is the start of each compressed block.  It consumes a
     * one-bit mode and then, for mode 0, rebuilds both Huffman trees.
     * 0x758AD3 returns to that state after symbol 0x100, so 0x100 is an
     * end-of-block marker, not an output byte.
     */

    while (produced < output_capacity)
	{
        uint32_t mode;
		GETBIT(src, byte_pos, bit_pos, mode);
	    if (mode != 0) return -10;

		EA_READ_LENGTHS(src, byte_pos, bit_pos, main_lengths, EA_MADDEN_MAIN_SYMBOLS);

		EA_READ_LENGTHS(src, byte_pos, bit_pos, dist_lengths, EA_MADDEN_DIST_SYMBOLS);

		EA_HUFF_BUILD(Hlengths, Hcodes, Hnodes, Hnode_count, Hmax_bits, main_lengths, EA_MADDEN_MAIN_SYMBOLS);

		EA_HUFF_BUILD(Dlengths, Dcodes, Dnodes, Dnode_count, Dmax_bits, dist_lengths, EA_MADDEN_DIST_SYMBOLS);

        for (;;)
		{
            uint16_t symbol;
			EA_HUFF_DECODE(src, byte_pos, bit_pos, Hnodes, Hnode_count, Hmax_bits, symbol);

            /* 0x000..0x0FF: literal byte. */
            if (symbol < 0x100u)
			{
                uint8_t b = (uint8_t)symbol;

                if (produced >= output_capacity)
				{
					return (int)produced;
				}
                dst[produced++] = b;
                window[window_pos] = b;
                window_pos = (window_pos + 1u) & (EA_MADDEN_WINDOW - 1u);
                continue;
            }

            /* 0x100: end of this Huffman/LZ block. */
            if (symbol == 0x100u)
			{
				break;
			}

            /* 0x101..0x11C: LZ match length symbol. */
            {
                unsigned length_code = (unsigned)symbol - 0x101u;
                unsigned length, distance, distance_code, i;

				EA_DECODE_LENGTH(src, byte_pos, bit_pos, length_code, length);

				EA_HUFF_DECODE(src, byte_pos, bit_pos, Dnodes, Dnode_count, Dmax_bits, distance_code);

				EA_DECODE_DISTANCE(src, byte_pos, bit_pos, distance_code, distance);

                for (i = 0; i < length; ++i)
				{
                    uint8_t b = window[(window_pos - distance) &
                                       (EA_MADDEN_WINDOW - 1u)];

                    if (produced >= output_capacity)
                        return (int)produced;

                    dst[produced++] = b;
                    window[window_pos] = b;
                    window_pos = (window_pos + 1u) &
                                 (EA_MADDEN_WINDOW - 1u);
                }
            }
        }
    }
    return (int)produced;
}

