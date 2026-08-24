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
#include <unistd.h>

#if defined(__APPLE__)
#include <sys/stdio.h>
#ifndef RENAME_EXCL
#error "RENAME_EXCL is unavailable"
#endif
#define PLATFORM_NAME "darwin"
#elif defined(__linux__)
#include <linux/fs.h>
#include <sys/syscall.h>
#ifndef RENAME_NOREPLACE
#error "RENAME_NOREPLACE is unavailable"
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

static napi_value errno_result(napi_env env, int code) {
  napi_value result, value;
  NAPI_OK(napi_create_object(env, &result), "cannot create native result");
  NAPI_OK(napi_create_int32(env, code, &value), "cannot encode native error");
  NAPI_OK(napi_set_named_property(env, result, "errorCode", value), "cannot set native error");
  NAPI_OK(napi_get_boolean(env, false, &value), "cannot encode native status");
  NAPI_OK(napi_set_named_property(env, result, "ok", value), "cannot set native status");
  return result;
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
  napi_value argv[2], result, value, segments;
  char root[PATH_MAX];
  napi_valuetype type;
  int root_fd = -1;
  int current_fd = -1;
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
  if (root_fd < 0) return errno_result(env, errno);
  if (fstat(root_fd, &root_stat) != 0) { int code = errno; close(root_fd); return errno_result(env, code); }
  if (!S_ISDIR(root_stat.st_mode)) { close(root_fd); return errno_result(env, ENOTDIR); }
  current_fd = root_fd;

  for (uint32_t index = 0; index + 1 < segment_count; index += 1) {
    napi_value segment_value;
    char segment[NAME_MAX + 1];
    NAPI_OK(napi_get_element(env, argv[1], index, &segment_value), "cannot inspect path segment");
    if (!read_segment(env, segment_value, segment)) { close(current_fd); return NULL; }
    int next_fd = openat(current_fd, segment, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (next_fd < 0) { int code = errno; close(current_fd); return errno_result(env, code); }
    if (current_fd != root_fd) close(current_fd);
    current_fd = next_fd;
  }

  napi_value leaf_value;
  char leaf[NAME_MAX + 1];
  NAPI_OK(napi_get_element(env, argv[1], segment_count - 1, &leaf_value), "cannot inspect leaf segment");
  if (!read_segment(env, leaf_value, leaf)) { close(current_fd); return NULL; }
  int leaf_fd = openat(current_fd, leaf, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  if (leaf_fd < 0) { int code = errno; close(current_fd); return errno_result(env, code); }
  if (fstat(leaf_fd, &leaf_stat) != 0) { int code = errno; close(leaf_fd); close(current_fd); return errno_result(env, code); }
  if (!S_ISREG(leaf_stat.st_mode) || leaf_stat.st_nlink != 1) {
    close(leaf_fd); close(current_fd); return errno_result(env, EPERM);
  }

  capacity = leaf_stat.st_size > 0 ? (size_t)leaf_stat.st_size : 1;
  bytes = malloc(capacity);
  if (bytes == NULL) { close(leaf_fd); close(current_fd); return errno_result(env, ENOMEM); }
  for (;;) {
    if (length == capacity) {
      size_t next_capacity = capacity > SIZE_MAX / 2 ? SIZE_MAX : capacity * 2;
      if (next_capacity <= capacity) { free(bytes); close(leaf_fd); close(current_fd); return errno_result(env, EFBIG); }
      unsigned char *grown = realloc(bytes, next_capacity);
      if (grown == NULL) { free(bytes); close(leaf_fd); close(current_fd); return errno_result(env, ENOMEM); }
      bytes = grown;
      capacity = next_capacity;
    }
    ssize_t read_count = read(leaf_fd, bytes + length, capacity - length);
    if (read_count < 0) { int code = errno; free(bytes); close(leaf_fd); close(current_fd); return errno_result(env, code); }
    if (read_count == 0) break;
    length += (size_t)read_count;
  }
  close(leaf_fd);
  close(current_fd);

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
  if (napi_create_object(env, &result) != napi_ok || napi_wrap(env, result, handle, finalize_parent, NULL, NULL) != napi_ok ||
      napi_type_tag_object(env, result, &PARENT_HANDLE_TAG) != napi_ok) {
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
  if (close(handle->fd) != 0) return rename_errno_result(env, errno, 0, strerror(errno));
  handle->closed = 1;
  NAPI_OK(napi_get_boolean(env, true, &result), "cannot encode close result");
  return result;
}

static napi_value RenameDirectoryNoReplace(napi_env env, napi_callback_info info) {
  size_t argc = 4;
  napi_value argv[4], value, result;
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
  NAPI_OK(napi_create_object(env, &result), "cannot create native result");
  NAPI_OK(napi_create_int32(env, 0, &value), "cannot encode native status");
  NAPI_OK(napi_set_named_property(env, result, "status", value), "cannot set native status");
  NAPI_OK(napi_get_boolean(env, true, &value), "cannot encode commit state");
  NAPI_OK(napi_set_named_property(env, result, "committed", value), "cannot set commit state");
  NAPI_OK(napi_create_string_utf8(env, PLATFORM_NAME, NAPI_AUTO_LENGTH, &value), "cannot encode platform");
  NAPI_OK(napi_set_named_property(env, result, "platform", value), "cannot set platform");
  return result;
}

static napi_value Init(napi_env env, napi_value exports) {
  napi_value value;
  NAPI_OK(napi_create_function(env, "readFileBoundNative", NAPI_AUTO_LENGTH, ReadFileBoundNative, NULL, &value), "cannot export bound read");
  NAPI_OK(napi_set_named_property(env, exports, "readFileBoundNative", value), "cannot export bound read");
  NAPI_OK(napi_create_function(env, "openParentDirectory", NAPI_AUTO_LENGTH, OpenParentDirectory, NULL, &value), "cannot export openParentDirectory");
  NAPI_OK(napi_set_named_property(env, exports, "openParentDirectory", value), "cannot export openParentDirectory");
  NAPI_OK(napi_create_function(env, "closeParentDirectory", NAPI_AUTO_LENGTH, CloseParentDirectory, NULL, &value), "cannot export closeParentDirectory");
  NAPI_OK(napi_set_named_property(env, exports, "closeParentDirectory", value), "cannot export closeParentDirectory");
  NAPI_OK(napi_create_function(env, "renameDirectoryNoReplace", NAPI_AUTO_LENGTH, RenameDirectoryNoReplace, NULL, &value), "cannot export renameDirectoryNoReplace");
  NAPI_OK(napi_set_named_property(env, exports, "renameDirectoryNoReplace", value), "cannot export renameDirectoryNoReplace");
  NAPI_OK(napi_create_string_utf8(env, PLATFORM_NAME, NAPI_AUTO_LENGTH, &value), "cannot encode platform");
  NAPI_OK(napi_set_named_property(env, exports, "platform", value), "cannot export platform");
  return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)
