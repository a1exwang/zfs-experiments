/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2013 by Saso Kiselkov. All rights reserved.
 * Copyright (c) 2014 Spectra Logic Corporation, All rights reserved.
 */

/* Portions Copyright 2010 Robert Milkowski */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <inttypes.h>
#include "sysmacros.h"
//#include <sys/spa.h>
/*
 * Forward references that lots of things need.
 */
typedef struct spa spa_t;
typedef struct vdev vdev_t;
typedef struct metaslab metaslab_t;
typedef struct metaslab_group metaslab_group_t;
typedef struct metaslab_class metaslab_class_t;
typedef struct zio zio_t;
typedef struct zilog zilog_t;
typedef struct spa_aux_vdev spa_aux_vdev_t;
typedef struct ddt ddt_t;
typedef struct ddt_entry ddt_entry_t;
typedef struct zbookmark_phys zbookmark_phys_t;

struct dsl_pool;
struct dsl_dataset;
struct dsl_crypto_params;

/*
 * General-purpose 32-bit and 64-bit bitfield encodings.
 */
#define	BF32_DECODE(x, low, len)	P2PHASE((x) >> (low), 1U << (len))
#define	BF64_DECODE(x, low, len)	P2PHASE((x) >> (low), 1ULL << (len))
#define	BF32_ENCODE(x, low, len)	(P2PHASE((x), 1U << (len)) << (low))
#define	BF64_ENCODE(x, low, len)	(P2PHASE((x), 1ULL << (len)) << (low))

#define	BF32_GET(x, low, len)		BF32_DECODE(x, low, len)
#define	BF64_GET(x, low, len)		BF64_DECODE(x, low, len)

#define	BF32_SET(x, low, len, val) do { \
	ASSERT3U(val, <, 1U << (len)); \
	ASSERT3U(low + len, <=, 32); \
	(x) ^= BF32_ENCODE((x >> low) ^ (val), low, len); \
_NOTE(CONSTCOND) } while (0)

#define	BF64_SET(x, low, len, val) do { \
	ASSERT3U(val, <, 1ULL << (len)); \
	ASSERT3U(low + len, <=, 64); \
	((x) ^= BF64_ENCODE((x >> low) ^ (val), low, len)); \
_NOTE(CONSTCOND) } while (0)

#define	BF32_GET_SB(x, low, len, shift, bias)	\
	((BF32_GET(x, low, len) + (bias)) << (shift))
#define	BF64_GET_SB(x, low, len, shift, bias)	\
	((BF64_GET(x, low, len) + (bias)) << (shift))

#define	BF32_SET_SB(x, low, len, shift, bias, val) do { \
	ASSERT(IS_P2ALIGNED(val, 1U << shift)); \
	ASSERT3S((val) >> (shift), >=, bias); \
	BF32_SET(x, low, len, ((val) >> (shift)) - (bias)); \
_NOTE(CONSTCOND) } while (0)
#define	BF64_SET_SB(x, low, len, shift, bias, val) do { \
	ASSERT(IS_P2ALIGNED(val, 1ULL << shift)); \
	ASSERT3S((val) >> (shift), >=, bias); \
	BF64_SET(x, low, len, ((val) >> (shift)) - (bias)); \
_NOTE(CONSTCOND) } while (0)

/*
 * We currently support block sizes from 512 bytes to 16MB.
 * The benefits of larger blocks, and thus larger IO, need to be weighed
 * against the cost of COWing a giant block to modify one byte, and the
 * large latency of reading or writing a large block.
 *
 * Note that although blocks up to 16MB are supported, the recordsize
 * property can not be set larger than zfs_max_recordsize (default 1MB).
 * See the comment near zfs_max_recordsize in dsl_dataset.c for details.
 *
 * Note that although the LSIZE field of the blkptr_t can store sizes up
 * to 32MB, the dnode's dn_datablkszsec can only store sizes up to
 * 32MB - 512 bytes.  Therefore, we limit SPA_MAXBLOCKSIZE to 16MB.
 */
