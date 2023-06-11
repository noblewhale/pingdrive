#ifndef PINGDRIVE_HEADER_HPP
#define PINGDRIVE_HEADER_HPP

#define FUSE_USE_VERSION 31

#include "global.hpp"

#include <fuse.h>
#include <string>

namespace pingloop::drive
{
  static int NEXT_FILE_ID = 1;

  struct file
  {
    int file_id = -1;
    bool is_dir = false;
    size_t size = 0;
    std::unordered_map<string, file*> children;

    struct timespec access_and_modification_times[2];

    file() { }
    file(bool is_dir) : is_dir(is_dir) { }
  };

  file root_file(true);

  static void* initialize(struct fuse_conn_info* conn, struct fuse_config* cfg)
  {
    (void)conn;
    cfg->kernel_cache = 0;

    return NULL;
  }

  static void get_attribute(file* file, struct stat* stbuf)
  {
    if (file->is_dir)
    {
      stbuf->st_mode = S_IFDIR | 0755;
      stbuf->st_nlink = 2;
      stbuf->st_uid = 33;
      stbuf->st_gid = 33;
    }
    else
    {
      stbuf->st_mode = S_IFREG | 0777;
      stbuf->st_nlink = 1;
      stbuf->st_size = file->size;
      stbuf->st_uid = 33;
      stbuf->st_gid = 33;
    }
  }

  static bool find_file(boost::filesystem::path path, file** out_file)
  {
    if (strcmp(absolute(path).c_str(), "/") == 0)
    {
      std::cout << "is root" << std::endl;
      *out_file = &root_file;
      return true;
    }
    file* current_file = &root_file;
    bool found_file = false;
    for (auto& part : absolute(path))
    {
      if (part == "/") continue;

      std::cout << "finding part " << part << "\n";

      for (auto kv : current_file->children) std::cout << kv.first << std::endl;
      auto fileIter = current_file->children.find(part.string());
      if (fileIter == current_file->children.end())
      {
        return false;
      }

      std::cout << "found part " << part << "\n";

      found_file = true;

      current_file = fileIter->second;
    }

    *out_file = current_file;
    return found_file;
  }

  static int get_attribute(const char* path, struct stat* stbuf, struct fuse_file_info* fi)
  {
    std::cout << "get attribute" << path << std::endl;
    if (path[0] != '/') return -ENOENT;
    std::cout << "made it here" << std::endl;

    (void)fi;
    int res = 0;
    memset(stbuf, 0, sizeof(struct stat));

    file* file;
    bool found_file = find_file(path, &file);

    if (!found_file)
    {
      return -ENOENT;
    }

    std::cout << "found file " << file->file_id << " is dir " << file->is_dir << " size " << file->size << std::endl;

    get_attribute(file, stbuf);

    return res;
  }

  int read_link (const char* path, char* buffer, size_t length)
  {
    std::cout << "read link" << path << std::endl;
    return 0;
  }

  static int read_directory(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi, enum fuse_readdir_flags flags)
  {
    (void)offset;
    (void)fi;
    (void)flags;

    std::cout << "read directory " << path << std::endl;
    file* file;
    bool found_file = find_file(path, &file);
    if (!found_file || !file->is_dir)
    {
      return -ENOENT;
    }

    filler(buf, ".", NULL, 0, fuse_fill_dir_flags::FUSE_FILL_DIR_PLUS);
    filler(buf, "..", NULL, 0, fuse_fill_dir_flags::FUSE_FILL_DIR_PLUS);

    for (auto child : file->children)
    {
      struct stat stat_buf;
      get_attribute(child.second, &stat_buf);
      filler(buf, child.first.c_str(), &stat_buf, 0, fuse_fill_dir_flags::FUSE_FILL_DIR_PLUS);
    }

    return 0;
  }

  int remove_file(const char* path)
  {
    std::cout << "unlink " << path << std::endl;
    return 0;
  }

  int remove_directory(const char* path)
  {
    std::cout << "remove directory " << path << std::endl;
    return 0;
  }

  int create_symlink(const char* path, const char* other_path)
  {
    std::cout << "create symlink " << path << " -> " << other_path << std::endl;
    return 0;
  }

  int rename_file(const char* path, const char* other_path, unsigned int flags)
  {
    std::cout << "rename file " << path << " -> " << other_path << std::endl;
    return 0;
  }

  int create_hardlink(const char* path, const char* other_path)
  {
    std::cout << "create hardlink" << path << " -> " << other_path << std::endl;
    return 0;
  }

  int change_permissions(const char* path, mode_t mode, struct fuse_file_info* fi)
  {
    std::cout << "change permissions " << path << std::endl;
    return 0;
  }

  int change_owner(const char* path, uid_t uid, gid_t gid, struct fuse_file_info* fi)
  {
    std::cout << "change owner " << path << std::endl;
    return 0;
  }

