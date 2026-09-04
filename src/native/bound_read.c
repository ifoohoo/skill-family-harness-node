#define _DARWIN_C_SOURCE 1
#define _GNU_SOURCE 1

#include <node_api.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <sys/stdio.h>
#ifndef RENAME_EXCL
#error "RENAME_EXCL is unavailable"
#endif
#ifndef RENAME_SWAP
#error "RENAME_SWAP is unavailable"
#endif
#define PLATFORM_NAME "darwin"
#elif defined(__linux__)
#include <linux/fs.h>
#include <sys/syscall.h>
#ifndef RENAME_NOREPLACE
#error "RENAME_NOREPLACE is unavailable"
#endif
#ifndef RENAME_EXCHANGE
#error "RENAME_EXCHANGE is unavailable"
#endif
#ifndef SYS_renameat2
#error "SYS_renameat2 is unavailable"
#endif
#define PLATFORM_NAME "linux"
#else
#error "boundRead is unsupported on this operating system"
#endif

#ifndef O_NOFOLLOW
#error "O_NOFOLLOW is required by the bound read contract"
#endif
#ifndef O_DIRECTORY
#error "O_DIRECTORY is required by the bound read contract"
#endif

#define NAPI_OK(call, message) do { if ((call) != napi_ok) { napi_throw_error(env, NULL, message); return NULL; } } while (0)

static napi_value throw_last(napi_env env, const char *message) {
  napi_throw_error(env, NULL, message);
  return NULL;
}

static napi_value set_string(napi_env env, napi_value object, const char *name, const char *value);

/* Failure reasons are the only closed internal set the JS layer may branch
 * on: the five member-policy reasons prove a static member violation under a
 * stable root; every other native fault (permission, allocation, read, …)
 * collapses to "native-io". `errorCode` stays purely diagnostic and must not
 * decide public disposition. */
static napi_value failure_result(napi_env env, int code, const char *failure_reason, const struct stat *root_stat) {
  napi_value result, value;
  char decimal[64];
  NAPI_OK(napi_create_object(env, &result), "cannot create native result");
  NAPI_OK(napi_create_int32(env, code, &value), "cannot encode native error");
  NAPI_OK(napi_set_named_property(env, result, "errorCode", value), "cannot set native error");
  NAPI_OK(napi_get_boolean(env, false, &value), "cannot encode native status");
  NAPI_OK(napi_set_named_property(env, result, "ok", value), "cannot set native status");
  if (failure_reason != NULL) {
    if (set_string(env, result, "failureReason", failure_reason) == NULL) return NULL;
  }
  if (root_stat == NULL) return result;
  snprintf(decimal, sizeof(decimal), "%llu", (unsigned long long)root_stat->st_dev);
  if (set_string(env, result, "rootDevice", decimal) == NULL) return NULL;
  snprintf(decimal, sizeof(decimal), "%llu", (unsigned long long)root_stat->st_ino);
  if (set_string(env, result, "rootInode", decimal) == NULL) return NULL;
  if (napi_create_int64(env, (int64_t)(root_stat->st_mode & 0777), &value) != napi_ok ||
      napi_set_named_property(env, result, "rootMode", value) != napi_ok) {
    napi_throw_error(env, NULL, "cannot encode native root identity");
    return NULL;
  }
  return result;
}

static const char *intermediate_failure_reason(int code) {
  if (code == ELOOP || code == ENOTDIR) return "intermediate-not-real-directory";
  if (code == ENOENT) return "member-missing";
  return "native-io";
}

static const char *leaf_open_failure_reason(int code) {
  if (code == ELOOP) return "leaf-symbolic-link";
  if (code == ENOENT) return "member-missing";
  return "native-io";
}

/* Closes the walk fds exactly once: current_fd aliases root_fd until the
 * first intermediate directory is opened, so it is skipped in that case. */
static void close_walk_fds(int root_fd, int current_fd) {
  if (current_fd >= 0 && current_fd != root_fd) close(current_fd);
  if (root_fd >= 0) close(root_fd);
}

static int read_string(napi_env env, napi_value value, char *buffer, size_t capacity, const char *message) {
  napi_valuetype type;
  size_t length = 0;
  if (napi_typeof(env, value, &type) != napi_ok || type != napi_string ||
      napi_get_value_string_utf8(env, value, NULL, 0, &length) != napi_ok ||
      length == 0 || length >= capacity ||
      napi_get_value_string_utf8(env, value, buffer, capacity, &length) != napi_ok) {
    napi_throw_type_error(env, NULL, message);
    return 0;
  }
  return 1;
}