#define	SPA_MINBLOCKSHIFT	9
#define	SPA_OLD_MAXBLOCKSHIFT	17
#define	SPA_MAXBLOCKSHIFT	24
#define	SPA_MINBLOCKSIZE	(1ULL << SPA_MINBLOCKSHIFT)
#define	SPA_OLD_MAXBLOCKSIZE	(1ULL << SPA_OLD_MAXBLOCKSHIFT)
#define	SPA_MAXBLOCKSIZE	(1ULL << SPA_MAXBLOCKSHIFT)

/*
 * Alignment Shift (ashift) is an immutable, internal top-level vdev property
 * which can only be set at vdev creation time. Physical writes are always done
 * according to it, which makes 2^ashift the smallest possible IO on a vdev.
 *
 * We currently allow values ranging from 512 bytes (2^9 = 512) to 64 KiB
 * (2^16 = 65,536).
 */
#define	ASHIFT_MIN		9
#define	ASHIFT_MAX		16

/*
 * Size of block to hold the configuration data (a packed nvlist)
 */
#define	SPA_CONFIG_BLOCKSIZE	(1ULL << 14)

/*
 * The DVA size encodings for LSIZE and PSIZE support blocks up to 32MB.
 * The ASIZE encoding should be at least 64 times larger (6 more bits)
 * to support up to 4-way RAID-Z mirror mode with worst-case gang block
 * overhead, three DVAs per bp, plus one more bit in case we do anything
 * else that expands the ASIZE.
 */
#define	SPA_LSIZEBITS		16	/* LSIZE up to 32M (2^16 * 512)	*/
#define	SPA_PSIZEBITS		16	/* PSIZE up to 32M (2^16 * 512)	*/
#define	SPA_ASIZEBITS		24	/* ASIZE up to 64 times larger	*/

#define	SPA_COMPRESSBITS	7
#define	SPA_VDEVBITS		24


#define	BPE_GET_ETYPE(bp)	\
	(ASSERT(BP_IS_EMBEDDED(bp)), \
	BF64_GET((bp)->blk_prop, 40, 8))
#define	BPE_SET_ETYPE(bp, t)	do { \
	ASSERT(BP_IS_EMBEDDED(bp)); \
	BF64_SET((bp)->blk_prop, 40, 8, t); \
_NOTE(CONSTCOND) } while (0)

#define	BPE_GET_LSIZE(bp)	\
	(ASSERT(BP_IS_EMBEDDED(bp)), \
	BF64_GET_SB((bp)->blk_prop, 0, 25, 0, 1))
#define	BPE_SET_LSIZE(bp, x)	do { \
	ASSERT(BP_IS_EMBEDDED(bp)); \
	BF64_SET_SB((bp)->blk_prop, 0, 25, 0, 1, x); \
_NOTE(CONSTCOND) } while (0)

#define	BPE_GET_PSIZE(bp)	\
	(ASSERT(BP_IS_EMBEDDED(bp)), \
	BF64_GET_SB((bp)->blk_prop, 25, 7, 0, 1))
#define	BPE_SET_PSIZE(bp, x)	do { \
	ASSERT(BP_IS_EMBEDDED(bp)); \
	BF64_SET_SB((bp)->blk_prop, 25, 7, 0, 1, x); \
_NOTE(CONSTCOND) } while (0)

typedef enum bp_embedded_type {
  BP_EMBEDDED_TYPE_DATA,
  BP_EMBEDDED_TYPE_RESERVED, /* Reserved for an unintegrated feature. */
  NUM_BP_EMBEDDED_TYPES = BP_EMBEDDED_TYPE_RESERVED
} bp_embedded_type_t;

#define	BPE_NUM_WORDS 14
#define	BPE_PAYLOAD_SIZE (BPE_NUM_WORDS * sizeof (uint64_t))
#define	BPE_IS_PAYLOADWORD(bp, wp) \
	((wp) != &(bp)->blk_prop && (wp) != &(bp)->blk_birth)

#define	SPA_BLKPTRSHIFT	7		/* blkptr_t is 128 bytes	*/
#define	SPA_DVAS_PER_BP	3		/* Number of DVAs in a bp	*/
#define	SPA_SYNC_MIN_VDEVS 3		/* min vdevs to update during sync */
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
/*
 * Macros to get and set fields in a bp or DVA.
 */
