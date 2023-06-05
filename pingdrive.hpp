#ifndef PINGDRIVE_HEADER_HPP
#define PINGDRIVE_HEADER_HPP

#define FUSE_USE_VERSION 31

#include <fuse.h>
#include <string>

namespace pingloop
{
  namespace drive
  {
    static void* initialize(struct fuse_conn_info* conn, struct fuse_config* cfg)
    {
      (void)conn;
      cfg->kernel_cache = 0;
      return NULL;
    }

    static int get_attribute(const char* path, struct stat* stbuf, struct fuse_file_info* fi)
    {
      (void)fi;
      int res = 0;

      memset(stbuf, 0, sizeof(struct stat));
      if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        stbuf->st_uid = 33;
        stbuf->st_gid = 33;
      }
      else if (strcmp(path + 1, "index.html") == 0) {
        stbuf->st_mode = S_IFREG | 0777;
        stbuf->st_nlink = 1;
        stbuf->st_size = p->get_size();
        stbuf->st_uid = 33;
        stbuf->st_gid = 33;
      }
      else
        res = -ENOENT;

      return res;
    }

    static int read_directory(const char* path, void* buf, fuse_fill_dir_t filler,
      off_t offset, struct fuse_file_info* fi,
      enum fuse_readdir_flags flags)
    {
      (void)offset;
      (void)fi;
      (void)flags;

      if (strcmp(path, "/") != 0)
        return -ENOENT;

      filler(buf, ".", NULL, 0, fuse_fill_dir_flags::FUSE_FILL_DIR_PLUS);
      filler(buf, "..", NULL, 0, fuse_fill_dir_flags::FUSE_FILL_DIR_PLUS);
      filler(buf, "index.html", NULL, 0, fuse_fill_dir_flags::FUSE_FILL_DIR_PLUS);

      return 0;
    }

    static int open_file(const char* path, struct fuse_file_info* fi)
    {
      if (strcmp(path + 1, "index.html") != 0)
        return -ENOENT;

      //if ((fi->flags & O_ACCMODE) != O_RDONLY)
        //return -EACCES;

      return 0;
    }

    static int read_from_file(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi)
    {
      (void)fi;

      if (strcmp(path + 1, "index.html") != 0) return -ENOENT;

      std::cout << "Start read size " << size << " offset " << offset << std::endl;

      size_t len = p->get_size();

      if (offset < len)
      {
        if (offset + size > len)
        {
          size = len - offset;
        }

        size = p->read_from_loop(buf, offset, size);
      }
      else
      {
        size = 0;
      }

      std::cout << "End read size " << size << std::endl;

      return size;
    }

    int write_to_file(const char* path, const char* buff, size_t size, off_t offset, struct fuse_file_info* fi)
    {
      (void)fi;

      if (strcmp(path + 1, "index.html") != 0) return -ENOENT;

      std::cout << "Start write size " << size << " offset " << offset << std::endl;

      return p->write_to_loop(buff, offset, size);;
    }

    static const struct fuse_operations operations = {
            .getattr = get_attribute,
            .open = open_file,
            .read = read_from_file,
            .write = write_to_file,
            .readdir = read_directory,
            .init = initialize,
    };
  }
}
#endif