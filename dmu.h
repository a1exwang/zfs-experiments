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

typedef enum dmu_object_byteswap {
  DMU_BSWAP_UINT8,
  DMU_BSWAP_UINT16,
  DMU_BSWAP_UINT32,
  DMU_BSWAP_UINT64,
  DMU_BSWAP_ZAP,
  DMU_BSWAP_DNODE,
  DMU_BSWAP_OBJSET,
  DMU_BSWAP_ZNODE,
  DMU_BSWAP_OLDACL,
  DMU_BSWAP_ACL,
  /*
   * Allocating a new byteswap type number makes the on-disk format
   * incompatible with any other format that uses the same number.
   *
   * Data can usually be structured to work with one of the
   * DMU_BSWAP_UINT* or DMU_BSWAP_ZAP types.
   */
      DMU_BSWAP_NUMFUNCS
} dmu_object_byteswap_t;
#define	DMU_OT_NEWTYPE 0x80
#define	DMU_OT_METADATA 0x40
#define	DMU_OT_ENCRYPTED 0x20
#define	DMU_OT_BYTESWAP_MASK 0x1f

/*
 * Defines a uint8_t object type. Object types specify if the data
 * in the object is metadata (boolean) and how to byteswap the data
 * (dmu_object_byteswap_t). All of the types created by this method
 * are cached in the dbuf metadata cache.
 */
#define	DMU_OT(byteswap, metadata, encrypted) \
	(DMU_OT_NEWTYPE | \
	((metadata) ? DMU_OT_METADATA : 0) | \
	((encrypted) ? DMU_OT_ENCRYPTED : 0) | \
	((byteswap) & DMU_OT_BYTESWAP_MASK))

#define	DMU_OT_IS_VALID(ot) (((ot) & DMU_OT_NEWTYPE) ? \
	((ot) & DMU_OT_BYTESWAP_MASK) < DMU_BSWAP_NUMFUNCS : \
	(ot) < DMU_OT_NUMTYPES)

#define	DMU_OT_IS_METADATA_CACHED(ot) (((ot) & DMU_OT_NEWTYPE) ? \
	B_TRUE : dmu_ot[(ot)].ot_dbuf_metadata_cache)

/*
 * MDB doesn't have dmu_ot; it defines these macros itself.
 */
#ifndef ZFS_MDB
#define	DMU_OT_IS_METADATA_IMPL(ot) (dmu_ot[ot].ot_metadata)
#define	DMU_OT_IS_ENCRYPTED_IMPL(ot) (dmu_ot[ot].ot_encrypt)
#define	DMU_OT_BYTESWAP_IMPL(ot) (dmu_ot[ot].ot_byteswap)
#endif

#define	DMU_OT_IS_METADATA(ot) (((ot) & DMU_OT_NEWTYPE) ? \
	((ot) & DMU_OT_METADATA) : \
	DMU_OT_IS_METADATA_IMPL(ot))

#define	DMU_OT_IS_DDT(ot) \
	((ot) == DMU_OT_DDT_ZAP)

#define	DMU_OT_IS_ZIL(ot) \
	((ot) == DMU_OT_INTENT_LOG)

/* Note: ztest uses DMU_OT_UINT64_OTHER as a proxy for file blocks */
#define	DMU_OT_IS_FILE(ot) \
	((ot) == DMU_OT_PLAIN_FILE_CONTENTS || (ot) == DMU_OT_UINT64_OTHER)

#define	DMU_OT_IS_ENCRYPTED(ot) (((ot) & DMU_OT_NEWTYPE) ? \
	((ot) & DMU_OT_ENCRYPTED) : \
	DMU_OT_IS_ENCRYPTED_IMPL(ot))

/*
 * These object types use bp_fill != 1 for their L0 bp's. Therefore they can't
 * have their data embedded (i.e. use a BP_IS_EMBEDDED() bp), because bp_fill
 * is repurposed for embedded BPs.
 */
#define	DMU_OT_HAS_FILL(ot) \
	((ot) == DMU_OT_DNODE || (ot) == DMU_OT_OBJSET)

