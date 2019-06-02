#include <cassert>
#include <cstring>
#include <algorithm>
#include <errno.h>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

#include <libnvpair.h>
#include "spa.h"
#include "dmu.h"
#include "dnode.h"
#include "dmu_objset.h"
#include "dsl_dir.h"
#include "zap_impl.h"
#include "zap_leaf.h"
#include <lz4.h>


enum zio_compress {
  ZIO_COMPRESS_INHERIT = 0,
  ZIO_COMPRESS_ON,
  ZIO_COMPRESS_OFF,
  ZIO_COMPRESS_LZJB,
  ZIO_COMPRESS_EMPTY,
  ZIO_COMPRESS_GZIP_1,
  ZIO_COMPRESS_GZIP_2,
  ZIO_COMPRESS_GZIP_3,
  ZIO_COMPRESS_GZIP_4,
  ZIO_COMPRESS_GZIP_5,
  ZIO_COMPRESS_GZIP_6,
  ZIO_COMPRESS_GZIP_7,
  ZIO_COMPRESS_GZIP_8,
  ZIO_COMPRESS_GZIP_9,
  ZIO_COMPRESS_ZLE,
  ZIO_COMPRESS_LZ4,
  ZIO_COMPRESS_FUNCTIONS
};

/*
 * NB: lzc_dataset_type should be updated whenever a new objset type is added,
 * if it represents a real type of a dataset that can be created from userland.
 */
typedef enum dmu_objset_type {
  DMU_OST_NONE,
  DMU_OST_META,
  DMU_OST_ZFS,
  DMU_OST_ZVOL,
  DMU_OST_OTHER,			/* For testing only! */
  DMU_OST_ANY,			/* Be careful! */
  DMU_OST_NUMTYPES
} dmu_objset_type_t;


void print_blkptr(const blkptr_t *p) {
  static const char *blkptr_types[] = {
      "none", // 0
      "object_directory",
      "object_array",
      "packed_nvlist",
      "nvlist_size",
      "bplist", // 5
      "bplist_hdr",
      "space_map_header",
      "space_map",
      "intent_log",
      "dnode", // 10
      "objset",
      "dsl_dataset",
  };
  auto t = BP_GET_TYPE(p);
  if (t < sizeof(blkptr_types)/sizeof(blkptr_types[0])) {
    std::cout << "blkptr: type " << blkptr_types[t] << " ";
  } else {
    std::cout << "blkptr: type " << t << " ";
  }
  if (DMU_OT_NONE == t) {
    std::cout << std::endl;
    return;
  }
  std::cout << (BP_GET_BYTEORDER(p) ? "LE" : "BE") << " ";
  std::cout << std::endl;

  std::cout << "  level " << BP_GET_LEVEL(p) << " " << std::endl;
  std::cout << "  psize 0x" << std::hex << BP_GET_PSIZE(p) << " " << std::endl;
  std::cout << "  lsize 0x" << std::hex << BP_GET_LSIZE(p) << " " << std::endl;
  std::cout << "  cksum 0x" << std::hex << BP_GET_CHECKSUM(p) << " " << std::endl;
  std::cout << "  compression 0x" << std::hex << BP_GET_COMPRESS(p) << " " << std::endl;
  std::cout << "  birth 0x" << std::hex << p->blk_birth << " " << std::endl;
  std::cout << "  fill_count 0x" << std::hex << p->blk_fill << " " << std::endl;
  for (const auto & i : p->blk_dva) {
    std::cout << "  vdev 0x" << std::hex << DVA_GET_VDEV(&i) << " off 0x" << std::hex << DVA_GET_OFFSET(&i)
        << " asize 0x" << std::hex << DVA_GET_ASIZE(&i) << " gang " << DVA_GET_GANG(&i) << std::endl;
  }
}