#define	DVA_GET_ASIZE(dva)	\
	BF64_GET_SB((dva)->dva_word[0], 0, SPA_ASIZEBITS, SPA_MINBLOCKSHIFT, 0)
#define	DVA_SET_ASIZE(dva, x)	\
	BF64_SET_SB((dva)->dva_word[0], 0, SPA_ASIZEBITS, \
	SPA_MINBLOCKSHIFT, 0, x)

#define	DVA_GET_GRID(dva)	BF64_GET((dva)->dva_word[0], 24, 8)
#define	DVA_SET_GRID(dva, x)	BF64_SET((dva)->dva_word[0], 24, 8, x)

#define	DVA_GET_VDEV(dva)	BF64_GET((dva)->dva_word[0], 32, SPA_VDEVBITS)
#define	DVA_SET_VDEV(dva, x)	\
	BF64_SET((dva)->dva_word[0], 32, SPA_VDEVBITS, x)

#define	DVA_GET_OFFSET(dva)	\
	BF64_GET_SB((dva)->dva_word[1], 0, 63, SPA_MINBLOCKSHIFT, 0)
#define	DVA_SET_OFFSET(dva, x)	\
	BF64_SET_SB((dva)->dva_word[1], 0, 63, SPA_MINBLOCKSHIFT, 0, x)

#define	DVA_GET_GANG(dva)	BF64_GET((dva)->dva_word[1], 63, 1)
#define	DVA_SET_GANG(dva, x)	BF64_SET((dva)->dva_word[1], 63, 1, x)

#define	BP_GET_LSIZE(bp)	\
	(BP_IS_EMBEDDED(bp) ?	\
	(BPE_GET_ETYPE(bp) == BP_EMBEDDED_TYPE_DATA ? BPE_GET_LSIZE(bp) : 0): \
	BF64_GET_SB((bp)->blk_prop, 0, SPA_LSIZEBITS, SPA_MINBLOCKSHIFT, 1))
#define	BP_SET_LSIZE(bp, x)	do { \
	ASSERT(!BP_IS_EMBEDDED(bp)); \
	BF64_SET_SB((bp)->blk_prop, \
	    0, SPA_LSIZEBITS, SPA_MINBLOCKSHIFT, 1, x); \
_NOTE(CONSTCOND) } while (0)

#define	BP_GET_PSIZE(bp)	\
	(BP_IS_EMBEDDED(bp) ? 0 : \
	BF64_GET_SB((bp)->blk_prop, 16, SPA_PSIZEBITS, SPA_MINBLOCKSHIFT, 1))
#define	BP_SET_PSIZE(bp, x)	do { \
	ASSERT(!BP_IS_EMBEDDED(bp)); \
	BF64_SET_SB((bp)->blk_prop, \
	    16, SPA_PSIZEBITS, SPA_MINBLOCKSHIFT, 1, x); \
_NOTE(CONSTCOND) } while (0)

#define	BP_GET_COMPRESS(bp)		\
	BF64_GET((bp)->blk_prop, 32, SPA_COMPRESSBITS)
#define	BP_SET_COMPRESS(bp, x)		\
	BF64_SET((bp)->blk_prop, 32, SPA_COMPRESSBITS, x)

#define	BP_IS_EMBEDDED(bp)		BF64_GET((bp)->blk_prop, 39, 1)
#define	BP_SET_EMBEDDED(bp, x)		BF64_SET((bp)->blk_prop, 39, 1, x)

#define	BP_GET_CHECKSUM(bp)		\
	(BP_IS_EMBEDDED(bp) ? ZIO_CHECKSUM_OFF : \
	BF64_GET((bp)->blk_prop, 40, 8))
#define	BP_SET_CHECKSUM(bp, x)		do { \
	ASSERT(!BP_IS_EMBEDDED(bp)); \
	BF64_SET((bp)->blk_prop, 40, 8, x); \
_NOTE(CONSTCOND) } while (0)