#define	DMU_OT_BYTESWAP(ot) (((ot) & DMU_OT_NEWTYPE) ? \
	((ot) & DMU_OT_BYTESWAP_MASK) : \
	DMU_OT_BYTESWAP_IMPL(ot))

typedef enum dmu_object_type {
  DMU_OT_NONE,
  /* general: */
      DMU_OT_OBJECT_DIRECTORY,	/* ZAP */
  DMU_OT_OBJECT_ARRAY,		/* UINT64 */
  DMU_OT_PACKED_NVLIST,		/* UINT8 (XDR by nvlist_pack/unpack) */
  DMU_OT_PACKED_NVLIST_SIZE,	/* UINT64 */
  DMU_OT_BPOBJ,			/* UINT64 */
  DMU_OT_BPOBJ_HDR,		/* UINT64 */
  /* spa: */
      DMU_OT_SPACE_MAP_HEADER,	/* UINT64 */
  DMU_OT_SPACE_MAP,		/* UINT64 */
  /* zil: */
      DMU_OT_INTENT_LOG,		/* UINT64 */
  /* dmu: */
      DMU_OT_DNODE,			/* DNODE */
  DMU_OT_OBJSET,			/* OBJSET */
  /* dsl: */
      DMU_OT_DSL_DIR,			/* UINT64 */
  DMU_OT_DSL_DIR_CHILD_MAP,	/* ZAP */
  DMU_OT_DSL_DS_SNAP_MAP,		/* ZAP */
  DMU_OT_DSL_PROPS,		/* ZAP */
  DMU_OT_DSL_DATASET,		/* UINT64 */
  /* zpl: */
      DMU_OT_ZNODE,			/* ZNODE */
  DMU_OT_OLDACL,			/* Old ACL */
  DMU_OT_PLAIN_FILE_CONTENTS,	/* UINT8 */
  DMU_OT_DIRECTORY_CONTENTS,	/* ZAP */
  DMU_OT_MASTER_NODE,		/* ZAP */
  DMU_OT_UNLINKED_SET,		/* ZAP */
  /* zvol: */
      DMU_OT_ZVOL,			/* UINT8 */
  DMU_OT_ZVOL_PROP,		/* ZAP */
  /* other; for testing only! */
      DMU_OT_PLAIN_OTHER,		/* UINT8 */
  DMU_OT_UINT64_OTHER,		/* UINT64 */
  DMU_OT_ZAP_OTHER,		/* ZAP */
  /* new object types: */
      DMU_OT_ERROR_LOG,		/* ZAP */
  DMU_OT_SPA_HISTORY,		/* UINT8 */
  DMU_OT_SPA_HISTORY_OFFSETS,	/* spa_his_phys_t */
  DMU_OT_POOL_PROPS,		/* ZAP */
  DMU_OT_DSL_PERMS,		/* ZAP */
  DMU_OT_ACL,			/* ACL */
  DMU_OT_SYSACL,			/* SYSACL */
  DMU_OT_FUID,			/* FUID table (Packed NVLIST UINT8) */
  DMU_OT_FUID_SIZE,		/* FUID table size UINT64 */
  DMU_OT_NEXT_CLONES,		/* ZAP */
  DMU_OT_SCAN_QUEUE,		/* ZAP */
  DMU_OT_USERGROUP_USED,		/* ZAP */
  DMU_OT_USERGROUP_QUOTA,		/* ZAP */
  DMU_OT_USERREFS,		/* ZAP */
  DMU_OT_DDT_ZAP,			/* ZAP */
  DMU_OT_DDT_STATS,		/* ZAP */
  DMU_OT_SA,			/* System attr */
  DMU_OT_SA_MASTER_NODE,		/* ZAP */
  DMU_OT_SA_ATTR_REGISTRATION,	/* ZAP */
  DMU_OT_SA_ATTR_LAYOUTS,		/* ZAP */
  DMU_OT_SCAN_XLATE,		/* ZAP */
  DMU_OT_DEDUP,			/* fake dedup BP from ddt_bp_create() */
  DMU_OT_DEADLIST,		/* ZAP */
  DMU_OT_DEADLIST_HDR,		/* UINT64 */
  DMU_OT_DSL_CLONES,		/* ZAP */
  DMU_OT_BPOBJ_SUBOBJ,		/* UINT64 */
  /*
   * Do not allocate new object types here. Doing so makes the on-disk
   * format incompatible with any other format that uses the same object
   * type number.
   *
   * When creating an object which does not have one of the above types
   * use the DMU_OTN_* type with the correct byteswap and metadata
   * values.
   *
   * The DMU_OTN_* types do not have entries in the dmu_ot table,
   * use the DMU_OT_IS_METADATA() and DMU_OT_BYTESWAP() macros instead
   * of indexing into dmu_ot directly (this works for both DMU_OT_* types
   * and DMU_OTN_* types).
   */
      DMU_OT_NUMTYPES,

  /*
   * Names for valid types declared with DMU_OT().
   */
      DMU_OTN_UINT8_DATA = DMU_OT(DMU_BSWAP_UINT8, B_FALSE, B_FALSE),
  DMU_OTN_UINT8_METADATA = DMU_OT(DMU_BSWAP_UINT8, B_TRUE, B_FALSE),
  DMU_OTN_UINT16_DATA = DMU_OT(DMU_BSWAP_UINT16, B_FALSE, B_FALSE),
  DMU_OTN_UINT16_METADATA = DMU_OT(DMU_BSWAP_UINT16, B_TRUE, B_FALSE),
  DMU_OTN_UINT32_DATA = DMU_OT(DMU_BSWAP_UINT32, B_FALSE, B_FALSE),
  DMU_OTN_UINT32_METADATA = DMU_OT(DMU_BSWAP_UINT32, B_TRUE, B_FALSE),
  DMU_OTN_UINT64_DATA = DMU_OT(DMU_BSWAP_UINT64, B_FALSE, B_FALSE),
  DMU_OTN_UINT64_METADATA = DMU_OT(DMU_BSWAP_UINT64, B_TRUE, B_FALSE),
  DMU_OTN_ZAP_DATA = DMU_OT(DMU_BSWAP_ZAP, B_FALSE, B_FALSE),
  DMU_OTN_ZAP_METADATA = DMU_OT(DMU_BSWAP_ZAP, B_TRUE, B_FALSE),

  DMU_OTN_UINT8_ENC_DATA = DMU_OT(DMU_BSWAP_UINT8, B_FALSE, B_TRUE),
  DMU_OTN_UINT8_ENC_METADATA = DMU_OT(DMU_BSWAP_UINT8, B_TRUE, B_TRUE),
  DMU_OTN_UINT16_ENC_DATA = DMU_OT(DMU_BSWAP_UINT16, B_FALSE, B_TRUE),
  DMU_OTN_UINT16_ENC_METADATA = DMU_OT(DMU_BSWAP_UINT16, B_TRUE, B_TRUE),
  DMU_OTN_UINT32_ENC_DATA = DMU_OT(DMU_BSWAP_UINT32, B_FALSE, B_TRUE),
  DMU_OTN_UINT32_ENC_METADATA = DMU_OT(DMU_BSWAP_UINT32, B_TRUE, B_TRUE),
  DMU_OTN_UINT64_ENC_DATA = DMU_OT(DMU_BSWAP_UINT64, B_FALSE, B_TRUE),
  DMU_OTN_UINT64_ENC_METADATA = DMU_OT(DMU_BSWAP_UINT64, B_TRUE, B_TRUE),
  DMU_OTN_ZAP_ENC_DATA = DMU_OT(DMU_BSWAP_ZAP, B_FALSE, B_TRUE),
  DMU_OTN_ZAP_ENC_METADATA = DMU_OT(DMU_BSWAP_ZAP, B_TRUE, B_TRUE),
} dmu_object_type_t;
