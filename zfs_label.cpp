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
#include "zfs_if.h"

using namespace std;
int main() {
  const char *vdev_path = "0.vdev";
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
      assert(be_magic == ub->ub_magic);
      cout << "ub magic: " << std::hex << std::setfill('0') << std::setw(8) << i << ", " << ub->ub_magic << endl;

      txgs.push_back({i, ub->ub_txg});
      cout << "ub: " << ub->ub_txg << endl;
    }
  }
  if (txgs.size() == 0) {
    cerr << "no txg in uberblocks found, aborting" << endl;
    abort();
  }

  std::sort(txgs.begin(), txgs.end(), [](const std::pair<int, uint64_t> &a,const std::pair<int, uint64_t> &b) {
    return a.second > b.second;
  });
  cout << "max txg: " << txgs[0].first << ", " << txgs[0].second << endl;


}
