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
//#include <sys/fs/zfs.h>
#include "dnode.h"
#include "dmu_objset.h"
#include <lz4.h>

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

using namespace std;
int main() {
  const char *vdev_path = "x/testfile";
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
      cout << "ub magic: " << std::hex << std::setfill('0') << std::setw(8) << i << ", " << ub->ub_magic << endl;

      txgs.emplace_back(i, ub->ub_txg);
      cout << "ub: " << ub->ub_txg << endl;
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

  // assert little endian
  assert(BP_GET_BYTEORDER(rootbp) == 1);

  auto vdev1 = DVA_GET_VDEV(&rootbp->blk_dva[0]);
  uint64_t off1 = DVA_GET_OFFSET(&rootbp->blk_dva[0]);
  auto gang1 = DVA_GET_GANG(&rootbp->blk_dva[0]);
  int asize = DVA_GET_ASIZE(&rootbp->blk_dva[0]);
  int lsize = BP_GET_LSIZE(rootbp);
  cout << "rootbp: " << endl;
  print_blkptr(rootbp);

  auto rootbp_type = BP_GET_TYPE(rootbp);
  cout << "rootbp prop: " << rootbp->blk_prop << endl;
  cout << "rootbp type 0x" << rootbp_type << endl;

  // assert lz4 compression
  assert(BP_GET_COMPRESS(rootbp) == 0xf);

  uint64_t data_off = 0x400000;
  auto *metadnode_compressed = (objset_phys_t*)(vdev_ptr + data_off + off1);
  auto *metadnode = (objset_phys_t*) new char[lsize];
  auto input_size = __builtin_bswap32 (*(uint32_t*)metadnode_compressed);
  const int decompressed_size = LZ4_decompress_safe(
      (const char*)metadnode_compressed+sizeof(int32_t),
      (char*)metadnode, input_size, lsize
  );
  cout << "dec size " << decompressed_size << endl;

  cout << "os type " << metadnode->os_type << endl;
  cout << "dnnode " << (int)metadnode->os_meta_dnode.dn_type << endl;

  for (int i = 0; i < metadnode->os_meta_dnode.dn_nblkptr; i++) {
    print_blkptr(&metadnode->os_meta_dnode.dn_blkptr[i]);
  }

}