  int change_file_size(const char* path, off_t offset, struct fuse_file_info* fi)
  {
    std::cout << "change file size " << path << "size: " << offset << std::endl;
    return 0;
  }

  static int open_file(const char* path, struct fuse_file_info* fi)
  {
    std::cout << "open file" << path << std::endl;
    file* file;
    bool found_file = find_file(path, &file);
    if (!found_file || file->is_dir)
    {
      return -ENOENT;
    }

    return 0;
  }

  static int read_from_file(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi)
  {
    (void)fi;
    std::cout << "read from file " << path << std::endl;
    file* file;
    bool found_file = find_file(path, &file);
    if (!found_file || file->is_dir)
    {
      return -ENOENT;
    }

    if (offset < 0) return -1;

    size_t positive_offset = (size_t)offset;

    size = std::min(size, file->size - positive_offset);
    std::cout << "Start read size " << size << " offset " << offset << std::endl;

    size_t len = file->size;

    if (positive_offset < len)
    {
      if (positive_offset + size > len)
      {
        size = len - positive_offset;
      }

      size = p.read_from_loop(buf, file->file_id, positive_offset, size);
    }
    else
    {
      size = 0;
    }

    std::cout << "End read size " << size << std::endl;

    return (int)size;
  }

  int write_to_file(const char* path, const char* buff, size_t size, off_t offset, struct fuse_file_info* fi)
  {
    (void)fi;
    file* file;
    bool found_file = find_file(path, &file);
    if (!found_file || file->is_dir)
    {
      return -ENOENT;
    }
    std::cout << "Start write size " << size << " offset " << offset << std::endl;

    int num_bytes_written = (int)p.write_to_loop(buff, file->file_id, offset, size, file->size);

    file->size = std::max(file->size, offset + size);

    return num_bytes_written;
  }

  int open_dir(const char* path, struct fuse_file_info* finfo)
  {
    std::cout << "open dir " << path << std::endl;
    return 0;
  }

  int create_file(const char* pathBuffer, mode_t mode, dev_t dev)
  {
    std::cout << "create file " << pathBuffer << std::endl;
    auto path = boost::filesystem::path(pathBuffer);
    auto file_name = path.filename().string();
    std::cout << "filename " << file_name << std::endl;
    path.remove_filename_and_trailing_separators();
    std::cout << "parent path " << path << std::endl;

    file* parent_dir;
    bool found_file = find_file(path, &parent_dir);
    if (!found_file)
    {
      return -ENOENT;
    }

    std::cout << "parent dir " << parent_dir->file_id << " is dir " << parent_dir->is_dir << std::endl;

    file* new_file = new file(false);
    new_file->file_id = NEXT_FILE_ID;
    NEXT_FILE_ID++;

    parent_dir->children[file_name] = new_file;

    return 0;
  }

  int make_directory(const char* pathBuffer, mode_t mode)
  {
    std::cout << "make dir " << pathBuffer << std::endl;
    auto path = boost::filesystem::path(pathBuffer);
    auto new_directory_name = path.filename().string();
    path.remove_leaf();

    file* parent_dir;
    bool found_file = find_file(path, &parent_dir);
    if (!found_file)
    {
      return -ENOENT;
    }

    file* new_directory = new file(true);
    parent_dir->children[new_directory_name] = new_directory;

    return 0;
  }

  int set_access_and_modification_times(const char* pathBuffer, const struct timespec tv[2], struct fuse_file_info* fi)
  {
    std::cout << "set access and modification times " << pathBuffer << std::endl;
    auto path = boost::filesystem::path(pathBuffer);

    file* file;
    bool found_file = find_file(path, &file);
    if (!found_file)
    {
      return -ENOENT;
    }

    file->access_and_modification_times[0] = tv[0];
    file->access_and_modification_times[1] = tv[1];

    return 0;
  }

  void clean_up_recursive(file* parent)
  {
    for (auto child : parent->children)
    {
      clean_up_recursive(child.second);
      delete child.second;
    }
    parent->children.clear();
  }

  void clean_up()
  {
    clean_up_recursive(&root_file);
  }

  static const struct fuse_operations operations = {
          .getattr = get_attribute,
          .mknod = create_file,
          .mkdir = make_directory,
          .unlink = remove_file,
          .rmdir = remove_directory,
          .symlink = create_symlink,
          .rename = rename_file,
          .link = create_hardlink,
          .chmod = change_permissions,
          .chown = change_owner,
          .truncate = change_file_size,
          .open = open_file,
          .read = read_from_file,
          .write = write_to_file,
          .opendir = open_dir,
          .readdir = read_directory,
          .init = initialize,
          .utimens = set_access_and_modification_times
  };
}
#endif