#define	BP_GET_TYPE(bp)			BF64_GET((bp)->blk_prop, 48, 8)
#define	BP_SET_TYPE(bp, x)		BF64_SET((bp)->blk_prop, 48, 8, x)

#define	BP_GET_LEVEL(bp)		BF64_GET((bp)->blk_prop, 56, 5)
#define	BP_SET_LEVEL(bp, x)		BF64_SET((bp)->blk_prop, 56, 5, x)

/* encrypted, authenticated, and MAC cksum bps use the same bit */
#define	BP_USES_CRYPT(bp)		BF64_GET((bp)->blk_prop, 61, 1)
#define	BP_SET_CRYPT(bp, x)		BF64_SET((bp)->blk_prop, 61, 1, x)

#define	BP_IS_ENCRYPTED(bp)			\
	(BP_USES_CRYPT(bp) &&			\
	BP_GET_LEVEL(bp) <= 0 &&		\
	DMU_OT_IS_ENCRYPTED(BP_GET_TYPE(bp)))

#define	BP_IS_AUTHENTICATED(bp)			\
	(BP_USES_CRYPT(bp) &&			\
	BP_GET_LEVEL(bp) <= 0 &&		\
	!DMU_OT_IS_ENCRYPTED(BP_GET_TYPE(bp)))

#define	BP_HAS_INDIRECT_MAC_CKSUM(bp)		\
	(BP_USES_CRYPT(bp) && BP_GET_LEVEL(bp) > 0)

#define	BP_IS_PROTECTED(bp)			\
	(BP_IS_ENCRYPTED(bp) || BP_IS_AUTHENTICATED(bp))

#define	BP_GET_DEDUP(bp)		BF64_GET((bp)->blk_prop, 62, 1)
#define	BP_SET_DEDUP(bp, x)		BF64_SET((bp)->blk_prop, 62, 1, x)

#define	BP_GET_BYTEORDER(bp)		BF64_GET((bp)->blk_prop, 63, 1)
#define	BP_SET_BYTEORDER(bp, x)		BF64_SET((bp)->blk_prop, 63, 1, x)

#define	BP_PHYSICAL_BIRTH(bp)		\
	(BP_IS_EMBEDDED(bp) ? 0 : \
	(bp)->blk_phys_birth ? (bp)->blk_phys_birth : (bp)->blk_birth)

#define	BP_SET_BIRTH(bp, logical, physical)	\
{						\
	ASSERT(!BP_IS_EMBEDDED(bp));		\
	(bp)->blk_birth = (logical);		\
	(bp)->blk_phys_birth = ((logical) == (physical) ? 0 : (physical)); \
}

#define	BP_GET_FILL(bp)				\
	((BP_IS_ENCRYPTED(bp)) ? BF64_GET((bp)->blk_fill, 0, 32) : \
	((BP_IS_EMBEDDED(bp)) ? 1 : (bp)->blk_fill))

#define	BP_SET_FILL(bp, fill)			\
{						\
	if (BP_IS_ENCRYPTED(bp))			\
		BF64_SET((bp)->blk_fill, 0, 32, fill); \
	else					\
		(bp)->blk_fill = fill;		\
}

#define	BP_GET_IV2(bp)				\
	(ASSERT(BP_IS_ENCRYPTED(bp)),		\
	BF64_GET((bp)->blk_fill, 32, 32))
#define	BP_SET_IV2(bp, iv2)			\
{						\
	ASSERT(BP_IS_ENCRYPTED(bp));		\
	BF64_SET((bp)->blk_fill, 32, 32, iv2);	\
}

#define	BP_IS_METADATA(bp)	\
	(BP_GET_LEVEL(bp) > 0 || DMU_OT_IS_METADATA(BP_GET_TYPE(bp)))

#define	BP_GET_ASIZE(bp)	\
	(BP_IS_EMBEDDED(bp) ? 0 : \
	DVA_GET_ASIZE(&(bp)->blk_dva[0]) + \
	DVA_GET_ASIZE(&(bp)->blk_dva[1]) + \
	(DVA_GET_ASIZE(&(bp)->blk_dva[2]) * !BP_IS_ENCRYPTED(bp)))

