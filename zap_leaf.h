#pragma once

#ifdef	__cplusplus
extern "C" {
#endif

struct zap;
struct zap_name;
struct zap_stats;

#define  ZAP_LEAF_MAGIC 0x2AB1EAF

/* chunk size = 24 bytes */
#define  ZAP_LEAF_CHUNKSIZE 24

/*
 * The amount of space available for chunks is:
 * block size (1<<l->l_bs) - hash entry size (2) * number of hash
 * entries - header space (2*chunksize)
 */
#define  ZAP_LEAF_NUMCHUNKS_BS(bs) \
  (((1<<(bs)) - 2*ZAP_LEAF_HASH_NUMENTRIES_BS(bs)) / \
  ZAP_LEAF_CHUNKSIZE - 2)

#define  ZAP_LEAF_NUMCHUNKS(l) (ZAP_LEAF_NUMCHUNKS_BS(((l)->l_bs)))

#define  ZAP_LEAF_NUMCHUNKS_DEF \
  (ZAP_LEAF_NUMCHUNKS_BS(fzap_default_block_shift))

/*
 * The amount of space within the chunk available for the array is:
 * chunk size - space for type (1) - space for next pointer (2)
 */
#define  ZAP_LEAF_ARRAY_BYTES (ZAP_LEAF_CHUNKSIZE - 3)

#define  ZAP_LEAF_ARRAY_NCHUNKS(bytes) \
  (((bytes)+ZAP_LEAF_ARRAY_BYTES-1)/ZAP_LEAF_ARRAY_BYTES)

/*
 * Low water mark:  when there are only this many chunks free, start
 * growing the ptrtbl.  Ideally, this should be larger than a
 * "reasonably-sized" entry.  20 chunks is more than enough for the
 * largest directory entry (MAXNAMELEN (256) byte name, 8-byte value),
 * while still being only around 3% for 16k blocks.
 */
#define  ZAP_LEAF_LOW_WATER (20)

/*
 * The leaf hash table has block size / 2^5 (32) number of entries,
 * which should be more than enough for the maximum number of entries,
 * which is less than block size / CHUNKSIZE (24) / minimum number of
 * chunks per entry (3).
 */
#define  ZAP_LEAF_HASH_SHIFT_BS(bs) ((bs) - 5)
#define  ZAP_LEAF_HASH_NUMENTRIES_BS(bs) (1 << ZAP_LEAF_HASH_SHIFT_BS(bs))
#define  ZAP_LEAF_HASH_SHIFT(l) (ZAP_LEAF_HASH_SHIFT_BS(((l)->l_bs)))
#define  ZAP_LEAF_HASH_NUMENTRIES(l) (ZAP_LEAF_HASH_NUMENTRIES_BS(((l)->l_bs)))

/*
 * The chunks start immediately after the hash table.  The end of the
 * hash table is at l_hash + HASH_NUMENTRIES, which we simply cast to a
 * chunk_t.
 */
#define  ZAP_LEAF_CHUNK(l, idx) \
  ((zap_leaf_chunk_t *) \
  (zap_leaf_phys(l)->l_hash + ZAP_LEAF_HASH_NUMENTRIES(l)))[idx]
#define  ZAP_LEAF_ENTRY(l, idx) (&ZAP_LEAF_CHUNK(l, idx).l_entry)

typedef enum zap_chunk_type {
  ZAP_CHUNK_FREE = 253,
  ZAP_CHUNK_ENTRY = 252,
  ZAP_CHUNK_ARRAY = 251,
  ZAP_CHUNK_TYPE_MAX = 250
} zap_chunk_type_t;

#define  ZLF_ENTRIES_CDSORTED (1<<0)

/*
 * TAKE NOTE:
 * If zap_leaf_phys_t is modified, zap_leaf_byteswap() must be modified.
 */
typedef struct zap_leaf_phys {
  struct zap_leaf_header {
    /* Public to ZAP */
    uint64_t lh_block_type;    /* ZBT_LEAF */
    uint64_t lh_pad1;
    uint64_t lh_prefix;    /* hash prefix of this leaf */
    uint32_t lh_magic;    /* ZAP_LEAF_MAGIC */
    uint16_t lh_nfree;    /* number free chunks */
    uint16_t lh_nentries;    /* number of entries */
    uint16_t lh_prefix_len;    /* num bits used to id this */

    /* Private to zap_leaf */
    uint16_t lh_freelist;    /* chunk head of free list */
    uint8_t lh_flags;    /* ZLF_* flags */
    uint8_t lh_pad2[11];
  } l_hdr; /* 2 24-byte chunks */

  /*
   * The header is followed by a hash table with
   * ZAP_LEAF_HASH_NUMENTRIES(zap) entries.  The hash table is
   * followed by an array of ZAP_LEAF_NUMCHUNKS(zap)
   * zap_leaf_chunk structures.  These structures are accessed
   * with the ZAP_LEAF_CHUNK() macro.
   */

  uint16_t l_hash[1];
} zap_leaf_phys_t;

typedef union zap_leaf_chunk {
  struct zap_leaf_entry {
    uint8_t le_type; 		/* always ZAP_CHUNK_ENTRY */
    uint8_t le_value_intlen;	/* size of value's ints */
    uint16_t le_next;		/* next entry in hash chain */
    uint16_t le_name_chunk;		/* first chunk of the name */
    uint16_t le_name_numints;	/* ints in name (incl null) */
    uint16_t le_value_chunk;	/* first chunk of the value */
    uint16_t le_value_numints;	/* value length in ints */
    uint32_t le_cd;			/* collision differentiator */
    uint64_t le_hash;		/* hash value of the name */
  } l_entry;
  struct zap_leaf_array {
    uint8_t la_type;		/* always ZAP_CHUNK_ARRAY */
    uint8_t la_array[ZAP_LEAF_ARRAY_BYTES];
    uint16_t la_next;		/* next blk or CHAIN_END */
  } l_array;
  struct zap_leaf_free {
    uint8_t lf_type;		/* always ZAP_CHUNK_FREE */
    uint8_t lf_pad[ZAP_LEAF_ARRAY_BYTES];
    uint16_t lf_next;	/* next in free list, or CHAIN_END */
  } l_free;
} zap_leaf_chunk_t;


#ifdef __cplusplus
}
#endif
