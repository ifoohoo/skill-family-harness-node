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
#error "RENAME_EXCL is unavailable; this candidate must not use a guessed constant"
#endif
#define PLATFORM_NAME "darwin"
#elif defined(__linux__)
#include <linux/fs.h>
#include <sys/syscall.h>
#ifndef RENAME_NOREPLACE
#error "RENAME_NOREPLACE is unavailable; this candidate must not use a guessed constant"
#endif
#ifndef SYS_renameat2
#error "SYS_renameat2 is unavailable; this candidate cannot be built"
#endif
#define PLATFORM_NAME "linux"
#else
#error "renameDirectoryNoReplace is unsupported on this operating system"
#endif

#ifndef O_NOFOLLOW
#error "O_NOFOLLOW is required by the parent directory handle contract"
#endif
#ifndef O_DIRECTORY
#error "O_DIRECTORY is required by the parent directory handle contract"
#endif

typedef struct {
  int fd;
  int closed;
} parent_handle;

static const napi_type_tag PARENT_HANDLE_TAG = {
  0xe7b11a6b48bd4a93ULL,
  0x9df71c780398503dULL,
};

static napi_value throw_last(napi_env env, const char *message) {
  napi_throw_error(env, NULL, message);
  return NULL;
}

#define NAPI_OK(call, message) do { if ((call) != napi_ok) return throw_last(env, message); } while (0)

static void finalize_parent(napi_env env, void *data, void *hint) {
  (void)env;
  (void)hint;
  parent_handle *handle = (parent_handle *)data;
  if (handle != NULL) {
    if (!handle->closed) close(handle->fd);
    free(handle);
  }
}

static napi_value errno_result(napi_env env, int code, int committed, const char *message) {
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

static int read_segment(napi_env env, napi_value value, char output[NAME_MAX + 1]) {
  napi_valuetype type;
  size_t length = 0;
  if (napi_typeof(env, value, &type) != napi_ok || type != napi_string ||
      napi_get_value_string_utf8(env, value, NULL, 0, &length) != napi_ok ||
      length == 0 || length > NAME_MAX ||
      napi_get_value_string_utf8(env, value, output, NAME_MAX + 1, &length) != napi_ok) {
    napi_throw_type_error(env, NULL, "sourceSegment and targetSegment must be bounded UTF-8 strings");
    return 0;
  }
  if (strcmp(output, ".") == 0 || strcmp(output, "..") == 0 || strchr(output, '/') != NULL) {
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

static napi_value OpenParentDirectory(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1], result;
  napi_valuetype type;
  size_t length = 0;
  char path_buffer[PATH_MAX];
  NAPI_OK(napi_get_cb_info(env, info, &argc, argv, NULL, NULL), "cannot read arguments");
  if (argc != 1 || napi_typeof(env, argv[0], &type) != napi_ok || type != napi_string ||
      napi_get_value_string_utf8(env, argv[0], NULL, 0, &length) != napi_ok ||
      length == 0 || length >= sizeof(path_buffer) ||
      napi_get_value_string_utf8(env, argv[0], path_buffer, sizeof(path_buffer), &length) != napi_ok ||
      path_buffer[0] != '/') {
    napi_throw_type_error(env, NULL, "openParentDirectory requires one canonical absolute path");
    return NULL;
  }
  int fd = open(path_buffer, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  if (fd < 0) return errno_result(env, errno, 0, strerror(errno));
  parent_handle *handle = calloc(1, sizeof(*handle));
  if (handle == NULL) {
    close(fd);
    return throw_last(env, "cannot allocate parent directory handle");
  }
  handle->fd = fd;
  NAPI_OK(napi_create_object(env, &result), "cannot create parent directory handle");
  NAPI_OK(napi_wrap(env, result, handle, finalize_parent, NULL, NULL), "cannot wrap parent directory handle");
  NAPI_OK(napi_type_tag_object(env, result, &PARENT_HANDLE_TAG), "cannot tag parent directory handle");
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

static napi_value CloseParentDirectory(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1], result;
  parent_handle *handle = NULL;
  NAPI_OK(napi_get_cb_info(env, info, &argc, argv, NULL, NULL), "cannot read arguments");
  if (argc != 1 || !unwrap_parent(env, argv[0], &handle)) return NULL;
  if (close(handle->fd) != 0) return errno_result(env, errno, 0, strerror(errno));
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
  if (argc != 4) return throw_last(env, "renameDirectoryNoReplace requires exactly four arguments");
  if (!unwrap_parent(env, argv[0], &parent) || !read_segment(env, argv[1], source) ||
      !read_segment(env, argv[2], target)) return NULL;
  if (strcmp(source, target) == 0) return throw_last(env, "sourceSegment and targetSegment must differ");
  if (!read_decimal_property(env, argv[3], "device", &expected_device) ||
      !read_decimal_property(env, argv[3], "inode", &expected_inode) ||
      napi_get_named_property(env, argv[3], "mode", &value) != napi_ok ||
      napi_get_value_int32(env, value, &expected_mode) != napi_ok || expected_mode < 0 || expected_mode > 0777) {
    napi_throw_type_error(env, NULL, "expectedSourceIdentity requires device, inode, and mode");
    return NULL;
  }

  struct stat source_stat;
  if (fstatat(parent->fd, source, &source_stat, AT_SYMLINK_NOFOLLOW) != 0)
    return errno_result(env, errno, 0, strerror(errno));
  if (!S_ISDIR(source_stat.st_mode)) return errno_result(env, ENOTDIR, 0, "source is not a directory");
  if ((uint64_t)source_stat.st_dev != expected_device || (uint64_t)source_stat.st_ino != expected_inode ||
      (source_stat.st_mode & 0777) != (mode_t)expected_mode)
    return errno_result(env, EPROTO, 0, "source identity changed before rename");

  int rc;
#if defined(__APPLE__)
  rc = renameatx_np(parent->fd, source, parent->fd, target, RENAME_EXCL);
#else
  rc = (int)syscall(SYS_renameat2, parent->fd, source, parent->fd, target, RENAME_NOREPLACE);
#endif
  if (rc != 0) return errno_result(env, errno, 0, strerror(errno));

  struct stat source_after, target_after;
  errno = 0;
  if (fstatat(parent->fd, source, &source_after, AT_SYMLINK_NOFOLLOW) == 0 || errno != ENOENT)
    return errno_result(env, EIO, 1, "source still exists after committed rename");
  if (fstatat(parent->fd, target, &target_after, AT_SYMLINK_NOFOLLOW) != 0 ||
      !S_ISDIR(target_after.st_mode) || target_after.st_dev != source_stat.st_dev ||
      target_after.st_ino != source_stat.st_ino || (target_after.st_mode & 0777) != (source_stat.st_mode & 0777))
    return errno_result(env, EIO, 1, "target identity mismatch after committed rename");

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