#define	BP_GET_UCSIZE(bp)	\
	(BP_IS_METADATA(bp) ? BP_GET_PSIZE(bp) : BP_GET_LSIZE(bp))

#define	BP_GET_NDVAS(bp)	\
	(BP_IS_EMBEDDED(bp) ? 0 : \
	!!DVA_GET_ASIZE(&(bp)->blk_dva[0]) + \
	!!DVA_GET_ASIZE(&(bp)->blk_dva[1]) + \
	(!!DVA_GET_ASIZE(&(bp)->blk_dva[2]) * !BP_IS_ENCRYPTED(bp)))

#define	BP_COUNT_GANG(bp)	\
	(BP_IS_EMBEDDED(bp) ? 0 : \
	(DVA_GET_GANG(&(bp)->blk_dva[0]) + \
	DVA_GET_GANG(&(bp)->blk_dva[1]) + \
	(DVA_GET_GANG(&(bp)->blk_dva[2]) * !BP_IS_ENCRYPTED(bp))))

#define	DVA_EQUAL(dva1, dva2)	\
	((dva1)->dva_word[1] == (dva2)->dva_word[1] && \
	(dva1)->dva_word[0] == (dva2)->dva_word[0])

#define	BP_EQUAL(bp1, bp2)	\
	(BP_PHYSICAL_BIRTH(bp1) == BP_PHYSICAL_BIRTH(bp2) &&	\
	(bp1)->blk_birth == (bp2)->blk_birth &&			\
	DVA_EQUAL(&(bp1)->blk_dva[0], &(bp2)->blk_dva[0]) &&	\
	DVA_EQUAL(&(bp1)->blk_dva[1], &(bp2)->blk_dva[1]) &&	\
	DVA_EQUAL(&(bp1)->blk_dva[2], &(bp2)->blk_dva[2]))


#define	DVA_IS_VALID(dva)	(DVA_GET_ASIZE(dva) != 0)

#define	BP_IDENTITY(bp)		(ASSERT(!BP_IS_EMBEDDED(bp)), &(bp)->blk_dva[0])
#define	BP_IS_GANG(bp)		\
	(BP_IS_EMBEDDED(bp) ? B_FALSE : DVA_GET_GANG(BP_IDENTITY(bp)))
#define	DVA_IS_EMPTY(dva)	((dva)->dva_word[0] == 0ULL &&	\
				(dva)->dva_word[1] == 0ULL)
#define	BP_IS_HOLE(bp) \
	(!BP_IS_EMBEDDED(bp) && DVA_IS_EMPTY(BP_IDENTITY(bp)))

/* BP_IS_RAIDZ(bp) assumes no block compression */
#define	BP_IS_RAIDZ(bp)		(DVA_GET_ASIZE(&(bp)->blk_dva[0]) > \
				BP_GET_PSIZE(bp))

#define	BP_ZERO(bp)				\
{						\
	(bp)->blk_dva[0].dva_word[0] = 0;	\
	(bp)->blk_dva[0].dva_word[1] = 0;	\
	(bp)->blk_dva[1].dva_word[0] = 0;	\
	(bp)->blk_dva[1].dva_word[1] = 0;	\
	(bp)->blk_dva[2].dva_word[0] = 0;	\
	(bp)->blk_dva[2].dva_word[1] = 0;	\
	(bp)->blk_prop = 0;			\
	(bp)->blk_pad[0] = 0;			\
	(bp)->blk_pad[1] = 0;			\
	(bp)->blk_phys_birth = 0;		\
	(bp)->blk_birth = 0;			\
	(bp)->blk_fill = 0;			\
	ZIO_SET_CHECKSUM(&(bp)->blk_cksum, 0, 0, 0, 0);	\
}

#ifdef _BIG_ENDIAN
#define	ZFS_HOST_BYTEORDER	(0ULL)
#else
#define	ZFS_HOST_BYTEORDER	(1ULL)
#endif

#define	BP_SHOULD_BYTESWAP(bp)	(BP_GET_BYTEORDER(bp) != ZFS_HOST_BYTEORDER)

#define	BP_SPRINTF_LEN	400

  void debug_me(const char *data);


#ifdef __cplusplus
};
#endif