// output must be at least LSIZE
std::vector<uint8_t> read_block(const blkptr_t *p, const void *dev_base_ptr) {
  auto vdev1 = DVA_GET_VDEV(&p->blk_dva[0]);
  uint64_t off1 = DVA_GET_OFFSET(&p->blk_dva[0]);
  auto gang1 = DVA_GET_GANG(&p->blk_dva[0]);
  int asize = DVA_GET_ASIZE(&p->blk_dva[0]);
  int lsize = BP_GET_LSIZE(p);
  assert(vdev1 == 0);

  std::vector<uint8_t> output(lsize, 0);
  auto *blk = (const char *)dev_base_ptr + off1;
  // assert lz4 compression
  if (BP_GET_COMPRESS(p) == ZIO_COMPRESS_OFF || BP_GET_COMPRESS(p) == ZIO_COMPRESS_INHERIT) {
    memcpy(output.data(), blk, lsize);
  } else if (BP_GET_COMPRESS(p) == ZIO_COMPRESS_LZ4) {
    auto input_size = __builtin_bswap32 (*(uint32_t*)blk);
    const int decompressed_size = LZ4_decompress_safe(
        (const char*)blk+sizeof(int32_t),
        (char*)output.data(), input_size, lsize
    );
    assert(decompressed_size == lsize);
  } else {
    std::cerr << "unknown blkptr compression type " << BP_GET_COMPRESS(p) << std::endl;
    assert(0);
  }
  return std::move(output);
}
#define ZFS_SEC_SIZE 512UL

std::vector<uint8_t> read_obj(const objset_phys_t* objset, uint64_t id, const void *dev_base_ptr, int leaf_id) {
  int level = objset->os_meta_dnode.dn_nlevels;
  assert(objset->os_type == DMU_OST_META);
  assert(objset->os_meta_dnode.dn_type == DMU_OT_DNODE);
  auto data = read_block(&objset->os_meta_dnode.dn_blkptr[0], dev_base_ptr);
  blkptr_t *blkptrs = (blkptr_t*)data.data();

  // max indirection 6
  std::vector<uint64_t> offsets;

  size_t radix = (1UL << objset->os_meta_dnode.dn_indblkshift) / sizeof(blkptr_t);
  size_t datablk_size = objset->os_meta_dnode.dn_datablkszsec * ZFS_SEC_SIZE;

  while (level > 0) {
    int curid = id % radix;
    id = id / radix;
    level--;
    offsets.insert(offsets.begin(), curid);
  }
  dnode_phys_t *dnodeptrs;
  data = read_block(&blkptrs[offsets[0]], dev_base_ptr);
  dnodeptrs = (dnode_phys_t*)data.data();
  for (int i = 1; i < offsets.size(); i++) {
    auto dnode = &dnodeptrs[offsets[i]];
//    assert(dnode->dn_nblkptr == 1);
    int j = 0;
    if (i == offsets.size() - 1) {
      j = leaf_id;
    }
    data = read_block(&dnode->dn_blkptr[j], dev_base_ptr);
    dnodeptrs = (dnode_phys_t*)data.data();
  }
  return std::move(data);
}

