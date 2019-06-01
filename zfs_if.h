#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <inttypes.h>
//#include <sys/spa.h>

#define	SPA_DVAS_PER_BP	3		/* Number of DVAs in a bp	*/

/*
 * All SPA data is represented by 128-bit data virtual addresses (DVAs).
 * The members of the dva_t should be considered opaque outside the SPA.
 */
typedef struct dva {
  uint64_t	dva_word[2];
} dva_t;

/*
 * Some checksums/hashes need a 256-bit initialization salt. This salt is kept
 * secret and is suitable for use in MAC algorithms as the key.
 */
typedef struct zio_cksum_salt {
  uint8_t		zcs_bytes[32];
} zio_cksum_salt_t;

/*
 * Each block has a 256-bit checksum -- strong enough for cryptographic hashes.
 */
typedef struct zio_cksum {
  uint64_t	zc_word[4];
} zio_cksum_t;

typedef struct blkptr {
  dva_t		blk_dva[SPA_DVAS_PER_BP]; /* Data Virtual Addresses */
  uint64_t	blk_prop;	/* size, compression, type, etc	    */
  uint64_t	blk_pad[2];	/* Extra space for the future	    */
  uint64_t	blk_phys_birth;	/* txg when block was allocated	    */
  uint64_t	blk_birth;	/* transaction group at birth	    */
  uint64_t	blk_fill;	/* fill count			    */
  zio_cksum_t	blk_cksum;	/* 256-bit checksum		    */
} blkptr_t;

struct uberblock {
  uint64_t	ub_magic;	/* UBERBLOCK_MAGIC		*/
  uint64_t	ub_version;	/* SPA_VERSION			*/
  uint64_t	ub_txg;		/* txg of last sync		*/
  uint64_t	ub_guid_sum;	/* sum of all vdev guids	*/
  uint64_t	ub_timestamp;	/* UTC time of last sync	*/
  blkptr_t	ub_rootbp;	/* MOS objset_phys_t		*/

  /* highest SPA_VERSION supported by software that wrote this txg */
  uint64_t	ub_software_version;

  /* Maybe missing in uberblocks we read, but always written */
  uint64_t	ub_mmp_magic;	/* MMP_MAGIC			*/
  /*
   * If ub_mmp_delay == 0 and ub_mmp_magic is valid, MMP is off.
   * Otherwise, nanosec since last MMP write.
   */
  uint64_t	ub_mmp_delay;

  /*
   * The ub_mmp_config contains the multihost write interval, multihost
   * fail intervals, sequence number for sub-second granularity, and
   * valid bit mask.  This layout is as follows:
   *
   *   64      56      48      40      32      24      16      8       0
   *   +-------+-------+-------+-------+-------+-------+-------+-------+
   * 0 | Fail Intervals|      Seq      |   Write Interval (ms) | VALID |
   *   +-------+-------+-------+-------+-------+-------+-------+-------+
   *
   * This allows a write_interval of (2^24/1000)s, over 4.5 hours
   *
   * VALID Bits:
   * - 0x01 - Write Interval (ms)
   * - 0x02 - Sequence number exists
   * - 0x04 - Fail Intervals
   * - 0xf8 - Reserved
   */
  uint64_t	ub_mmp_config;

  /*
   * ub_checkpoint_txg indicates two things about the current uberblock:
   *
   * 1] If it is not zero then this uberblock is a checkpoint. If it is
   *    zero, then this uberblock is not a checkpoint.
   *
   * 2] On checkpointed uberblocks, the value of ub_checkpoint_txg is
   *    the ub_txg that the uberblock had at the time we moved it to
   *    the MOS config.
   *
   * The field is set when we checkpoint the uberblock and continues to
   * hold that value even after we've rewound (unlike the ub_txg that
   * is reset to a higher value).
   *
   * Besides checks used to determine whether we are reopening the
   * pool from a checkpointed uberblock [see spa_ld_select_uberblock()],
   * the value of the field is used to determine which ZIL blocks have
   * been allocated according to the ms_sm when we are rewinding to a
   * checkpoint. Specifically, if blk_birth > ub_checkpoint_txg, then
   * the ZIL block is not allocated [see uses of spa_min_claim_txg()].
   */
  uint64_t	ub_checkpoint_txg;
};

  void debug_me(const char *data);


#ifdef __cplusplus
};
#endif