static int read_segment(napi_env env, napi_value value, char output[NAME_MAX + 1]) {
  if (!read_string(env, value, output, NAME_MAX + 1, "path segments must be bounded strings")) return 0;
  if (strcmp(output, ".") == 0 || strcmp(output, "..") == 0 || strchr(output, '/') != NULL || strchr(output, '\\') != NULL) {
    napi_throw_type_error(env, NULL, "path segments must be single safe relative components");
    return 0;
  }
  return 1;
}

static napi_value set_string(napi_env env, napi_value object, const char *name, const char *value) {
  napi_value encoded;
  if (napi_create_string_utf8(env, value, NAPI_AUTO_LENGTH, &encoded) != napi_ok ||
      napi_set_named_property(env, object, name, encoded) != napi_ok) {
    napi_throw_error(env, NULL, "cannot encode native identity");
    return NULL;
  }
  return object;
}

static napi_value ReadFileBoundNative(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value argv[2], result, value;
  char root[PATH_MAX];
  napi_valuetype type;
  int root_fd = -1;
  int current_fd = -1;
  int leaf_fd = -1;
  struct stat root_stat;
  struct stat leaf_stat;
  unsigned char *bytes = NULL;
  size_t length = 0;
  size_t capacity = 0;
  NAPI_OK(napi_get_cb_info(env, info, &argc, argv, NULL, NULL), "cannot read native arguments");
  if (argc != 2 || !read_string(env, argv[0], root, sizeof(root), "root must be a bounded absolute path")) return NULL;
  if (root[0] != '/') {
    napi_throw_type_error(env, NULL, "root must be an absolute path");
    return NULL;
  }
  bool is_array = false;
  if (napi_typeof(env, argv[1], &type) != napi_ok || type != napi_object ||
      napi_is_array(env, argv[1], &is_array) != napi_ok || !is_array) {
    napi_throw_type_error(env, NULL, "segments must be an array");
    return NULL;
  }
  uint32_t segment_count = 0;
  NAPI_OK(napi_get_array_length(env, argv[1], &segment_count), "cannot inspect segment count");
  if (segment_count == 0) {
    napi_throw_type_error(env, NULL, "segments must not be empty");
    return NULL;
  }

  root_fd = open(root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  if (root_fd < 0) return failure_result(env, errno, "native-io", NULL);
  if (fstat(root_fd, &root_stat) != 0) {
    int code = errno;
    close(root_fd);
    return failure_result(env, code, "native-io", NULL);
  }
  if (!S_ISDIR(root_stat.st_mode)) {
    close(root_fd);
    return failure_result(env, ENOTDIR, "native-io", NULL);
  }
  current_fd = root_fd;

  for (uint32_t index = 0; index + 1 < segment_count; index += 1) {
    napi_value segment_value;
    char segment[NAME_MAX + 1];
    if (napi_get_element(env, argv[1], index, &segment_value) != napi_ok) {
      napi_throw_error(env, NULL, "cannot inspect path segment");
      close_walk_fds(root_fd, current_fd);
      return NULL;
    }
    if (!read_segment(env, segment_value, segment)) { close_walk_fds(root_fd, current_fd); return NULL; }
    int next_fd = openat(current_fd, segment, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (next_fd < 0) {
      int code = errno;
      close_walk_fds(root_fd, current_fd);
      return failure_result(env, code, intermediate_failure_reason(code), &root_stat);
    }
    if (current_fd != root_fd) close(current_fd);
    current_fd = next_fd;
  }

  napi_value leaf_value;
  char leaf[NAME_MAX + 1];
  if (napi_get_element(env, argv[1], segment_count - 1, &leaf_value) != napi_ok) {
    napi_throw_error(env, NULL, "cannot inspect leaf segment");
    close_walk_fds(root_fd, current_fd);
    return NULL;
  }
  if (!read_segment(env, leaf_value, leaf)) { close_walk_fds(root_fd, current_fd); return NULL; }
  leaf_fd = openat(current_fd, leaf, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  if (leaf_fd < 0) {
    int code = errno;
    close_walk_fds(root_fd, current_fd);
    return failure_result(env, code, leaf_open_failure_reason(code), &root_stat);
  }
  if (fstat(leaf_fd, &leaf_stat) != 0) {
    int code = errno;
    close(leaf_fd);
    close_walk_fds(root_fd, current_fd);
    return failure_result(env, code, "native-io", &root_stat);
  }
  if (!S_ISREG(leaf_stat.st_mode)) {
    close(leaf_fd);
    close_walk_fds(root_fd, current_fd);
    return failure_result(env, EPERM, "leaf-not-regular", &root_stat);
  }
  if (leaf_stat.st_nlink != 1) {
    close(leaf_fd);
    close_walk_fds(root_fd, current_fd);
    return failure_result(env, EPERM, "leaf-multiple-links", &root_stat);
  }

  capacity = leaf_stat.st_size > 0 ? (size_t)leaf_stat.st_size : 1;
  bytes = malloc(capacity);
  if (bytes == NULL) { close(leaf_fd); close_walk_fds(root_fd, current_fd); return failure_result(env, ENOMEM, "native-io", &root_stat); }
  for (;;) {
    if (length == capacity) {
      size_t next_capacity = capacity > SIZE_MAX / 2 ? SIZE_MAX : capacity * 2;
      if (next_capacity <= capacity) { free(bytes); close(leaf_fd); close_walk_fds(root_fd, current_fd); return failure_result(env, EFBIG, "native-io", &root_stat); }
      unsigned char *grown = realloc(bytes, next_capacity);
      if (grown == NULL) { free(bytes); close(leaf_fd); close_walk_fds(root_fd, current_fd); return failure_result(env, ENOMEM, "native-io", &root_stat); }
      bytes = grown;
      capacity = next_capacity;
    }
    ssize_t read_count = read(leaf_fd, bytes + length, capacity - length);
    if (read_count < 0) { int code = errno; free(bytes); close(leaf_fd); close_walk_fds(root_fd, current_fd); return failure_result(env, code, "native-io", &root_stat); }
    if (read_count == 0) break;
    length += (size_t)read_count;
  }
  close(leaf_fd);
  close_walk_fds(root_fd, current_fd);

  NAPI_OK(napi_create_object(env, &result), "cannot create native result");
  NAPI_OK(napi_get_boolean(env, true, &value), "cannot encode native status");
  NAPI_OK(napi_set_named_property(env, result, "ok", value), "cannot set native status");
  NAPI_OK(napi_create_buffer_copy(env, length, bytes, NULL, &value), "cannot encode native bytes");
  free(bytes);
  NAPI_OK(napi_set_named_property(env, result, "bytes", value), "cannot set native bytes");
  NAPI_OK(napi_create_int64(env, (int64_t)(root_stat.st_mode & 0777), &value), "cannot encode root mode");
  NAPI_OK(napi_set_named_property(env, result, "rootMode", value), "cannot set root mode");
  NAPI_OK(napi_create_int64(env, (int64_t)(leaf_stat.st_mode & 0777), &value), "cannot encode leaf mode");
  NAPI_OK(napi_set_named_property(env, result, "leafMode", value), "cannot set leaf mode");
  NAPI_OK(napi_create_int64(env, (int64_t)leaf_stat.st_mode, &value), "cannot encode leaf stat mode");
  NAPI_OK(napi_set_named_property(env, result, "statMode", value), "cannot set leaf stat mode");
  char decimal[64];
  snprintf(decimal, sizeof(decimal), "%llu", (unsigned long long)root_stat.st_dev);
  if (set_string(env, result, "rootDevice", decimal) == NULL) return NULL;
  snprintf(decimal, sizeof(decimal), "%llu", (unsigned long long)root_stat.st_ino);
  if (set_string(env, result, "rootInode", decimal) == NULL) return NULL;
  return result;
}

typedef struct {
  int fd;
  int closed;
} parent_handle;

static const napi_type_tag PARENT_HANDLE_TAG = {
  0xe7b11a6b48bd4a93ULL,
  0x9df71c780398503dULL,
};

static napi_value rename_errno_result(napi_env env, int code, int committed, const char *message) {
  napi_value result, value;
  NAPI_OK(napi_create_object(env, &result), "cannot create native result");
  NAPI_OK(napi_create_int32(env, code, &value), "cannot encode native status");
  NAPI_OK(napi_set_named_property(env, result, "status", value), "cannot set native status");
  NAPI_OK(napi_get_boolean(env, committed, &value), "cannot encode commit state");
  NAPI_OK(napi_set_named_property(env, result, "committed", value), "cannot set commit state");
  NAPI_OK(napi_create_string_utf8(env, message, NAPI_AUTO_LENGTH, &value), "cannot encode native error");
  NAPI_OK(napi_set_named_property(env, result, "error", value), "cannot set native error");
  NAPI_OK(napi_create_string_utf8(env, PLATFORM_NAME, NAPI_AUTO_LENGTH, &value), "cannot encode platform");
  NAPI_OK(napi_set_named_property(env, result, "platform", value), "cannot set platform");
  return result;
}

static void finalize_parent(napi_env env, void *data, void *hint) {
  (void)env;
  (void)hint;
  parent_handle *handle = (parent_handle *)data;
  if (handle != NULL) {
    if (!handle->closed) close(handle->fd);
    free(handle);
  }
}

static int read_parent_segment(napi_env env, napi_value value, char output[NAME_MAX + 1]) {
  if (!read_string(env, value, output, NAME_MAX + 1, "sourceSegment and targetSegment must be bounded UTF-8 strings")) return 0;
  if (strcmp(output, ".") == 0 || strcmp(output, "..") == 0 || strchr(output, '/') != NULL || strchr(output, '\\') != NULL) {
    napi_throw_type_error(env, NULL, "sourceSegment and targetSegment must be single safe path segments");
    return 0;
  }
  return 1;
}

static int read_decimal_property(napi_env env, napi_value object, const char *name, uint64_t *output) {
  napi_value value;
  napi_valuetype type;
  char buffer[64];
  size_t length = 0;
  char *end = NULL;
  if (napi_get_named_property(env, object, name, &value) != napi_ok ||
      napi_typeof(env, value, &type) != napi_ok || type != napi_string ||
      napi_get_value_string_utf8(env, value, buffer, sizeof(buffer), &length) != napi_ok ||
      length == 0 || length >= sizeof(buffer)) {
    napi_throw_type_error(env, NULL, "expectedSourceIdentity device/inode must be decimal strings");
    return 0;
  }
  errno = 0;
  unsigned long long parsed = strtoull(buffer, &end, 10);
  if (errno != 0 || end == buffer || *end != '\0') {
    napi_throw_type_error(env, NULL, "expectedSourceIdentity contains an invalid decimal identity");
    return 0;
  }
  *output = (uint64_t)parsed;
  return 1;
}

static int read_directory_identity(napi_env env, napi_value object, uint64_t *device,
                                   uint64_t *inode, int32_t *mode) {
  napi_value value;
  if (!read_decimal_property(env, object, "device", device) ||
      !read_decimal_property(env, object, "inode", inode) ||
      napi_get_named_property(env, object, "mode", &value) != napi_ok ||
      napi_get_value_int32(env, value, mode) != napi_ok || *mode < 0 || *mode > 0777) {
    napi_throw_type_error(env, NULL, "expected directory identity requires device, inode, and mode");
    return 0;
  }
  return 1;
}

static int matches_directory_identity(const struct stat *actual, uint64_t device,
                                      uint64_t inode, int32_t mode) {
  return S_ISDIR(actual->st_mode) && (uint64_t)actual->st_dev == device &&
    (uint64_t)actual->st_ino == inode &&
    (actual->st_mode & 0777) == (mode_t)mode;
}

static napi_value rename_success_result(napi_env env) {
  napi_value result, value;
  NAPI_OK(napi_create_object(env, &result), "cannot create native result");
  NAPI_OK(napi_create_int32(env, 0, &value), "cannot encode native status");
  NAPI_OK(napi_set_named_property(env, result, "status", value), "cannot set native status");
  NAPI_OK(napi_get_boolean(env, true, &value), "cannot encode commit state");
  NAPI_OK(napi_set_named_property(env, result, "committed", value), "cannot set commit state");
  NAPI_OK(napi_create_string_utf8(env, PLATFORM_NAME, NAPI_AUTO_LENGTH, &value), "cannot encode platform");
  NAPI_OK(napi_set_named_property(env, result, "platform", value), "cannot set platform");
  return result;
}

static int unwrap_parent(napi_env env, napi_value value, parent_handle **output) {
  bool tagged = false;
  if (napi_check_object_type_tag(env, value, &PARENT_HANDLE_TAG, &tagged) != napi_ok || !tagged ||
      napi_unwrap(env, value, (void **)output) != napi_ok || *output == NULL || (*output)->closed) {
    napi_throw_type_error(env, NULL, "parentHandle must be an open handle created by this addon");
    return 0;
  }
  return 1;
}

static napi_value OpenParentDirectory(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1], result;
  char directory[PATH_MAX];
  if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok || argc != 1 ||
      !read_string(env, argv[0], directory, sizeof(directory), "openParentDirectory requires one canonical absolute path") ||
      directory[0] != '/') {
    napi_throw_type_error(env, NULL, "openParentDirectory requires one canonical absolute path");
    return NULL;
  }
  int fd = open(directory, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  if (fd < 0) return rename_errno_result(env, errno, 0, strerror(errno));
  parent_handle *handle = calloc(1, sizeof(*handle));
  if (handle == NULL) { close(fd); return throw_last(env, "cannot allocate parent directory handle"); }
  handle->fd = fd;
  if (napi_create_object(env, &result) != napi_ok) {
    close(fd); free(handle); return throw_last(env, "cannot create parent directory handle");
  }
  if (napi_type_tag_object(env, result, &PARENT_HANDLE_TAG) != napi_ok) {
    close(fd); free(handle); return throw_last(env, "cannot tag parent directory handle");
  }
  if (napi_wrap(env, result, handle, finalize_parent, NULL, NULL) != napi_ok) {
    close(fd); free(handle); return throw_last(env, "cannot create parent directory handle");
  }
  return result;
}

static napi_value CloseParentDirectory(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1], result;
  parent_handle *handle = NULL;
  NAPI_OK(napi_get_cb_info(env, info, &argc, argv, NULL, NULL), "cannot read arguments");
  if (argc != 1 || !unwrap_parent(env, argv[0], &handle)) return NULL;
  int fd = handle->fd;
  handle->closed = 1;
  handle->fd = -1;
  if (close(fd) != 0) return rename_errno_result(env, errno, 0, strerror(errno));
  NAPI_OK(napi_get_boolean(env, true, &result), "cannot encode close result");
  return result;
}

static napi_value RenameDirectoryNoReplace(napi_env env, napi_callback_info info) {
  size_t argc = 4;
  napi_value argv[4], value;
  parent_handle *parent = NULL;
  char source[NAME_MAX + 1], target[NAME_MAX + 1];
  uint64_t expected_device = 0, expected_inode = 0;
  int32_t expected_mode = 0;
  NAPI_OK(napi_get_cb_info(env, info, &argc, argv, NULL, NULL), "cannot read arguments");
  if (argc != 4 || !unwrap_parent(env, argv[0], &parent) || !read_parent_segment(env, argv[1], source) ||
      !read_parent_segment(env, argv[2], target)) return NULL;
  if (strcmp(source, target) == 0) return throw_last(env, "sourceSegment and targetSegment must differ");
  if (!read_decimal_property(env, argv[3], "device", &expected_device) ||
      !read_decimal_property(env, argv[3], "inode", &expected_inode) ||
      napi_get_named_property(env, argv[3], "mode", &value) != napi_ok ||
      napi_get_value_int32(env, value, &expected_mode) != napi_ok || expected_mode < 0 || expected_mode > 0777) {
    napi_throw_type_error(env, NULL, "expectedSourceIdentity requires device, inode, and mode");
    return NULL;
  }
  struct stat source_stat;
  if (fstatat(parent->fd, source, &source_stat, AT_SYMLINK_NOFOLLOW) != 0) return rename_errno_result(env, errno, 0, strerror(errno));
  if (!S_ISDIR(source_stat.st_mode)) return rename_errno_result(env, ENOTDIR, 0, "source is not a directory");
  if ((uint64_t)source_stat.st_dev != expected_device || (uint64_t)source_stat.st_ino != expected_inode ||
      (source_stat.st_mode & 0777) != (mode_t)expected_mode) return rename_errno_result(env, EPROTO, 0, "source identity changed before rename");
  int rc;
#if defined(__APPLE__)
  rc = renameatx_np(parent->fd, source, parent->fd, target, RENAME_EXCL);
#else
  rc = (int)syscall(SYS_renameat2, parent->fd, source, parent->fd, target, RENAME_NOREPLACE);
#endif
  if (rc != 0) return rename_errno_result(env, errno, 0, strerror(errno));
  struct stat source_after, target_after;
  errno = 0;
  if (fstatat(parent->fd, source, &source_after, AT_SYMLINK_NOFOLLOW) == 0 || errno != ENOENT) return rename_errno_result(env, EIO, 1, "source still exists after committed rename");
  if (fstatat(parent->fd, target, &target_after, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISDIR(target_after.st_mode) ||
      target_after.st_dev != source_stat.st_dev || target_after.st_ino != source_stat.st_ino ||
      (target_after.st_mode & 0777) != (source_stat.st_mode & 0777)) return rename_errno_result(env, EIO, 1, "target identity mismatch after committed rename");
  return rename_success_result(env);
}

static napi_value ExchangeDirectories(napi_env env, napi_callback_info info) {
  size_t argc = 6;
  napi_value argv[6];
  parent_handle *parent = NULL;
  char source[NAME_MAX + 1], target[NAME_MAX + 1];
  uint64_t parent_device = 0, parent_inode = 0;
  uint64_t source_device = 0, source_inode = 0;
  uint64_t target_device = 0, target_inode = 0;
  int32_t parent_mode = 0, source_mode = 0, target_mode = 0;
  NAPI_OK(napi_get_cb_info(env, info, &argc, argv, NULL, NULL), "cannot read arguments");
  if (argc != 6 || !unwrap_parent(env, argv[0], &parent) ||
      !read_parent_segment(env, argv[1], source) ||
      !read_parent_segment(env, argv[2], target)) return NULL;
  if (strcmp(source, target) == 0) return throw_last(env, "sourceSegment and targetSegment must differ");
  if (!read_directory_identity(env, argv[3], &parent_device, &parent_inode, &parent_mode) ||
      !read_directory_identity(env, argv[4], &source_device, &source_inode, &source_mode) ||
      !read_directory_identity(env, argv[5], &target_device, &target_inode, &target_mode)) return NULL;

  struct stat parent_stat, source_stat, target_stat;
  if (fstat(parent->fd, &parent_stat) != 0) return rename_errno_result(env, errno, 0, strerror(errno));
  if (!matches_directory_identity(&parent_stat, parent_device, parent_inode, parent_mode)) {
    return rename_errno_result(env, EPROTO, 0, "parent identity changed before exchange");
  }
  if (fstatat(parent->fd, source, &source_stat, AT_SYMLINK_NOFOLLOW) != 0) {
    return rename_errno_result(env, errno, 0, strerror(errno));
  }
  if (!matches_directory_identity(&source_stat, source_device, source_inode, source_mode)) {
    return rename_errno_result(env, EPROTO, 0, "source identity changed before exchange");
  }
  if (fstatat(parent->fd, target, &target_stat, AT_SYMLINK_NOFOLLOW) != 0) {
    return rename_errno_result(env, errno, 0, strerror(errno));
  }
  if (!matches_directory_identity(&target_stat, target_device, target_inode, target_mode)) {
    return rename_errno_result(env, EPROTO, 0, "target identity changed before exchange");
  }

  int rc;
#if defined(__APPLE__)
  rc = renameatx_np(parent->fd, source, parent->fd, target, RENAME_SWAP);
#else
  rc = (int)syscall(SYS_renameat2, parent->fd, source, parent->fd, target, RENAME_EXCHANGE);
#endif
  if (rc != 0) return rename_errno_result(env, errno, 0, strerror(errno));

  struct stat source_after, target_after;
  if (fstatat(parent->fd, source, &source_after, AT_SYMLINK_NOFOLLOW) != 0 ||
      !matches_directory_identity(&source_after, target_device, target_inode, target_mode)) {
    return rename_errno_result(env, EIO, 1, "source identity mismatch after committed exchange");
  }
  if (fstatat(parent->fd, target, &target_after, AT_SYMLINK_NOFOLLOW) != 0 ||
      !matches_directory_identity(&target_after, source_device, source_inode, source_mode)) {
    return rename_errno_result(env, EIO, 1, "target identity mismatch after committed exchange");
  }
  return rename_success_result(env);
}

/* A complete bound-tree observation is deliberately implemented beside the
 * existing descriptor-relative reader.  Every regular member is opened with
 * openat(O_NOFOLLOW), fstat'd on that descriptor, read from that descriptor,
 * and fstat'd again before the descriptor is closed.  JavaScript only receives
 * the bytes and facts from that one descriptor; it never supplements them
 * with path-based lstat calls. */
static int same_member_stat(const struct stat *before, const struct stat *after) {
  return before->st_dev == after->st_dev && before->st_ino == after->st_ino &&
    before->st_mode == after->st_mode && before->st_nlink == after->st_nlink &&
    before->st_size == after->st_size && before->st_mtime == after->st_mtime &&
    before->st_ctime == after->st_ctime;
}

static int set_int64(napi_env env, napi_value object, const char *name, int64_t value) {
  napi_value encoded;
  if (napi_create_int64(env, value, &encoded) != napi_ok ||
      napi_set_named_property(env, object, name, encoded) != napi_ok) {
    napi_throw_error(env, NULL, "cannot encode filesystem observation fact");
    return 0;
  }
  return 1;
}

static int set_member_string(napi_env env, napi_value object, const char *name, const char *value) {
  return set_string(env, object, name, value) != NULL;
}

static int append_directory_member(napi_env env, napi_value members, uint32_t *count,
                                   const char *relative, const struct stat *st) {
  napi_value member;
  if (napi_create_object(env, &member) != napi_ok) { napi_throw_error(env, NULL, "cannot create directory observation"); return 0; }
  if (!set_member_string(env, member, "path", relative) ||
      !set_member_string(env, member, "type", "directory") ||
      !set_int64(env, member, "statMode", (int64_t)st->st_mode)) return 0;
  if (napi_set_element(env, members, (*count)++, member) != napi_ok) { napi_throw_error(env, NULL, "cannot append directory observation"); return 0; }
  return 1;
}

static int append_file_member(napi_env env, napi_value members, uint32_t *count,
                              const char *relative, const struct stat *st,
                              const unsigned char *bytes, size_t length) {
  napi_value member, buffer;
  if (napi_create_object(env, &member) != napi_ok) { napi_throw_error(env, NULL, "cannot create file observation"); return 0; }
  if (!set_member_string(env, member, "path", relative) ||
      !set_member_string(env, member, "type", "file") ||
      !set_int64(env, member, "statMode", (int64_t)st->st_mode) ||
      !set_int64(env, member, "bytes", (int64_t)length)) return 0;
  if (napi_create_buffer_copy(env, length, bytes, NULL, &buffer) != napi_ok ||
      napi_set_named_property(env, member, "content", buffer) != napi_ok ||
      napi_set_element(env, members, (*count)++, member) != napi_ok) { napi_throw_error(env, NULL, "cannot append file observation"); return 0; }
  return 1;
}

static int observe_directory(napi_env env, int dir_fd, const char *prefix,
                             napi_value members, uint32_t *count) {
  int scan_fd = dup(dir_fd);
  if (scan_fd < 0) { napi_throw_error(env, NULL, "filesystem observation directory duplication failed"); return 0; }
  DIR *directory = fdopendir(scan_fd);
  if (directory == NULL) { close(scan_fd); napi_throw_error(env, NULL, "filesystem observation directory open failed"); return 0; }
  struct dirent *entry;
  while ((entry = readdir(directory)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
    char relative[PATH_MAX];
    int written = snprintf(relative, sizeof(relative), "%s%s%s", prefix,
                           prefix[0] == '\0' ? "" : "/", entry->d_name);
    if (written < 1 || (size_t)written >= sizeof(relative)) {
      closedir(directory); napi_throw_error(env, NULL, "filesystem observation path is too long"); return 0;
    }
    struct stat named_stat;
    if (fstatat(dir_fd, entry->d_name, &named_stat, AT_SYMLINK_NOFOLLOW) != 0 ||
        (!S_ISDIR(named_stat.st_mode) && !S_ISREG(named_stat.st_mode))) {
      closedir(directory); napi_throw_error(env, NULL, "filesystem observation encountered a symlink, special file, or changed member"); return 0;
    }
    int child_fd = openat(dir_fd, entry->d_name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (child_fd < 0) { closedir(directory); napi_throw_error(env, NULL, "filesystem observation member changed or cannot be opened"); return 0; }
    struct stat before;
    if (fstat(child_fd, &before) != 0) { close(child_fd); closedir(directory); napi_throw_error(env, NULL, "filesystem observation member cannot be stat'd"); return 0; }
    if (S_ISDIR(before.st_mode)) {
      if (!append_directory_member(env, members, count, relative, &before) ||
          !observe_directory(env, child_fd, relative, members, count)) {
        close(child_fd); closedir(directory); return 0;
      }
      close(child_fd);
      continue;
    }
    if (!S_ISREG(before.st_mode) || before.st_nlink != 1 || before.st_size < 0) {
      close(child_fd); closedir(directory); napi_throw_error(env, NULL, "filesystem observation encountered an unsupported member"); return 0;
    }
    size_t length = 0, capacity = before.st_size > 0 ? (size_t)before.st_size : 1;
    unsigned char *bytes = malloc(capacity);
    if (bytes == NULL) { close(child_fd); closedir(directory); napi_throw_error(env, NULL, "filesystem observation allocation failed"); return 0; }
    for (;;) {
      if (length == capacity) {
        if (capacity > SIZE_MAX / 2) { free(bytes); close(child_fd); closedir(directory); napi_throw_error(env, NULL, "filesystem observation file is too large"); return 0; }
        size_t next = capacity * 2; unsigned char *grown = realloc(bytes, next);
        if (grown == NULL) { free(bytes); close(child_fd); closedir(directory); napi_throw_error(env, NULL, "filesystem observation allocation failed"); return 0; }
        bytes = grown; capacity = next;
      }
      ssize_t read_count = read(child_fd, bytes + length, capacity - length);
      if (read_count < 0) { free(bytes); close(child_fd); closedir(directory); napi_throw_error(env, NULL, "filesystem observation read failed"); return 0; }
      if (read_count == 0) break;
      length += (size_t)read_count;
    }
    struct stat after;
    if (fstat(child_fd, &after) != 0 || !same_member_stat(&before, &after) || (size_t)after.st_size != length) {
      free(bytes); close(child_fd); closedir(directory); napi_throw_error(env, NULL, "filesystem observation member changed while being read"); return 0;
    }
    int ok = append_file_member(env, members, count, relative, &after, bytes, length);
    free(bytes); close(child_fd);
    if (!ok) { closedir(directory); return 0; }
  }
  if (closedir(directory) != 0) { napi_throw_error(env, NULL, "filesystem observation directory close failed"); return 0; }
  return 1;
}

static napi_value ObserveFilesystemTreeNative(napi_env env, napi_callback_info info) {
  size_t argc = 1; napi_value argv[1], result, members;
  char root[PATH_MAX];
  NAPI_OK(napi_get_cb_info(env, info, &argc, argv, NULL, NULL), "cannot read native arguments");
  if (argc != 1 || !read_string(env, argv[0], root, sizeof(root), "root must be a bounded absolute path") || root[0] != '/') return NULL;
  int root_fd = open(root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  if (root_fd < 0) return throw_last(env, "filesystem observation root cannot be opened");
  struct stat root_stat;
  if (fstat(root_fd, &root_stat) != 0 || !S_ISDIR(root_stat.st_mode)) { close(root_fd); return throw_last(env, "filesystem observation root is not a directory"); }
  NAPI_OK(napi_create_object(env, &result), "cannot create filesystem observation");
  NAPI_OK(napi_create_array(env, &members), "cannot create filesystem observation members");
  uint32_t count = 0;
  if (!observe_directory(env, root_fd, "", members, &count)) { close(root_fd); return NULL; }
  close(root_fd);
  NAPI_OK(napi_set_named_property(env, result, "members", members), "cannot set filesystem observation members");
  if (!set_int64(env, result, "rootMode", (int64_t)root_stat.st_mode)) return NULL;
  char decimal[64];
  snprintf(decimal, sizeof(decimal), "%llu", (unsigned long long)root_stat.st_dev);
  if (!set_member_string(env, result, "rootDevice", decimal)) return NULL;
  snprintf(decimal, sizeof(decimal), "%llu", (unsigned long long)root_stat.st_ino);
  if (!set_member_string(env, result, "rootInode", decimal)) return NULL;
  return result;
}

static napi_value Init(napi_env env, napi_value exports) {
  napi_value value;
  NAPI_OK(napi_create_function(env, "readFileBoundNative", NAPI_AUTO_LENGTH, ReadFileBoundNative, NULL, &value), "cannot export bound read");
  NAPI_OK(napi_set_named_property(env, exports, "readFileBoundNative", value), "cannot export bound read");
  NAPI_OK(napi_create_function(env, "observeFilesystemTreeNative", NAPI_AUTO_LENGTH, ObserveFilesystemTreeNative, NULL, &value), "cannot export filesystem observation");
  NAPI_OK(napi_set_named_property(env, exports, "observeFilesystemTreeNative", value), "cannot export filesystem observation");
  NAPI_OK(napi_create_function(env, "openParentDirectory", NAPI_AUTO_LENGTH, OpenParentDirectory, NULL, &value), "cannot export openParentDirectory");
  NAPI_OK(napi_set_named_property(env, exports, "openParentDirectory", value), "cannot export openParentDirectory");
  NAPI_OK(napi_create_function(env, "closeParentDirectory", NAPI_AUTO_LENGTH, CloseParentDirectory, NULL, &value), "cannot export closeParentDirectory");
  NAPI_OK(napi_set_named_property(env, exports, "closeParentDirectory", value), "cannot export closeParentDirectory");
  NAPI_OK(napi_create_function(env, "exchangeDirectories", NAPI_AUTO_LENGTH, ExchangeDirectories, NULL, &value), "cannot export exchangeDirectories");
  NAPI_OK(napi_set_named_property(env, exports, "exchangeDirectories", value), "cannot export exchangeDirectories");
  NAPI_OK(napi_create_function(env, "renameDirectoryNoReplace", NAPI_AUTO_LENGTH, RenameDirectoryNoReplace, NULL, &value), "cannot export renameDirectoryNoReplace");
  NAPI_OK(napi_set_named_property(env, exports, "renameDirectoryNoReplace", value), "cannot export renameDirectoryNoReplace");
  NAPI_OK(napi_create_string_utf8(env, PLATFORM_NAME, NAPI_AUTO_LENGTH, &value), "cannot encode platform");
  NAPI_OK(napi_set_named_property(env, exports, "platform", value), "cannot export platform");
  return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)