using namespace std;
int main() {
  const char *vdev_path = "test3";
  int fd = open(vdev_path, O_RDONLY);
  if (fd < 0) {
    cerr << "failed to open file " << vdev_path << ", err: " << strerror(errno) << endl;
    abort();
  }

  struct stat buf;
  if (stat(vdev_path, &buf) < 0) {
    cerr << "failed to stat file " << vdev_path << ", err: " << strerror(errno) << endl;
    abort();
  }
  size_t vdev_size = buf.st_size;

  size_t block_size = 128 * 1024;
  size_t label_offset = 16 * 1024;
  size_t label_size = block_size - label_offset;
  const char *vdev_ptr = (const char*)mmap(nullptr, vdev_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (vdev_ptr == MAP_FAILED) {
    cerr << "failed to mmap " << vdev_path << ", err: " << strerror(errno) << endl;
    abort();
  }
  const char *label_ptr = vdev_ptr + label_offset;

  nvlist_t *list;
  if (nvlist_unpack((char*)label_ptr, label_size, &list, 0) != 0) {
    cerr << "failed unpack zfs labels" << endl;
    abort();
  }

  size_t list_size;
  if (nvlist_size(list, &list_size, NV_ENCODE_XDR) != 0) {
    cerr << "Failed to get nvlist size" << endl;
    abort();
  }

  // print labels
  nvlist_print(stdout, list);

  // print uberblocks
  uint64_t be_magic = 0x00bab10c;
  std::vector<std::pair<int, uint64_t>> txgs;
  for (int i = 0; i < 128; i++) {
    auto ub = (uberblock*)(vdev_ptr + block_size*1 + i * 1024);
    if (ub->ub_magic != 0) {
//      assert(be_magic == ub->ub_magic);
//      cout << "ub magic: " << std::hex << std::setfill('0') << std::setw(8) << i << ", " << ub->ub_magic << endl;

      txgs.emplace_back(i, ub->ub_txg);
//      cout << "ub: " << ub->ub_txg << endl;
    }
  }
  if (txgs.empty()) {
    cerr << "no txg in uberblocks found, aborting" << endl;
    abort();
  }

  std::sort(txgs.begin(), txgs.end(), [](const std::pair<int, uint64_t> &a,const std::pair<int, uint64_t> &b) {
    return a.second > b.second;
  });
  cout << "max txg: " << txgs[0].first << ", " << txgs[0].second << endl;

  int chosen_txg = txgs[0].first;
  auto main_ub = (uberblock*)(vdev_ptr + block_size*1 + chosen_txg * 1024);
//  assert(main_ub->ub_txg == txgs[0].second && main_ub->ub_magic == be_magic);
  cout << "ub_version: " << dec << main_ub->ub_version << endl;
  auto rootbp = &main_ub->ub_rootbp;

  /** get root dnode array from root bp */
  // assert little endian
  assert(BP_GET_BYTEORDER(rootbp) == 1);
  cout << "rootbp: " << endl;
  print_blkptr(rootbp);

  auto rootbp_type = BP_GET_TYPE(rootbp);
  cout << "rootbp type 0x" << rootbp_type << endl;

  uint64_t data_off = 0x400000;
  auto dev_base_ptr = vdev_ptr + data_off;

  auto output = read_block(rootbp, dev_base_ptr);
  auto metadnode = (objset_phys_t*)output.data();

  assert(metadnode->os_type == DMU_OST_META);
  assert(metadnode->os_meta_dnode.dn_type == DMU_OT_DNODE);

  print_blkptr(&metadnode->os_meta_dnode.dn_blkptr[0]);
  std::cout << "root dnodes level " << (int)metadnode->os_meta_dnode.dn_nlevels << endl;
  auto data = read_block(&metadnode->os_meta_dnode.dn_blkptr[0], dev_base_ptr);
  cout << "max blkid " << metadnode->os_meta_dnode.dn_maxblkid << endl;

  auto obj1_data = read_obj(metadnode, 1, dev_base_ptr, 0);

  auto zap = (zap_phys_t*)obj1_data.data();
  assert(zap->zap_block_type == ZBT_HEADER);
  auto *zap_leaves = (uint64_t*)((char*)zap + obj1_data.size() / 2);
  uint64_t zap_leaf0_id = zap_leaves[0];

  auto zap_leaf_data = read_obj(metadnode, 1, dev_base_ptr, 1);
  zap_leaf_phys_t *leaf = (zap_leaf_phys_t*)zap_leaf_data.data();
  cout << "leaf magic " << leaf->l_hdr.lh_magic << endl;
  auto leaf_chunks_data = (char*)leaf + 7216;
  auto *chunks = (zap_leaf_chunk_t*) leaf_chunks_data;
  for (int i = 0; i < leaf->l_hdr.lh_nentries; i++) {
    assert(chunks[i].l_entry.le_type == 252);
  }

//  auto *zap_leaf0 = (zap_leaf_phys_t*)(dev_base_ptr + block_size * zap_leaf0_id);
//  std::cout << "leaf magic: " << hex << zap_leaf0->l_hdr.lh_magic << std::endl;


//  auto dnode1_data = read_block(&root_dnode_blkptrs[0], dev_base_ptr);
//  auto dnode1 = (dnode_phys_t*)dnode1_data.data();
////  std::cout << "dnode1 blkptr " << endl;
////  print_blkptr(&dnode1->dn_blkptr[0]);
//  cout << "dnode1 type " << (int)dnode1->dn_type << endl;
//  assert(dnode1->dn_type == DMU_OT_DSL_DIR);
//
//  cout << "bonus len 0x" << hex << dnode1->dn_bonuslen << endl;
//  auto dsldir = (dsl_dir_phys_t*)dnode1->dn_bonus;
//  cout << "active dataset objnum 0x" << hex << dsldir->dd_head_dataset_obj << endl;
//  cout << "parent obj 0x" << hex << dsldir->dd_parent_obj << endl;
//  cout << "proj zapobj num 0x" << hex << dsldir->dd_props_zapobj << endl;



}
