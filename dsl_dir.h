#pragma once

struct dsl_dataset;

/*
 * DD_FIELD_* are strings that are used in the "extensified" dsl_dir zap object.
 * They should be of the format <reverse-dns>:<field>.
 */

#define	DD_FIELD_FILESYSTEM_COUNT	"com.joyent:filesystem_count"
#define	DD_FIELD_SNAPSHOT_COUNT		"com.joyent:snapshot_count"
#define	DD_FIELD_CRYPTO_KEY_OBJ		"com.datto:crypto_key_obj"
#define	DD_FIELD_LAST_REMAP_TXG		"com.delphix:last_remap_txg"

typedef enum dd_used {
  DD_USED_HEAD,
  DD_USED_SNAP,
  DD_USED_CHILD,
  DD_USED_CHILD_RSRV,
  DD_USED_REFRSRV,
  DD_USED_NUM
} dd_used_t;

#define	DD_FLAG_USED_BREAKDOWN (1<<0)

typedef struct dsl_dir_phys {
  uint64_t dd_creation_time; /* not actually used */
  uint64_t dd_head_dataset_obj;
  uint64_t dd_parent_obj;
  uint64_t dd_origin_obj;
  uint64_t dd_child_dir_zapobj;
  /*
   * how much space our children are accounting for; for leaf
   * datasets, == physical space used by fs + snaps
   */
  uint64_t dd_used_bytes;
  uint64_t dd_compressed_bytes;
  uint64_t dd_uncompressed_bytes;
  /* Administrative quota setting */
  uint64_t dd_quota;
  /* Administrative reservation setting */
  uint64_t dd_reserved;
  uint64_t dd_props_zapobj;
  uint64_t dd_deleg_zapobj; /* dataset delegation permissions */
  uint64_t dd_flags;
  uint64_t dd_used_breakdown[DD_USED_NUM];
  uint64_t dd_clones; /* dsl_dir objects */
  uint64_t dd_pad[13]; /* pad out to 256 bytes for good measure */
} dsl_dir_phys_t;

