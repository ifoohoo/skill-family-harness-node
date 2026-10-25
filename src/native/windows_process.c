/*
 * Windows 11 x64 native backend for the existing harness entries.
 * Compiled only for OS==win. No TxF. Reads, publication, and job assignment
 * fail closed; they never report a POSIX mode or a successful job that was
 * not assigned before the process was resumed.
 */
#ifndef _WIN32
#error windows_process.c is compiled only for Windows
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <io.h>
#include <node_api.h>

#ifndef FILE_OPEN
#define FILE_OPEN 1
#endif
#ifndef FILE_CREATE
#define FILE_CREATE 2
#endif
#define FILE_OPEN_REPARSE_POINT 0x00200000
#define FILE_SYNCHRONOUS_IO_NONALERT 0x00000020
#define FILE_NON_DIRECTORY_FILE 0x00000040
#define FILE_DIRECTORY_FILE 0x00000001
#define NT_SUCCESS(status) ((LONG)(status) >= 0)
#define SF_STATUS_OBJECT_NAME_NOT_FOUND ((LONG)0xC0000034L)
#define SF_STATUS_OBJECT_PATH_NOT_FOUND ((LONG)0xC000003AL)
#define SF_STATUS_ACCESS_DENIED ((LONG)0xC0000022L)
#define SF_FSCTL_GET_REPARSE_POINT 0x000900A8u
#define SF_REPARSE_SYMLINK 0xA000000Cu
#define SF_REPARSE_BUFFER 16384u

typedef struct _SF_UNICODE_STRING {
  USHORT Length;
  USHORT MaximumLength;
  PWSTR Buffer;
} SF_UNICODE_STRING;

typedef struct _SF_IO_STATUS_BLOCK {
  union { LONG Status; PVOID Pointer; } StatusBlock;
  ULONG_PTR Information;
} SF_IO_STATUS_BLOCK;

typedef struct _SF_OBJECT_ATTRIBUTES {
  ULONG Length;
  HANDLE RootDirectory;
  SF_UNICODE_STRING *ObjectName;
  ULONG Attributes;
  PVOID SecurityDescriptor;
  PVOID SecurityQualityOfService;
} SF_OBJECT_ATTRIBUTES;

typedef LONG (NTAPI *SfNtCreateFile)(
  PHANDLE, ACCESS_MASK, SF_OBJECT_ATTRIBUTES *, SF_IO_STATUS_BLOCK *,
  PLARGE_INTEGER, ULONG, ULONG, ULONG, ULONG, PVOID, ULONG);

typedef struct { HANDLE handle; } WinHandle;
typedef struct { HANDLE job; int closed; } WindowsJob;

static SfNtCreateFile nt_create_file(void) {
  static SfNtCreateFile function = NULL;
  if (function == NULL) {
    function = (SfNtCreateFile) GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtCreateFile");
  }
  return function;
}

static void close_win_handle(napi_env env, void *data, void *hint) {
  (void) env;
  (void) hint;
  WinHandle *item = data;
  if (item != NULL && item->handle != NULL && item->handle != INVALID_HANDLE_VALUE) CloseHandle(item->handle);
  free(item);
}

static void close_job(napi_env env, void *data, void *hint) {
  (void) env;
  (void) hint;
  WindowsJob *job = data;
  if (job != NULL && job->job != NULL && !job->closed) CloseHandle(job->job);
  free(job);
}

static napi_value boolean_value(napi_env env, bool value) {
  napi_value result;
  napi_get_boolean(env, value, &result);
  return result;
}

static int set_string(napi_env env, napi_value object, const char *name, const char *value) {
  napi_value encoded;
  if (napi_create_string_utf8(env, value, NAPI_AUTO_LENGTH, &encoded) != napi_ok) return 0;
  return napi_set_named_property(env, object, name, encoded) == napi_ok;
}

static int set_bool(napi_env env, napi_value object, const char *name, bool value) {
  return napi_set_named_property(env, object, name, boolean_value(env, value)) == napi_ok;
}

static int set_uint(napi_env env, napi_value object, const char *name, uint32_t value) {
  napi_value encoded;
  if (napi_create_uint32(env, value, &encoded) != napi_ok) return 0;
  return napi_set_named_property(env, object, name, encoded) == napi_ok;
}

static napi_value failure_result(napi_env env, const char *reason) {
  DWORD error = GetLastError();
  napi_value result;
  napi_create_object(env, &result);
  set_bool(env, result, "ok", false);
  set_string(env, result, "failureReason", reason);
  set_uint(env, result, "nativeError", error);
  return result;
}

static int read_utf8(napi_env env, napi_value value, char *buffer, size_t size) {
  size_t length = 0;
  if (napi_get_value_string_utf8(env, value, NULL, 0, &length) != napi_ok || length + 1 > size) return 0;
  return napi_get_value_string_utf8(env, value, buffer, size, &length) == napi_ok;
}

static wchar_t *utf8_to_wide(const char *value) {
  int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, NULL, 0);
  if (count <= 0) return NULL;
  wchar_t *wide = calloc((size_t) count, sizeof(wchar_t));
  if (wide == NULL) return NULL;
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, wide, count) != count) {
    free(wide);
    return NULL;
  }
  return wide;
}

/* Only internally expand a caller's validated local drive path. */
static wchar_t *local_path_wide(const char *value) {
  wchar_t *plain = utf8_to_wide(value);
  if (plain == NULL) return NULL;
  size_t length = wcslen(plain);
  if (length < 3 || plain[1] != L':' || (plain[2] != L'/' && plain[2] != L'\\') || length > 32760) {
    free(plain);
    return NULL;
  }
  wchar_t *expanded = calloc(length + 5, sizeof(wchar_t));
  if (expanded == NULL) { free(plain); return NULL; }
  memcpy(expanded, L"\\\\?\\", 4 * sizeof(wchar_t));
  for (size_t index = 0; index <= length; index += 1) expanded[index + 4] = plain[index] == L'/' ? L'\\' : plain[index];
  free(plain);
  return expanded;
}

static int current_user_owns(PSECURITY_DESCRIPTOR descriptor) {
  PSID owner = NULL;
  BOOL defaulted = FALSE;
  HANDLE token = NULL;
  if (!GetSecurityDescriptorOwner(descriptor, &owner, &defaulted) || owner == NULL ||
      !OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return 0;
  DWORD length = 0;
  GetTokenInformation(token, TokenUser, NULL, 0, &length);
  TOKEN_USER *user = length == 0 ? NULL : malloc(length);
  int same = user != NULL && GetTokenInformation(token, TokenUser, user, length, &length) && EqualSid(owner, user->User.Sid);
  free(user);
  CloseHandle(token);
  return same;
}

static int hex_bytes(const unsigned char *bytes, size_t count, char *out, size_t out_size) {
  static const char digits[] = "0123456789abcdef";
  if (out_size < count * 2 + 1) return 0;
  for (size_t index = 0; index < count; index += 1) {
    out[index * 2] = digits[bytes[index] >> 4];
    out[index * 2 + 1] = digits[bytes[index] & 0x0f];
  }
  out[count * 2] = '\0';
  return 1;
}

static int is_device_segment(const wchar_t *segment) {
  wchar_t name[16];
  size_t length = 0;
  while (segment[length] != L'\0' && segment[length] != L'.' && length < 15) {
    wchar_t ch = segment[length];
    if (ch >= L'a' && ch <= L'z') ch = (wchar_t) (ch - 32);
    name[length++] = ch;
  }
  name[length] = L'\0';
  if (wcscmp(name, L"CON") == 0 || wcscmp(name, L"PRN") == 0 || wcscmp(name, L"AUX") == 0 || wcscmp(name, L"NUL") == 0) return 1;
  if (length == 4 && (wcsncmp(name, L"COM", 3) == 0 || wcsncmp(name, L"LPT", 3) == 0) && name[3] >= L'1' && name[3] <= L'9') return 1;
  return 0;
}

static const char *validate_segment(const wchar_t *segment) {
  size_t length = wcslen(segment);
  if (length == 0 || wcscmp(segment, L".") == 0 || wcscmp(segment, L"..") == 0) return "invalid-path";
  if (wcschr(segment, L':') != NULL || wcschr(segment, L'\\') != NULL || wcschr(segment, L'/') != NULL) return "invalid-path";
  wchar_t last = segment[length - 1];
  if (last == L'.' || last == L' ') return "invalid-path";
  if (is_device_segment(segment)) return "invalid-path";
  return NULL;
}

static int final_segment_matches(HANDLE handle, const wchar_t *segment);
static HANDLE open_relative(HANDLE parent, const wchar_t *segment, int directory, ACCESS_MASK extra, ULONG share, LONG *status_out);

static HANDLE open_root(const wchar_t *root) {
  if (root == NULL || wcslen(root) < 3 || root[1] != L':' || (root[2] != L'\\' && root[2] != L'/')) return INVALID_HANDLE_VALUE;
  wchar_t drive[] = L"\\\\?\\X:\\";
  drive[4] = root[0];
  HANDLE current = CreateFileW(drive, FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | READ_CONTROL | SYNCHRONIZE,
    FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
  if (current == INVALID_HANDLE_VALUE) return current;
  wchar_t *segments = _wcsdup(root + 3);
  if (segments == NULL) { CloseHandle(current); return INVALID_HANDLE_VALUE; }
  wchar_t *segment = segments;
  for (wchar_t *cursor = segments;; cursor += 1) {
    if (*cursor != L'\\' && *cursor != L'/' && *cursor != L'\0') continue;
    int last = *cursor == L'\0';
    *cursor = L'\0';
    if (*segment != L'\0') {
      if (validate_segment(segment) != NULL) { CloseHandle(current); current = INVALID_HANDLE_VALUE; break; }
      LONG status = 0;
      HANDLE next = open_relative(current, segment, 1, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, &status);
      FILE_ATTRIBUTE_TAG_INFO tag;
      int safe = next != INVALID_HANDLE_VALUE && GetFileInformationByHandleEx(next, FileAttributeTagInfo, &tag, sizeof(tag)) &&
        (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0 && final_segment_matches(next, segment);
      CloseHandle(current);
      current = next;
      if (!safe) { if (current != INVALID_HANDLE_VALUE) CloseHandle(current); current = INVALID_HANDLE_VALUE; break; }
    }
    if (last) break;
    segment = cursor + 1;
  }
  free(segments);
  return current;
}

static int nt_status_missing(LONG status) {
  return status == SF_STATUS_OBJECT_NAME_NOT_FOUND || status == SF_STATUS_OBJECT_PATH_NOT_FOUND;
}

static const char *win32_open_reason(DWORD error) {
  if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return "member-missing";
  if (error == ERROR_ACCESS_DENIED || error == ERROR_PRIVILEGE_NOT_HELD) return "access-denied";
  return "native-io";
}

static HANDLE open_relative(HANDLE parent, const wchar_t *segment, int directory, ACCESS_MASK extra, ULONG share, LONG *status_out) {
  SfNtCreateFile create = nt_create_file();
  if (status_out != NULL) *status_out = SF_STATUS_ACCESS_DENIED;
  if (create == NULL || parent == NULL || parent == INVALID_HANDLE_VALUE || segment == NULL) return INVALID_HANDLE_VALUE;
  size_t name_chars = wcslen(segment);
  if (name_chars == 0 || name_chars > 65535u / sizeof(wchar_t)) return INVALID_HANDLE_VALUE;
  size_t name_bytes = name_chars * sizeof(wchar_t);
  SF_UNICODE_STRING name;
  name.Length = (USHORT) name_bytes;
  name.MaximumLength = name.Length;
  name.Buffer = (PWSTR) segment;
  SF_OBJECT_ATTRIBUTES attributes;
  ZeroMemory(&attributes, sizeof(attributes));
  attributes.Length = sizeof(attributes);
  attributes.RootDirectory = parent;
  attributes.ObjectName = &name;
  attributes.Attributes = 0;
  SF_IO_STATUS_BLOCK status;
  ZeroMemory(&status, sizeof(status));
  HANDLE handle = NULL;
  ULONG options = FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT |
    (directory > 0 ? FILE_DIRECTORY_FILE : directory == 0 ? FILE_NON_DIRECTORY_FILE : 0);
  ACCESS_MASK access = READ_CONTROL | FILE_READ_ATTRIBUTES | SYNCHRONIZE | extra |
    (directory ? FILE_LIST_DIRECTORY : FILE_READ_DATA);
  LONG result = create(&handle, access, &attributes, &status, NULL, 0, share, FILE_OPEN, options, NULL, 0);
  if (status_out != NULL) *status_out = result;
  if (!NT_SUCCESS(result) || handle == NULL || handle == INVALID_HANDLE_VALUE) {
    if (handle != NULL && handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    typedef ULONG (WINAPI *StatusToError)(LONG);
    StatusToError convert = (StatusToError) GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlNtStatusToDosError");
    SetLastError(convert == NULL ? ERROR_GEN_FAILURE : convert(result));
    return INVALID_HANDLE_VALUE;
  }
  return handle;
}

static HANDLE open_local_path(const char *input, ACCESS_MASK extra, ULONG share) {
  if (input != NULL && strlen(input) == 3 && input[1] == ':' && (input[2] == '/' || input[2] == '\\')) {
    wchar_t *drive = local_path_wide(input);
    if (drive == NULL) return INVALID_HANDLE_VALUE;
    HANDLE handle = CreateFileW(drive, READ_CONTROL | FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY | SYNCHRONIZE | extra,
      share, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    free(drive);
    return handle;
  }
  wchar_t *plain = utf8_to_wide(input);
  if (plain == NULL) return INVALID_HANDLE_VALUE;
  for (wchar_t *cursor = plain; *cursor != L'\0'; cursor += 1) if (*cursor == L'/') *cursor = L'\\';
  wchar_t *slash = wcsrchr(plain, L'\\');
  if (slash == NULL || slash < plain + 2 || slash[1] == L'\0') { free(plain); return INVALID_HANDLE_VALUE; }
  wchar_t *leaf = _wcsdup(slash + 1);
  if (leaf == NULL || validate_segment(leaf) != NULL) { free(leaf); free(plain); return INVALID_HANDLE_VALUE; }
  if (slash == plain + 2) slash[1] = L'\0';
  else *slash = L'\0';
  HANDLE parent = open_root(plain);
  LONG status = 0;
  HANDLE file = parent == INVALID_HANDLE_VALUE ? INVALID_HANDLE_VALUE : open_relative(parent, leaf, -1, extra, share, &status);
  if (parent != INVALID_HANDLE_VALUE) CloseHandle(parent);
  free(leaf);
  free(plain);
  return file;
}

static int file_identity(HANDLE handle, char *volume, char *file_id, DWORD *attributes) {
  FILE_ID_INFO info;
  if (!GetFileInformationByHandleEx(handle, FileIdInfo, &info, sizeof(info))) return 0;
  unsigned char volume_bytes[8];
  unsigned long long serial = info.VolumeSerialNumber;
  for (int index = 7; index >= 0; index -= 1) {
    volume_bytes[index] = (unsigned char) (serial & 0xff);
    serial >>= 8;
  }
  if (!hex_bytes(volume_bytes, 8, volume, 17) || !hex_bytes(info.FileId.Identifier, 16, file_id, 33)) return 0;
  FILE_BASIC_INFO basic;
  if (!GetFileInformationByHandleEx(handle, FileBasicInfo, &basic, sizeof(basic))) return 0;
  *attributes = (DWORD) basic.FileAttributes;
  return 1;
}

static int copy_security(HANDLE handle, unsigned char **bytes, DWORD *size) {
  PSECURITY_DESCRIPTOR descriptor = NULL;
  DWORD result = GetSecurityInfo(handle, SE_FILE_OBJECT,
    OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
    NULL, NULL, NULL, NULL, &descriptor);
  if (result != ERROR_SUCCESS || descriptor == NULL) return 0;
  DWORD length = GetSecurityDescriptorLength(descriptor);
  unsigned char *copy = malloc(length);
  if (copy == NULL) { LocalFree(descriptor); return 0; }
  memcpy(copy, descriptor, length);
  LocalFree(descriptor);
  *bytes = copy;
  *size = length;
  return 1;
}

/* Creation with an explicit SD can clear SE_DACL_AUTO_INHERITED. Restore that
 * saved control through the security API on the still-empty, already protected
 * staging object, then require an exact observation before writing content. */
static int restore_created_security(HANDLE handle, PSECURITY_DESCRIPTOR descriptor) {
  SECURITY_DESCRIPTOR_CONTROL control = 0;
  DWORD revision = 0;
  if (!GetSecurityDescriptorControl(descriptor, &control, &revision)) return 0;
  SECURITY_INFORMATION information = OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION;
  if ((control & SE_DACL_AUTO_INHERITED) == 0) return SetKernelObjectSecurity(handle, information, descriptor) == TRUE;
  PSID owner = NULL, group = NULL;
  PACL dacl = NULL;
  BOOL defaulted = FALSE, present = FALSE;
  if (!GetSecurityDescriptorOwner(descriptor, &owner, &defaulted) ||
      !GetSecurityDescriptorGroup(descriptor, &group, &defaulted) ||
      !GetSecurityDescriptorDacl(descriptor, &present, &dacl, &defaulted) || !present) return 0;
  information |= (control & SE_DACL_PROTECTED) ? PROTECTED_DACL_SECURITY_INFORMATION : UNPROTECTED_DACL_SECURITY_INFORMATION;
  return SetSecurityInfo(handle, SE_FILE_OBJECT, information, owner, group, dacl, NULL) == ERROR_SUCCESS;
}

static int final_segment_matches(HANDLE handle, const wchar_t *segment) {
  DWORD length = GetFinalPathNameByHandleW(handle, NULL, 0, FILE_NAME_NORMALIZED);
  if (length == 0) return 0;
  wchar_t *path = calloc(length + 1, sizeof(wchar_t));
  if (path == NULL) return 0;
  if (GetFinalPathNameByHandleW(handle, path, length + 1, FILE_NAME_NORMALIZED) == 0) {
    free(path);
    return 0;
  }
  wchar_t *slash = wcsrchr(path, L'\\');
  const wchar_t *name = slash == NULL ? path : slash + 1;
  int matches = CompareStringOrdinal(name, -1, segment, -1, FALSE) == CSTR_EQUAL;
  free(path);
  return matches;
}

static napi_value external_handle(napi_env env, HANDLE handle) {
  WinHandle *item = calloc(1, sizeof(*item));
  napi_value external;
  if (item == NULL) { CloseHandle(handle); return NULL; }
  item->handle = handle;
  if (napi_create_external(env, item, close_win_handle, NULL, &external) != napi_ok) {
    CloseHandle(handle);
    free(item);
    return NULL;
  }
  return external;
}

static HANDLE unwrap_handle(napi_env env, napi_value value) {
  WinHandle *item = NULL;
  if (napi_get_value_external(env, value, (void **) &item) != napi_ok || item == NULL) return NULL;
  return item->handle;
}

static int schema_final_path(HANDLE handle, char *buffer, size_t size) {
  DWORD length = GetFinalPathNameByHandleW(handle, NULL, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
  if (length == 0 || length > 32767) return 0;
  wchar_t *path = calloc((size_t) length + 1, sizeof(wchar_t));
  if (path == NULL) return 0;
  if (GetFinalPathNameByHandleW(handle, path, length + 1, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS) == 0) {
    free(path);
    return 0;
  }
  const wchar_t *body = path;
  if (wcsncmp(path, L"\\\\?\\UNC\\", 8) == 0 || (wcsncmp(path, L"\\\\?\\", 4) != 0 && wcsncmp(path, L"\\\\", 2) == 0)) {
    free(path);
    return 0;
  }
  if (wcsncmp(path, L"\\\\?\\", 4) == 0) body = path + 4;
  size_t body_len = wcslen(body);
  if (body_len < 3 || body[1] != L':' || (body[2] != L'\\' && body[2] != L'/') || (body_len > 3 && (body[body_len - 1] == L'\\' || body[body_len - 1] == L'/'))) {
    free(path);
    return 0;
  }
  wchar_t *normalized = calloc(body_len + 1, sizeof(wchar_t));
  if (normalized == NULL) { free(path); return 0; }
  wchar_t drive = body[0];
  if (drive >= L'a' && drive <= L'z') drive = (wchar_t) (drive - 32);
  normalized[0] = drive;
  normalized[1] = L':';
  for (size_t index = 2; index < body_len; index += 1) {
    wchar_t ch = body[index] == L'\\' ? L'/' : body[index];
    if (ch == L'\0') { free(normalized); free(path); return 0; }
    normalized[index] = ch;
  }
  normalized[body_len] = L'\0';
  free(path);
  int written = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, normalized, -1, buffer, (int) size, NULL, NULL);
  free(normalized);
  return written > 1;
}

typedef struct {
  HANDLE handle;
  char volume[17];
  char file_id[33];
  DWORD attributes;
  unsigned char *security;
  DWORD security_size;
  LARGE_INTEGER last_write;
  LARGE_INTEGER change_time;
} BoundOpen;

static void release_bound(BoundOpen *nodes, uint32_t count) {
  if (nodes == NULL) return;
  for (uint32_t index = 0; index < count; index += 1) {
    if (nodes[index].handle != NULL && nodes[index].handle != INVALID_HANDLE_VALUE) CloseHandle(nodes[index].handle);
    free(nodes[index].security);
    nodes[index].handle = NULL;
    nodes[index].security = NULL;
  }
  free(nodes);
}

static int capture_bound(HANDLE handle, BoundOpen *node) {
  node->handle = handle;
  if (!file_identity(handle, node->volume, node->file_id, &node->attributes)) return 0;
  FILE_BASIC_INFO basic;
  if (!GetFileInformationByHandleEx(handle, FileBasicInfo, &basic, sizeof(basic))) return 0;
  node->last_write = basic.LastWriteTime;
  node->change_time = basic.ChangeTime;
  return copy_security(handle, &node->security, &node->security_size);
}

static int bound_still_same(const BoundOpen *node, int check_contents) {
  char volume[17], file_id[33];
  DWORD attributes = 0;
  unsigned char *security = NULL;
  DWORD size = 0;
  if (!file_identity(node->handle, volume, file_id, &attributes)) return 0;
  FILE_BASIC_INFO basic;
  if (check_contents && (!GetFileInformationByHandleEx(node->handle, FileBasicInfo, &basic, sizeof(basic)) ||
      basic.LastWriteTime.QuadPart != node->last_write.QuadPart || basic.ChangeTime.QuadPart != node->change_time.QuadPart)) return 0;
  if (strcmp(volume, node->volume) != 0 || strcmp(file_id, node->file_id) != 0 || attributes != node->attributes) return 0;
  if (!copy_security(node->handle, &security, &size)) return 0;
  int same = size == node->security_size && memcmp(security, node->security, size) == 0;
  free(security);
  return same;
}

static int set_buffer(napi_env env, napi_value object, const char *name, const void *data, size_t size) {
  napi_value buffer;
  const char empty = 0;
  if (napi_create_buffer_copy(env, size, size == 0 ? &empty : data, NULL, &buffer) != napi_ok) return 0;
  return napi_set_named_property(env, object, name, buffer) == napi_ok;
}

/* Failure disposition is useful only while the original bound handles still
 * prove the same root. Unknown or changed observations omit these facts. */
static napi_value bound_failure_result(napi_env env, const char *reason, const BoundOpen *nodes, uint32_t held, const char *actual) {
  napi_value result = failure_result(env, reason);
  if (actual != NULL) set_string(env, result, "actual", actual);
  int stable = nodes != NULL && held > 0;
  for (uint32_t index = 0; stable && index < held; index += 1) stable = bound_still_same(&nodes[index], 0);
  char canonical[32768];
  if (stable && schema_final_path(nodes[0].handle, canonical, sizeof(canonical))) {
    set_string(env, result, "rootVolumeSerial", nodes[0].volume);
    set_string(env, result, "rootFileId", nodes[0].file_id);
    set_string(env, result, "rootCanonicalPath", canonical);
    set_uint(env, result, "rootAttributes", nodes[0].attributes);
    set_buffer(env, result, "rootSecurityDescriptor", nodes[0].security, nodes[0].security_size);
  }
  return result;
}

static napi_value ReadFileBoundNative(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value args[2];
  napi_get_cb_info(env, info, &argc, args, NULL, NULL);
  if (argc < 2) return failure_result(env, "native-io");
  char root_utf8[32768];
  if (!read_utf8(env, args[0], root_utf8, sizeof(root_utf8))) return failure_result(env, "invalid-path");
  wchar_t *root = utf8_to_wide(root_utf8);
  if (root == NULL) return failure_result(env, "invalid-path");
  bool array = false;
  uint32_t count = 0;
  if (napi_is_array(env, args[1], &array) != napi_ok || !array || napi_get_array_length(env, args[1], &count) != napi_ok || count == 0 || count > 256) {
    free(root);
    return failure_result(env, "invalid-path");
  }
  HANDLE parent = open_root(root);
  free(root);
  if (parent == INVALID_HANDLE_VALUE) return failure_result(env, "invalid-root");
  BoundOpen *nodes = calloc((size_t) count + 1, sizeof(*nodes));
  if (nodes == NULL) { CloseHandle(parent); return failure_result(env, "native-io"); }
  uint32_t held = 0;
  if (!capture_bound(parent, &nodes[0])) {
    CloseHandle(parent);
    release_bound(nodes, 0);
    return failure_result(env, "native-io");
  }
  held = 1;
  if (nodes[0].attributes & FILE_ATTRIBUTE_REPARSE_POINT) {
    release_bound(nodes, held);
    return failure_result(env, "reparse");
  }
  const char *failure = NULL;
  const char *actual_type = NULL;
  for (uint32_t index = 0; index < count; index += 1) {
    napi_value item;
    char segment_utf8[1024];
    napi_get_element(env, args[1], index, &item);
    if (!read_utf8(env, item, segment_utf8, sizeof(segment_utf8))) { failure = "invalid-path"; break; }
    wchar_t *segment = utf8_to_wide(segment_utf8);
    if (segment == NULL) { failure = "invalid-path"; break; }
    const char *segment_failure = validate_segment(segment);
    if (segment_failure != NULL) { free(segment); failure = segment_failure; break; }
    int directory = index + 1 < count;
    LONG status = 0;
    HANDLE opened = open_relative(nodes[held - 1].handle, segment, directory, 0, FILE_SHARE_READ | (directory ? FILE_SHARE_WRITE : 0), &status);
    if (opened == INVALID_HANDLE_VALUE) {
      LONG other = 0;
      HANDLE probe = open_relative(nodes[held - 1].handle, segment, directory ? 0 : 1, 0, FILE_SHARE_READ | (!directory ? FILE_SHARE_WRITE : 0), &other);
      if (probe != INVALID_HANDLE_VALUE) {
        FILE_ATTRIBUTE_TAG_INFO tag;
        if (!directory && GetFileInformationByHandleEx(probe, FileAttributeTagInfo, &tag, sizeof(tag)) &&
            (tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
          actual_type = "directory";
        }
        CloseHandle(probe);
        free(segment);
        failure = directory ? "not-directory" : "not-regular";
        break;
      }
      free(segment);
      failure = nt_status_missing(status) && nt_status_missing(other) ? "member-missing" : "native-io";
      break;
    }
    if (!capture_bound(opened, &nodes[held])) {
      CloseHandle(opened);
      free(segment);
      failure = "native-io";
      break;
    }
    held += 1;
    BoundOpen *node = &nodes[held - 1];
    if (strcmp(node->volume, nodes[0].volume) != 0) { free(segment); failure = "cross-volume"; break; }
    if (node->attributes & FILE_ATTRIBUTE_REPARSE_POINT) { free(segment); failure = "reparse"; break; }
    if (!final_segment_matches(opened, segment)) { free(segment); failure = "case-alias"; break; }
    free(segment);
  }
  if (failure != NULL) {
    napi_value result = bound_failure_result(env, failure, nodes, held, actual_type);
    release_bound(nodes, held);
    return result;
  }
  BoundOpen *leaf = &nodes[held - 1];
  BY_HANDLE_FILE_INFORMATION ordinary;
  if (held != count + 1 || !GetFileInformationByHandle(leaf->handle, &ordinary) || ordinary.nNumberOfLinks != 1 ||
      (ordinary.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
    napi_value result = bound_failure_result(env, "not-regular", nodes, held, NULL);
    release_bound(nodes, held);
    return result;
  }
  LARGE_INTEGER size;
  if (!GetFileSizeEx(leaf->handle, &size) || size.QuadPart < 0) {
    release_bound(nodes, held);
    return failure_result(env, "native-io");
  }
  size_t length = (size_t) size.QuadPart;
  unsigned char *content = NULL;
  if (length > 0) {
    content = malloc(length);
    if (content == NULL) {
      release_bound(nodes, held);
      return failure_result(env, "native-io");
    }
  }
  size_t read_total = 0;
  while (read_total < length) {
    DWORD chunk = 0;
    DWORD request = (DWORD) (length - read_total > 1 << 20 ? 1 << 20 : length - read_total);
    if (!ReadFile(leaf->handle, content + read_total, request, &chunk, NULL) || chunk == 0 || read_total + chunk > length) {
      free(content);
      release_bound(nodes, held);
      return failure_result(env, "native-io");
    }
    read_total += chunk;
  }
  LARGE_INTEGER after;
  int stable = GetFileSizeEx(leaf->handle, &after) && after.QuadPart == size.QuadPart && read_total == length;
  for (uint32_t index = 0; stable && index < held; index += 1) stable = bound_still_same(&nodes[index], index + 1 == held);
  char canonical[32768];
  if (!stable || !schema_final_path(leaf->handle, canonical, sizeof(canonical))) {
    free(content);
    release_bound(nodes, held);
    return failure_result(env, "native-io");
  }
  napi_value result;
  napi_create_object(env, &result);
  set_bool(env, result, "ok", true);
  set_string(env, result, "volumeSerial", leaf->volume);
  set_string(env, result, "fileId", leaf->file_id);
  set_string(env, result, "rootVolumeSerial", nodes[0].volume);
  set_string(env, result, "rootFileId", nodes[0].file_id);
  set_string(env, result, "canonicalPath", canonical);
  set_uint(env, result, "attributes", leaf->attributes);
  set_uint(env, result, "rootAttributes", nodes[0].attributes);
  set_buffer(env, result, "bytes", content, read_total);
  set_buffer(env, result, "securityDescriptor", leaf->security, leaf->security_size);
  set_buffer(env, result, "rootSecurityDescriptor", nodes[0].security, nodes[0].security_size);
  free(content);
  release_bound(nodes, held);
  return result;
}

static int read_reparse_target(HANDLE handle, uint32_t *tag, unsigned char **target, DWORD *target_size) {
  unsigned char *buffer = malloc(SF_REPARSE_BUFFER);
  if (buffer == NULL) return 0;
  DWORD returned = 0;
  BOOL ok = DeviceIoControl(handle, SF_FSCTL_GET_REPARSE_POINT, NULL, 0, buffer, SF_REPARSE_BUFFER, &returned, NULL);
  if (!ok || returned < 8) { free(buffer); return 0; }
  uint32_t reparse_tag = 0;
  memcpy(&reparse_tag, buffer, sizeof(reparse_tag));
  *tag = reparse_tag;
  *target = NULL;
  *target_size = 0;
  if (reparse_tag == SF_REPARSE_SYMLINK && returned >= 20) {
    uint16_t print_offset = 0;
    uint16_t print_length = 0;
    memcpy(&print_offset, buffer + 12, sizeof(print_offset));
    memcpy(&print_length, buffer + 14, sizeof(print_length));
    size_t start = 20u + print_offset;
    if ((size_t) print_length > 0 && start + print_length <= returned) {
      unsigned char *copy = malloc(print_length);
      if (copy == NULL) { free(buffer); return 0; }
      memcpy(copy, buffer + start, print_length);
      *target = copy;
      *target_size = print_length;
    }
  }
  free(buffer);
  return 1;
}

static napi_value CaptureSecurity(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1];
  napi_get_cb_info(env, info, &argc, args, NULL, NULL);
  char path_utf8[32768];
  if (argc < 1 || !read_utf8(env, args[0], path_utf8, sizeof(path_utf8))) return failure_result(env, "invalid-path");
  /* Security observation does not read file bytes. Permit existing writers
   * (including directory flush handles in unrelated child operations), while
   * retaining the no-delete binding for this object's identity. */
  HANDLE handle = open_local_path(path_utf8, 0, FILE_SHARE_READ | FILE_SHARE_WRITE);
  DWORD open_error = handle == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
  if (handle == INVALID_HANDLE_VALUE) return failure_result(env, win32_open_reason(open_error));
  DWORD attributes = 0;
  char volume[17], file_id[33];
  unsigned char *security = NULL;
  DWORD security_size = 0;
  int ok = file_identity(handle, volume, file_id, &attributes) && copy_security(handle, &security, &security_size);
  char canonical[32768];
  int have_path = ok && schema_final_path(handle, canonical, sizeof(canonical));
  uint32_t reparse_tag = 0;
  unsigned char *reparse_target = NULL;
  DWORD reparse_target_size = 0;
  int reparse = ok && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
  int reparse_ok = !reparse || read_reparse_target(handle, &reparse_tag, &reparse_target, &reparse_target_size);
  wchar_t filesystem[32];
  int have_filesystem = GetVolumeInformationByHandleW(handle, NULL, 0, NULL, NULL, NULL, filesystem, 32);
  CloseHandle(handle);
  if (!ok || !have_path || !reparse_ok) {
    free(security);
    free(reparse_target);
    return failure_result(env, ok && reparse_ok ? "native-io" : "security-unreadable");
  }
  napi_value result, descriptor;
  napi_create_object(env, &result);
  set_bool(env, result, "ok", true);
  if (have_filesystem && _wcsicmp(filesystem, L"NTFS") == 0) set_string(env, result, "filesystem", "NTFS");
  else if (have_filesystem) set_string(env, result, "filesystem", "other");
  set_bool(env, result, "currentUserOwner", current_user_owns((PSECURITY_DESCRIPTOR) security) == 1);
  set_uint(env, result, "attributes", attributes);
  set_string(env, result, "volumeSerial", volume);
  set_string(env, result, "fileId", file_id);
  set_string(env, result, "canonicalPath", canonical);
  napi_create_buffer_copy(env, security_size, security, NULL, &descriptor);
  napi_set_named_property(env, result, "securityDescriptor", descriptor);
  if (reparse) {
    set_uint(env, result, "reparseTag", reparse_tag);
    set_buffer(env, result, "reparseTarget", reparse_target == NULL ? (const void *) "" : reparse_target, reparse_target_size);
  }
  free(security);
  free(reparse_target);
  return result;
}

static int read_handle_bytes(HANDLE handle, unsigned char **out, size_t *out_len);

static napi_value ReadCapturedFile(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value args[2];
  bool allow_double_link = false;
  napi_get_cb_info(env, info, &argc, args, NULL, NULL);
  if (argc >= 2) napi_get_value_bool(env, args[1], &allow_double_link);
  char path_utf8[32768];
  if (argc < 1 || !read_utf8(env, args[0], path_utf8, sizeof(path_utf8))) return failure_result(env, "invalid-path");
  HANDLE handle = open_local_path(path_utf8, 0, FILE_SHARE_READ);
  DWORD open_error = handle == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
  if (handle == INVALID_HANDLE_VALUE) return failure_result(env, win32_open_reason(open_error));
  DWORD attributes = 0;
  char volume[17], file_id[33];
  unsigned char *security = NULL;
  DWORD security_size = 0;
  BY_HANDLE_FILE_INFORMATION ordinary;
  int identified = file_identity(handle, volume, file_id, &attributes) && copy_security(handle, &security, &security_size) &&
    GetFileInformationByHandle(handle, &ordinary);
  if (!identified) {
    CloseHandle(handle);
    free(security);
    return failure_result(env, "security-unreadable");
  }
  if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) || (attributes & FILE_ATTRIBUTE_DIRECTORY) || (ordinary.nNumberOfLinks != 1 && !(allow_double_link && ordinary.nNumberOfLinks == 2))) {
    CloseHandle(handle);
    free(security);
    return failure_result(env, (attributes & FILE_ATTRIBUTE_REPARSE_POINT) ? "reparse" : "not-regular");
  }
  unsigned char *bytes = NULL;
  size_t byte_len = 0;
  char volume_after[17], file_after[33];
  DWORD attributes_after = 0;
  int stable = read_handle_bytes(handle, &bytes, &byte_len) &&
    file_identity(handle, volume_after, file_after, &attributes_after) &&
    strcmp(volume_after, volume) == 0 && strcmp(file_after, file_id) == 0 && attributes_after == attributes;
  CloseHandle(handle);
  if (!stable) {
    free(security);
    free(bytes);
    return failure_result(env, "native-io");
  }
  napi_value result;
  napi_create_object(env, &result);
  set_bool(env, result, "ok", true);
  set_uint(env, result, "numberOfLinks", ordinary.nNumberOfLinks);
  set_bool(env, result, "currentUserOwner", current_user_owns((PSECURITY_DESCRIPTOR) security) == 1);
  set_uint(env, result, "attributes", attributes);
  set_string(env, result, "volumeSerial", volume);
  set_string(env, result, "fileId", file_id);
  set_buffer(env, result, "bytes", bytes, byte_len);
  set_buffer(env, result, "securityDescriptor", security, security_size);
  free(security);
  free(bytes);
  return result;
}

static HANDLE create_new_relative(HANDLE parent, const wchar_t *leaf, PSECURITY_DESCRIPTOR descriptor, LONG *status_out, int directory);
static int flush_handle(HANDLE handle);
static void delete_open_file(HANDLE handle);
static int publish_temporary_name(wchar_t *temporary, size_t capacity, const wchar_t *target);

static napi_value WindowsRestoreCapable(napi_env env, napi_callback_info info) {
  size_t argc = 3;
  napi_value args[3];
  napi_get_cb_info(env, info, &argc, args, NULL, NULL);
  void *data = NULL;
  size_t length = 0;
  if (argc < 1 || napi_get_buffer_info(env, args[0], &data, &length) != napi_ok || data == NULL || length < 20 ||
      !IsValidSecurityDescriptor((PSECURITY_DESCRIPTOR) data)) {
    return failure_result(env, "not-restorable");
  }
  PSID owner = NULL;
  BOOL owner_default = FALSE;
  PSID group = NULL;
  BOOL group_default = FALSE;
  if (!GetSecurityDescriptorOwner((PSECURITY_DESCRIPTOR) data, &owner, &owner_default) || owner == NULL || !IsValidSid(owner)) {
    return failure_result(env, "not-restorable");
  }
  int has_group = GetSecurityDescriptorGroup((PSECURITY_DESCRIPTOR) data, &group, &group_default) && group != NULL && IsValidSid(group);
  HANDLE token = NULL;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return failure_result(env, "not-restorable");
  DWORD user_length = 0;
  GetTokenInformation(token, TokenUser, NULL, 0, &user_length);
  TOKEN_USER *user = user_length == 0 ? NULL : malloc(user_length);
  int owner_ok = user != NULL && GetTokenInformation(token, TokenUser, user, user_length, &user_length) && EqualSid(owner, user->User.Sid);
  int group_ok = !has_group || (owner_ok && EqualSid(group, user->User.Sid));
  if (has_group && owner_ok && !group_ok) {
    DWORD groups_length = 0;
    GetTokenInformation(token, TokenGroups, NULL, 0, &groups_length);
    TOKEN_GROUPS *groups = groups_length == 0 ? NULL : malloc(groups_length);
    if (groups != NULL && GetTokenInformation(token, TokenGroups, groups, groups_length, &groups_length)) {
      for (DWORD index = 0; index < groups->GroupCount; index += 1) {
        if (EqualSid(groups->Groups[index].Sid, group)) { group_ok = 1; break; }
      }
    }
    free(groups);
  }
  free(user);
  CloseHandle(token);
  if (!owner_ok || !group_ok) return failure_result(env, "not-restorable");
  if (argc < 3) return failure_result(env, "not-restorable");
  char target_utf8[32768];
  uint32_t promised = 0;
  if (!read_utf8(env, args[1], target_utf8, sizeof(target_utf8)) || napi_get_value_uint32(env, args[2], &promised) != napi_ok) return failure_result(env, "not-restorable");
  HANDLE existing = open_local_path(target_utf8, WRITE_DAC | WRITE_OWNER | FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
  if (existing == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND) return failure_result(env, "not-restorable");
  if (existing != INVALID_HANDLE_VALUE) CloseHandle(existing);
  char *slash = strrchr(target_utf8, '/');
  char *backslash = strrchr(target_utf8, '\\');
  if (backslash != NULL && (slash == NULL || backslash > slash)) slash = backslash;
  if (slash == NULL) return failure_result(env, "not-restorable");
  wchar_t temporary[32768];
  wchar_t *full_target = local_path_wide(target_utf8);
  if (full_target == NULL || !publish_temporary_name(temporary, 32768, full_target)) {
    free(full_target); return failure_result(env, "not-restorable");
  }
  free(full_target);
  const wchar_t *probe = wcsrchr(temporary, L'\\');
  if (probe == NULL) return failure_result(env, "not-restorable");
  probe += 1;
  *slash = '\0';
  HANDLE parent = open_local_path(target_utf8, FILE_GENERIC_READ | FILE_GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE);
  if (parent == INVALID_HANDLE_VALUE) return failure_result(env, "not-restorable");
  LONG create_status = 0;
  HANDLE staged = create_new_relative(parent, probe, (PSECURITY_DESCRIPTOR) data, &create_status, 0);
  if (staged == INVALID_HANDLE_VALUE) { CloseHandle(parent); return failure_result(env, "not-restorable"); }
  unsigned char *actual = NULL;
  DWORD actual_size = 0;
  FILE_BASIC_INFO basic;
  ZeroMemory(&basic, sizeof(basic));
  basic.FileAttributes = promised & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE);
  if (basic.FileAttributes == 0) basic.FileAttributes = FILE_ATTRIBUTE_NORMAL;
  int capable = restore_created_security(staged, (PSECURITY_DESCRIPTOR) data) &&
    copy_security(staged, &actual, &actual_size) && actual_size == length && memcmp(actual, data, length) == 0 &&
    flush_handle(staged) && SetFileInformationByHandle(staged, FileBasicInfo, &basic, sizeof(basic));
  free(actual);
  basic.FileAttributes = FILE_ATTRIBUTE_NORMAL;
  if (!SetFileInformationByHandle(staged, FileBasicInfo, &basic, sizeof(basic))) capable = 0;
  delete_open_file(staged);
  CloseHandle(staged);
  if (!flush_handle(parent)) capable = 0;
  CloseHandle(parent);
  if (!capable) return failure_result(env, "not-restorable");
  napi_value result;
  napi_create_object(env, &result);
  set_bool(env, result, "ok", true);
  return result;
}

static PSECURITY_DESCRIPTOR protected_descriptor(void) {
  HANDLE token = NULL;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return NULL;
  DWORD length = 0;
  GetTokenInformation(token, TokenUser, NULL, 0, &length);
  TOKEN_USER *user = length == 0 ? NULL : malloc(length);
  if (user == NULL || !GetTokenInformation(token, TokenUser, user, length, &length)) {
    free(user);
    CloseHandle(token);
    return NULL;
  }
  CloseHandle(token);
  SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
  PSID system = NULL;
  if (!AllocateAndInitializeSid(&authority, 1, SECURITY_LOCAL_SYSTEM_RID, 0, 0, 0, 0, 0, 0, 0, &system)) {
    free(user);
    return NULL;
  }
  EXPLICIT_ACCESSW entries[2];
  ZeroMemory(entries, sizeof(entries));
  for (int index = 0; index < 2; index += 1) {
    entries[index].grfAccessPermissions = FILE_ALL_ACCESS;
    entries[index].grfAccessMode = SET_ACCESS;
    entries[index].grfInheritance = NO_INHERITANCE;
    entries[index].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    entries[index].Trustee.TrusteeType = index == 0 ? TRUSTEE_IS_USER : TRUSTEE_IS_WELL_KNOWN_GROUP;
  }
  entries[0].Trustee.ptstrName = (LPWSTR) user->User.Sid;
  entries[1].Trustee.ptstrName = system;
  PACL dacl = NULL;
  if (SetEntriesInAclW(2, entries, NULL, &dacl) != ERROR_SUCCESS) {
    FreeSid(system);
    free(user);
    return NULL;
  }
  PSECURITY_DESCRIPTOR absolute = LocalAlloc(LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH);
  if (absolute == NULL || !InitializeSecurityDescriptor(absolute, SECURITY_DESCRIPTOR_REVISION) ||
      !SetSecurityDescriptorOwner(absolute, user->User.Sid, FALSE) ||
      !SetSecurityDescriptorDacl(absolute, TRUE, dacl, FALSE) ||
      !SetSecurityDescriptorControl(absolute, SE_DACL_PROTECTED, SE_DACL_PROTECTED)) {
    LocalFree(absolute);
    LocalFree(dacl);
    FreeSid(system);
    free(user);
    return NULL;
  }
  DWORD bytes = 0;
  MakeSelfRelativeSD(absolute, NULL, &bytes);
  PSECURITY_DESCRIPTOR relative = bytes == 0 ? NULL : LocalAlloc(LPTR, bytes);
  if (relative == NULL || !MakeSelfRelativeSD(absolute, relative, &bytes)) {
    LocalFree(relative);
    relative = NULL;
  }
  LocalFree(absolute);
  LocalFree(dacl);
  FreeSid(system);
  free(user);
  return relative;
}

static DWORD windows_flush_access(void) {
  return GENERIC_READ | GENERIC_WRITE;
}

/* Full temporary name or nothing. A truncated suffix must not become the final path. */
static int publish_temporary_name(wchar_t *temporary, size_t capacity, const wchar_t *target) {
  if (temporary == NULL || target == NULL || capacity < 2) return 0;
  const wchar_t *leaf = wcsrchr(target, L'\\');
  if (leaf == NULL || leaf[1] == L'\0') return 0;
  leaf += 1;
  size_t parent_length = (size_t) (leaf - target);
  unsigned char random[16];
  typedef BOOLEAN (WINAPI *RandomBytes)(PVOID, ULONG);
  RandomBytes random_bytes = (RandomBytes) GetProcAddress(GetModuleHandleW(L"advapi32.dll"), "SystemFunction036");
  char random_hex[33];
  if (random_bytes == NULL || !random_bytes(random, sizeof(random)) || !hex_bytes(random, sizeof(random), random_hex, sizeof(random_hex))) return 0;
  wchar_t suffix[64];
  int written = swprintf(suffix, 64, L".%lu.%hs.publish", (unsigned long) GetCurrentProcessId(), random_hex);
  size_t leaf_length = wcslen(leaf);
  if (written <= 0 || parent_length + 1 + leaf_length + (size_t) written + 1 > capacity) return 0;
  memcpy(temporary, target, parent_length * sizeof(wchar_t));
  temporary[parent_length] = L'.';
  memcpy(temporary + parent_length + 1, leaf, leaf_length * sizeof(wchar_t));
  memcpy(temporary + parent_length + 1 + leaf_length, suffix, ((size_t) written + 1) * sizeof(wchar_t));
  return 1;
}

static int flush_handle(HANDLE handle) {
  return FlushFileBuffers(handle) ? 1 : 0;
}

static void delete_open_file(HANDLE handle) {
  FILE_DISPOSITION_INFO info;
  info.DeleteFile = TRUE;
  SetFileInformationByHandle(handle, FileDispositionInfo, &info, sizeof(info));
}

static HANDLE create_new_relative(HANDLE parent, const wchar_t *leaf, PSECURITY_DESCRIPTOR descriptor, LONG *status_out, int directory) {
  SfNtCreateFile create = nt_create_file();
  if (status_out != NULL) *status_out = SF_STATUS_ACCESS_DENIED;
  if (create == NULL || parent == NULL || parent == INVALID_HANDLE_VALUE || leaf == NULL) return INVALID_HANDLE_VALUE;
  size_t name_chars = wcslen(leaf);
  if (name_chars == 0 || name_chars > 65535u / sizeof(wchar_t)) return INVALID_HANDLE_VALUE;
  SF_UNICODE_STRING name;
  name.Length = (USHORT) (name_chars * sizeof(wchar_t));
  name.MaximumLength = name.Length;
  name.Buffer = (PWSTR) leaf;
  SF_OBJECT_ATTRIBUTES attributes;
  ZeroMemory(&attributes, sizeof(attributes));
  attributes.Length = sizeof(attributes);
  attributes.RootDirectory = parent;
  attributes.ObjectName = &name;
  attributes.SecurityDescriptor = descriptor;
  SF_IO_STATUS_BLOCK status;
  ZeroMemory(&status, sizeof(status));
  HANDLE handle = NULL;
  LONG result = create(&handle, FILE_GENERIC_READ | FILE_GENERIC_WRITE | DELETE | SYNCHRONIZE | WRITE_DAC | WRITE_OWNER, &attributes, &status, NULL,
    directory ? 0 : FILE_ATTRIBUTE_NORMAL, 0, FILE_CREATE,
    FILE_SYNCHRONOUS_IO_NONALERT | (directory ? FILE_DIRECTORY_FILE : FILE_NON_DIRECTORY_FILE), NULL, 0);
  if (status_out != NULL) *status_out = result;
  if (!NT_SUCCESS(result) || handle == NULL || handle == INVALID_HANDLE_VALUE) {
    if (handle != NULL && handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    return INVALID_HANDLE_VALUE;
  }
  return handle;
}

static int rename_relative(HANDLE file, HANDLE parent, const wchar_t *leaf, int replace, DWORD *error_out) {
  size_t name_chars = wcslen(leaf);
  size_t bytes = sizeof(FILE_RENAME_INFO) + name_chars * sizeof(wchar_t);
  FILE_RENAME_INFO *info = calloc(1, bytes);
  if (info == NULL) {
    if (error_out != NULL) *error_out = ERROR_NOT_ENOUGH_MEMORY;
    return 0;
  }
  /* The caller holds its approved target with DELETE access and share-delete.
   * That open already rejects an external deny-delete or writable occupant.
   * Ex keeps our own approved read handle alive through the replacement. */
  *((ULONG *) info) = replace ? 0x00000003u : 0u;
  info->RootDirectory = parent;
  info->FileNameLength = (DWORD) (name_chars * sizeof(wchar_t));
  memcpy(info->FileName, leaf, name_chars * sizeof(wchar_t));
  typedef LONG (NTAPI *SetInformation)(HANDLE, SF_IO_STATUS_BLOCK *, PVOID, ULONG, int);
  typedef ULONG (WINAPI *StatusToError)(LONG);
  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  SetInformation set_information = (SetInformation) GetProcAddress(ntdll, "NtSetInformationFile");
  StatusToError to_error = (StatusToError) GetProcAddress(ntdll, "RtlNtStatusToDosError");
  SF_IO_STATUS_BLOCK status;
  ZeroMemory(&status, sizeof(status));
  LONG result = set_information == NULL ? SF_STATUS_ACCESS_DENIED : set_information(file, &status, info, (ULONG) bytes, replace ? 65 : 10);
  int moved = NT_SUCCESS(result);
  DWORD error = moved ? ERROR_SUCCESS : to_error == NULL ? ERROR_GEN_FAILURE : to_error(result);
  if (error_out != NULL) *error_out = error;
  SetLastError(error);
  free(info);
  return moved;
}

static int read_handle_bytes(HANDLE handle, unsigned char **out, size_t *out_len) {
  LARGE_INTEGER size;
  if (!GetFileSizeEx(handle, &size) || size.QuadPart < 0) return 0;
  if (SetFilePointer(handle, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR) return 0;
  size_t length = (size_t) size.QuadPart;
  unsigned char *content = length == 0 ? NULL : malloc(length);
  if (length > 0 && content == NULL) return 0;
  size_t total = 0;
  while (total < length) {
    DWORD chunk = 0;
    DWORD request = (DWORD) (length - total > 1 << 20 ? 1 << 20 : length - total);
    if (!ReadFile(handle, content + total, request, &chunk, NULL) || chunk == 0 || total + chunk > length) {
      free(content);
      return 0;
    }
    total += chunk;
  }
  *out = content;
  *out_len = total;
  return 1;
}

static int bound_fact(napi_env env, BoundOpen *node, napi_value fact) {
  return set_string(env, fact, "volumeSerial", node->volume) &&
    set_string(env, fact, "fileId", node->file_id) &&
    set_uint(env, fact, "attributes", node->attributes) &&
    set_buffer(env, fact, "securityDescriptor", node->security, node->security_size);
}

/* Enumerate the open directory itself, and keep each ancestor open until its
 * descendants and their security observations have been checked. No absolute
 * child path is reopened by this walk. */
static const char *walk_bound_tree(napi_env env, BoundOpen *directory, const char *prefix,
    napi_value members, uint32_t *count, unsigned int depth, int record_links) {
  if (depth > 256) return "capacity";
  unsigned char *buffer = malloc(65536);
  if (buffer == NULL) return "native-io";
  const char *failure = NULL;
  int restart = 1;
  while (failure == NULL) {
    if (!GetFileInformationByHandleEx(directory->handle,
        restart ? FileIdBothDirectoryRestartInfo : FileIdBothDirectoryInfo, buffer, 65536)) {
      if (GetLastError() != ERROR_NO_MORE_FILES) failure = "native-io";
      break;
    }
    restart = 0;
    FILE_ID_BOTH_DIR_INFO *entry = (FILE_ID_BOTH_DIR_INFO *) buffer;
    for (;;) {
      size_t chars = entry->FileNameLength / sizeof(wchar_t);
      wchar_t *name = calloc(chars + 1, sizeof(wchar_t));
      if (name == NULL) { failure = "native-io"; break; }
      memcpy(name, entry->FileName, entry->FileNameLength);
      int dot = wcscmp(name, L".") == 0 || wcscmp(name, L"..") == 0;
      if (!dot) {
        if (validate_segment(name) != NULL) { free(name); failure = "invalid-path"; break; }
        int name_bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, name, -1, NULL, 0, NULL, NULL);
        size_t prefix_length = strlen(prefix);
        char *relative = name_bytes <= 0 ? NULL : calloc(prefix_length + (size_t) name_bytes + 2, 1);
        if (relative == NULL) { free(name); failure = "native-io"; break; }
        memcpy(relative, prefix, prefix_length);
        if (prefix_length > 0) relative[prefix_length++] = '/';
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, name, -1, relative + prefix_length, name_bytes, NULL, NULL);
        LONG status = 0;
        int is_directory = (entry->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        HANDLE opened = open_relative(directory->handle, name, is_directory, 0, FILE_SHARE_READ, &status);
        BoundOpen node;
        ZeroMemory(&node, sizeof(node));
        if (opened == INVALID_HANDLE_VALUE) failure = "native-io";
        else if (!capture_bound(opened, &node)) failure = "security-unreadable";
        else if (strcmp(node.volume, directory->volume) != 0) failure = "cross-volume";
        else if (!final_segment_matches(opened, name)) failure = "case-alias";
        free(name);
        napi_value fact;
        napi_create_object(env, &fact);
        if (failure == NULL && (!bound_fact(env, &node, fact) || !set_string(env, fact, "path", relative))) failure = "native-io";
        if (failure == NULL && (node.attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
          uint32_t tag = 0;
          unsigned char *target = NULL;
          DWORD target_size = 0;
          if (!record_links || !read_reparse_target(opened, &tag, &target, &target_size) || tag != SF_REPARSE_SYMLINK) failure = "reparse";
          else {
            set_string(env, fact, "type", "symlink");
            set_uint(env, fact, "reparseTag", tag);
            set_buffer(env, fact, "reparseTarget", target, target_size);
          }
          free(target);
        } else if (failure == NULL && is_directory) {
          set_string(env, fact, "type", "directory");
          failure = walk_bound_tree(env, &node, relative, members, count, depth + 1, record_links);
        } else if (failure == NULL) {
          BY_HANDLE_FILE_INFORMATION ordinary;
          unsigned char *bytes = NULL;
          size_t length = 0;
          if (!GetFileInformationByHandle(opened, &ordinary) || ordinary.nNumberOfLinks != 1) failure = "not-regular";
          else if (!read_handle_bytes(opened, &bytes, &length)) failure = "native-io";
          else {
            set_string(env, fact, "type", "file");
            set_buffer(env, fact, "bytes", bytes, length);
          }
          free(bytes);
        }
        if (failure == NULL && !bound_still_same(&node, 1)) failure = "native-io";
        if (opened != INVALID_HANDLE_VALUE) CloseHandle(opened);
        free(node.security);
        free(relative);
        if (failure == NULL) {
          if (*count >= 20000) failure = "capacity";
          else napi_set_element(env, members, (*count)++, fact);
        }
      } else free(name);
      if (failure != NULL || entry->NextEntryOffset == 0) break;
      if ((unsigned char *) entry + entry->NextEntryOffset >= buffer + 65536) { failure = "native-io"; break; }
      entry = (FILE_ID_BOTH_DIR_INFO *) ((unsigned char *) entry + entry->NextEntryOffset);
    }
  }
  free(buffer);
  if (failure == NULL && !bound_still_same(directory, 1)) failure = "native-io";
  return failure;
}

static napi_value ObserveFilesystemTreeNative(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value args[2];
  napi_get_cb_info(env, info, &argc, args, NULL, NULL);
  char root_utf8[32768];
  bool record_links = false;
  if (argc < 1 || !read_utf8(env, args[0], root_utf8, sizeof(root_utf8))) return failure_result(env, "invalid-path");
  if (argc > 1) napi_get_value_bool(env, args[1], &record_links);
  wchar_t *root = utf8_to_wide(root_utf8);
  HANDLE opened = root == NULL ? INVALID_HANDLE_VALUE : open_root(root);
  free(root);
  if (opened == INVALID_HANDLE_VALUE) return failure_result(env, "invalid-root");
  BoundOpen node;
  ZeroMemory(&node, sizeof(node));
  if (!capture_bound(opened, &node) || (node.attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
    CloseHandle(opened); free(node.security); return failure_result(env, "invalid-root");
  }
  napi_value result, members, root_fact;
  napi_create_object(env, &result);
  napi_create_array(env, &members);
  napi_create_object(env, &root_fact);
  uint32_t count = 0;
  const char *failure = walk_bound_tree(env, &node, "", members, &count, 0, record_links);
  char canonical[32768];
  if (failure == NULL && (!bound_fact(env, &node, root_fact) || !schema_final_path(opened, canonical, sizeof(canonical)))) failure = "native-io";
  CloseHandle(opened);
  free(node.security);
  if (failure != NULL) return failure_result(env, failure);
  set_bool(env, result, "ok", true);
  set_string(env, root_fact, "canonicalPath", canonical);
  napi_set_named_property(env, result, "root", root_fact);
  napi_set_named_property(env, result, "members", members);
  return result;
}

static int protected_security_matches(PSECURITY_DESCRIPTOR actual, PSECURITY_DESCRIPTOR expected) {
  PSID actual_owner = NULL, expected_owner = NULL;
  PACL actual_acl = NULL, expected_acl = NULL;
  BOOL ignored = FALSE, actual_present = FALSE, expected_present = FALSE;
  SECURITY_DESCRIPTOR_CONTROL control = 0;
  DWORD revision = 0;
  return GetSecurityDescriptorOwner(actual, &actual_owner, &ignored) &&
    GetSecurityDescriptorOwner(expected, &expected_owner, &ignored) &&
    actual_owner != NULL && expected_owner != NULL && EqualSid(actual_owner, expected_owner) &&
    GetSecurityDescriptorDacl(actual, &actual_present, &actual_acl, &ignored) &&
    GetSecurityDescriptorDacl(expected, &expected_present, &expected_acl, &ignored) &&
    actual_present && expected_present && actual_acl != NULL && expected_acl != NULL &&
    actual_acl->AclSize == expected_acl->AclSize && memcmp(actual_acl, expected_acl, actual_acl->AclSize) == 0 &&
    GetSecurityDescriptorControl(actual, &control, &revision) && (control & SE_DACL_PROTECTED) != 0;
}

static napi_value PublishProtectedFile(napi_env env, napi_callback_info info) {
  size_t argc = 8;
  napi_value args[8];
  HANDLE approved_existing = INVALID_HANDLE_VALUE;
  napi_get_cb_info(env, info, &argc, args, NULL, NULL);
  char path_utf8[32768], kind[64];
  bool replace = false;
  if (argc < 4 || !read_utf8(env, args[0], path_utf8, sizeof(path_utf8)) || !read_utf8(env, args[2], kind, sizeof(kind)) ||
      napi_get_value_bool(env, args[3], &replace) != napi_ok) return failure_result(env, "invalid-path");
  void *data = NULL;
  size_t length = 0;
  if (napi_get_buffer_info(env, args[1], &data, &length) != napi_ok) return failure_result(env, "native-io");
  bool retain_handle = false;
  if (argc >= 8 && napi_get_value_bool(env, args[7], &retain_handle) != napi_ok) return failure_result(env, "invalid-path");
  wchar_t *target = local_path_wide(path_utf8);
  if (target == NULL) return failure_result(env, "invalid-path");
  int protected_kind = strcmp(kind, "windows-protected") == 0;
  int inherited_kind = strcmp(kind, "windows-inherited") == 0;
  int preserve_kind = strcmp(kind, "windows-preserve") == 0;
  int restore_kind = strcmp(kind, "windows-restore") == 0;
  uint32_t restore_attributes = 0;
  DWORD preserved_attributes = 0;
  if ((!protected_kind && !inherited_kind && !preserve_kind && !restore_kind) ||
      (retain_handle && (!protected_kind || replace || length != 0))) {
    free(target);
    return failure_result(env, "invalid-path");
  }
  wchar_t *slash = wcsrchr(target, L'\\');
  if (slash == NULL) slash = wcsrchr(target, L'/');
  wchar_t temporary[32768];
  if (slash == NULL || slash[1] == L'\0') {
    free(target);
    return failure_result(env, "invalid-path");
  }
  if (!publish_temporary_name(temporary, sizeof(temporary) / sizeof(temporary[0]), target)) {
    free(target);
    return failure_result(env, "invalid-path");
  }
  wchar_t *temp_slash = wcsrchr(temporary, L'\\');
  if (temp_slash == NULL) temp_slash = wcsrchr(temporary, L'/');
  const wchar_t *temp_leaf = temp_slash == NULL ? temporary : temp_slash + 1;
  *slash = L'\0';
  const wchar_t *leaf = slash + 1;
  char parent_utf8[32768];
  memcpy(parent_utf8, path_utf8, strlen(path_utf8) + 1);
  char *parent_slash = strrchr(parent_utf8, '/');
  char *parent_backslash = strrchr(parent_utf8, '\\');
  if (parent_backslash != NULL && (parent_slash == NULL || parent_backslash > parent_slash)) parent_slash = parent_backslash;
  if (parent_slash == NULL) { free(target); return failure_result(env, "invalid-path"); }
  if (parent_slash == parent_utf8 + 2) parent_slash[1] = '\0'; else *parent_slash = '\0';
  HANDLE parent = open_local_path(parent_utf8, windows_flush_access(), FILE_SHARE_READ | FILE_SHARE_WRITE);
  DWORD parent_error = parent == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
  if (parent == INVALID_HANDLE_VALUE) {
    free(target);
    return failure_result(env, win32_open_reason(parent_error));
  }
  char parent_volume[17], parent_file[33], pre_file[33], temp_file[33];
  DWORD parent_attributes = 0;
  if (!file_identity(parent, parent_volume, parent_file, &parent_attributes) || (parent_attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
    if (approved_existing != INVALID_HANDLE_VALUE) CloseHandle(approved_existing);
    CloseHandle(parent);
    free(target);
    return failure_result(env, (parent_attributes & FILE_ATTRIBUTE_REPARSE_POINT) ? "reparse" : "native-io");
  }
  if (argc >= 7) {
    napi_value expected_parent, expected_volume, expected_file, expected_security, expected_attributes;
    char volume[17], id[33];
    void *frozen_security = NULL;
    size_t frozen_security_size = 0;
    unsigned char *observed_security = NULL;
    DWORD observed_security_size = 0;
    uint32_t frozen_attributes = 0;
    if (napi_get_named_property(env, args[6], "parent", &expected_parent) != napi_ok ||
        napi_get_named_property(env, expected_parent, "volumeSerial", &expected_volume) != napi_ok ||
        napi_get_named_property(env, expected_parent, "fileId", &expected_file) != napi_ok ||
        !read_utf8(env, expected_volume, volume, sizeof(volume)) || !read_utf8(env, expected_file, id, sizeof(id)) ||
        strcmp(volume, parent_volume) != 0 || strcmp(id, parent_file) != 0 ||
        napi_get_named_property(env, expected_parent, "securityDescriptor", &expected_security) != napi_ok ||
        napi_get_buffer_info(env, expected_security, &frozen_security, &frozen_security_size) != napi_ok ||
        napi_get_named_property(env, expected_parent, "attributes", &expected_attributes) != napi_ok ||
        napi_get_value_uint32(env, expected_attributes, &frozen_attributes) != napi_ok || frozen_attributes != parent_attributes ||
        !copy_security(parent, &observed_security, &observed_security_size) || observed_security_size != frozen_security_size ||
        memcmp(observed_security, frozen_security, frozen_security_size) != 0) {
      free(observed_security);
      CloseHandle(parent); free(target); return failure_result(env, "parent-changed");
    }
    free(observed_security);
  }
  PSECURITY_DESCRIPTOR descriptor = NULL;
  int descriptor_local = 0;
  unsigned char *pre_security = NULL;
  DWORD pre_security_size = 0;
  int have_pre = 0;
  if (protected_kind) {
    descriptor = protected_descriptor();
    descriptor_local = 1;
    if (descriptor == NULL) { if (approved_existing != INVALID_HANDLE_VALUE) CloseHandle(approved_existing);
    CloseHandle(parent); free(target); return failure_result(env, "security-unreadable"); }
  } else if (preserve_kind || replace) {
    LONG existing_status = 0;
    HANDLE existing = open_relative(parent, leaf, 0, DELETE, FILE_SHARE_READ | FILE_SHARE_DELETE, &existing_status);
    if (existing == INVALID_HANDLE_VALUE) {
      if (approved_existing != INVALID_HANDLE_VALUE) CloseHandle(approved_existing);
    CloseHandle(parent);
      free(target);
      return failure_result(env, nt_status_missing(existing_status) ? "member-missing" : "target-open-failed");
    }
    DWORD pre_attributes = 0;
    BY_HANDLE_FILE_INFORMATION original_file;
    char pre_volume[17];
    int captured = file_identity(existing, pre_volume, pre_file, &pre_attributes) &&
      GetFileInformationByHandle(existing, &original_file) && original_file.nNumberOfLinks == 1 &&
      (pre_attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) == 0 &&
      copy_security(existing, &pre_security, &pre_security_size);
    approved_existing = existing;
    preserved_attributes = pre_attributes;
    if (!captured || strcmp(pre_volume, parent_volume) != 0) {
      free(pre_security);
      if (approved_existing != INVALID_HANDLE_VALUE) CloseHandle(approved_existing);
    CloseHandle(parent);
      free(target);
      return failure_result(env, "security-unreadable");
    }
    descriptor = (PSECURITY_DESCRIPTOR) pre_security;
    have_pre = 1;
  }
  if (restore_kind) {
    void *supplied = NULL;
    size_t supplied_size = 0;
    if (argc < 6 || napi_get_buffer_info(env, args[4], &supplied, &supplied_size) != napi_ok || supplied == NULL ||
        supplied_size < 20 || supplied_size > 65536 || napi_get_value_uint32(env, args[5], &restore_attributes) != napi_ok) {
      if (approved_existing != INVALID_HANDLE_VALUE) CloseHandle(approved_existing);
    CloseHandle(parent);
      free(target);
      return failure_result(env, "not-restorable");
    }
    free(pre_security);
    pre_security = malloc(supplied_size);
    if (pre_security == NULL) {
      if (approved_existing != INVALID_HANDLE_VALUE) CloseHandle(approved_existing);
    CloseHandle(parent);
      free(target);
      return failure_result(env, "native-io");
    }
    memcpy(pre_security, supplied, supplied_size);
    pre_security_size = (DWORD) supplied_size;
    descriptor = (PSECURITY_DESCRIPTOR) pre_security;
  }
  int have_pre_id = have_pre;
  if (replace && !have_pre_id) {
    LONG existing_status = 0;
    HANDLE existing = open_relative(parent, leaf, 0, DELETE, FILE_SHARE_READ | FILE_SHARE_DELETE, &existing_status);
    if (existing == INVALID_HANDLE_VALUE) {
      if (descriptor_local) LocalFree(descriptor);
      if (approved_existing != INVALID_HANDLE_VALUE) CloseHandle(approved_existing);
    CloseHandle(parent);
      free(target);
      return failure_result(env, nt_status_missing(existing_status) ? "member-missing" : "target-open-failed");
    }
    DWORD pre_attributes = 0;
    BY_HANDLE_FILE_INFORMATION original_file;
    char pre_volume[17];
    int captured = file_identity(existing, pre_volume, pre_file, &pre_attributes) &&
      GetFileInformationByHandle(existing, &original_file) && original_file.nNumberOfLinks == 1 &&
      (pre_attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) == 0;
    approved_existing = existing;
    preserved_attributes = pre_attributes;
    if (!captured || strcmp(pre_volume, parent_volume) != 0) {
      if (descriptor_local) LocalFree(descriptor);
      if (approved_existing != INVALID_HANDLE_VALUE) CloseHandle(approved_existing);
    CloseHandle(parent);
      free(target);
      return failure_result(env, "native-io");
    }
    have_pre_id = 1;
  }
  if (argc >= 7 && have_pre_id) {
    napi_value expected_target, expected_volume, expected_file, expected_bytes, expected_security, expected_attributes;
    char volume[17], id[33];
    void *frozen = NULL;
    size_t frozen_length = 0, observed_length = 0;
    unsigned char *observed = NULL, *observed_security = NULL;
    DWORD observed_security_size = 0;
    void *frozen_security = NULL;
    size_t frozen_security_size = 0;
    uint32_t frozen_attributes = 0;
    int matches = napi_get_named_property(env, args[6], "target", &expected_target) == napi_ok &&
      napi_get_named_property(env, expected_target, "volumeSerial", &expected_volume) == napi_ok &&
      napi_get_named_property(env, expected_target, "fileId", &expected_file) == napi_ok &&
      napi_get_named_property(env, expected_target, "bytes", &expected_bytes) == napi_ok &&
      read_utf8(env, expected_volume, volume, sizeof(volume)) && read_utf8(env, expected_file, id, sizeof(id)) &&
      strcmp(volume, parent_volume) == 0 && strcmp(id, pre_file) == 0 &&
      napi_get_buffer_info(env, expected_bytes, &frozen, &frozen_length) == napi_ok &&
      read_handle_bytes(approved_existing, &observed, &observed_length) && observed_length == frozen_length &&
      (frozen_length == 0 || memcmp(observed, frozen, frozen_length) == 0) &&
      napi_get_named_property(env, expected_target, "securityDescriptor", &expected_security) == napi_ok &&
      napi_get_buffer_info(env, expected_security, &frozen_security, &frozen_security_size) == napi_ok &&
      napi_get_named_property(env, expected_target, "attributes", &expected_attributes) == napi_ok &&
      napi_get_value_uint32(env, expected_attributes, &frozen_attributes) == napi_ok && frozen_attributes == preserved_attributes &&
      copy_security(approved_existing, &observed_security, &observed_security_size) && observed_security_size == frozen_security_size &&
      memcmp(observed_security, frozen_security, frozen_security_size) == 0;
    free(observed_security);
    free(observed);
    if (!matches) {
      if (approved_existing != INVALID_HANDLE_VALUE) CloseHandle(approved_existing);
      if (descriptor_local) LocalFree(descriptor);
      free(pre_security); CloseHandle(parent); free(target); return failure_result(env, "target-changed");
    }
  }
  /* Same-parent temp, flush, then one handle-relative rename. A crash before rename leaves the final name absent. */
  LONG create_status = 0;
  HANDLE handle = create_new_relative(parent, temp_leaf, descriptor, &create_status, 0);
  if (handle == INVALID_HANDLE_VALUE) {
    if (descriptor_local) LocalFree(descriptor);
    free(pre_security);
    if (approved_existing != INVALID_HANDLE_VALUE) CloseHandle(approved_existing);
    CloseHandle(parent);
    free(target);
    napi_value result = failure_result(env, "native-io");
    set_uint(env, result, "nativeStatus", (uint32_t) create_status);
    return result;
  }
  unsigned char *initial_security = NULL;
  DWORD initial_security_size = 0;
  int protection_ready = 1;
  if (pre_security != NULL) {
    protection_ready = restore_created_security(handle, (PSECURITY_DESCRIPTOR) pre_security);
  }
  protection_ready = protection_ready && copy_security(handle, &initial_security, &initial_security_size);
  if (protection_ready && pre_security != NULL) protection_ready = initial_security_size == pre_security_size && memcmp(initial_security, pre_security, pre_security_size) == 0;
  if (protection_ready && protected_kind) protection_ready = protected_security_matches((PSECURITY_DESCRIPTOR) initial_security, descriptor);
  if (!protection_ready) {
    napi_value rejected = failure_result(env, "security-not-preserved");
    if (initial_security != NULL) set_buffer(env, rejected, "securityDescriptor", initial_security, initial_security_size);
    if (pre_security != NULL) set_buffer(env, rejected, "expectedSecurityDescriptor", pre_security, pre_security_size);
    free(initial_security);
    delete_open_file(handle); CloseHandle(handle);
    if (approved_existing != INVALID_HANDLE_VALUE) CloseHandle(approved_existing);
    if (descriptor_local) LocalFree(descriptor);
    free(pre_security); CloseHandle(parent); free(target); return rejected;
  }
  size_t written_total = 0;
  int write_ok = 1;
  while (written_total < length) {
    DWORD chunk = 0;
    if (!WriteFile(handle, (const unsigned char *) data + written_total,
        (DWORD) (length - written_total > 1 << 20 ? 1 << 20 : length - written_total), &chunk, NULL) || chunk == 0) {
      write_ok = 0;
      break;
    }
    written_total += chunk;
  }
  char temp_volume[17];
  DWORD temp_attributes = 0;
  if (write_ok && !flush_handle(handle)) write_ok = 0;
  if (write_ok && (restore_kind || have_pre)) {
    FILE_BASIC_INFO basic;
    ZeroMemory(&basic, sizeof(basic));
    DWORD promised = (restore_kind ? restore_attributes : preserved_attributes) & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE);
    if (!GetFileInformationByHandleEx(handle, FileBasicInfo, &basic, sizeof(basic))) write_ok = 0;
    else {
      basic.FileAttributes = (basic.FileAttributes & ~(FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE)) | promised;
      if (basic.FileAttributes == 0) basic.FileAttributes = FILE_ATTRIBUTE_NORMAL;
      if (!SetFileInformationByHandle(handle, FileBasicInfo, &basic, sizeof(basic))) write_ok = 0;
    }
  }
  int flushed = write_ok && flush_handle(handle) && file_identity(handle, temp_volume, temp_file, &temp_attributes);
  if (!flushed || strcmp(temp_volume, parent_volume) != 0) {
    delete_open_file(handle);
    CloseHandle(handle);
    if (descriptor_local) LocalFree(descriptor);
    free(initial_security);
    free(pre_security);
    if (approved_existing != INVALID_HANDLE_VALUE) CloseHandle(approved_existing);
    CloseHandle(parent);
    free(target);
    return failure_result(env, "flush-failed");
  }
  if (have_pre_id) {
    LONG confirm_status = 0;
    HANDLE confirm = open_relative(parent, leaf, 0, 0, FILE_SHARE_READ | FILE_SHARE_DELETE, &confirm_status);
    char confirm_volume[17], confirm_file[33];
    DWORD confirm_attributes = 0;
    int same = confirm != INVALID_HANDLE_VALUE && file_identity(confirm, confirm_volume, confirm_file, &confirm_attributes) &&
      strcmp(confirm_file, pre_file) == 0 && strcmp(confirm_volume, parent_volume) == 0;
    if (confirm != INVALID_HANDLE_VALUE) CloseHandle(confirm);
    char parent_volume_now[17], parent_file_now[33];
    DWORD parent_attributes_now = 0;
    same = same && file_identity(parent, parent_volume_now, parent_file_now, &parent_attributes_now) &&
      strcmp(parent_file_now, parent_file) == 0;
    if (!same) {
      delete_open_file(handle);
      CloseHandle(handle);
      if (descriptor_local) LocalFree(descriptor);
      free(initial_security);
    free(pre_security);
      if (approved_existing != INVALID_HANDLE_VALUE) CloseHandle(approved_existing);
    CloseHandle(parent);
      free(target);
      return failure_result(env, "target-confirm-failed");
    }
  }
  DWORD rename_error = ERROR_SUCCESS;
  if (!rename_relative(handle, parent, leaf, replace ? 1 : 0, &rename_error)) {
    delete_open_file(handle);
    CloseHandle(handle);
    if (descriptor_local) LocalFree(descriptor);
    free(initial_security);
    free(pre_security);
    if (approved_existing != INVALID_HANDLE_VALUE) CloseHandle(approved_existing);
    CloseHandle(parent);
    free(target);
    napi_value failure = failure_result(env, !replace && (rename_error == ERROR_ALREADY_EXISTS || rename_error == ERROR_FILE_EXISTS) ? "exists" : "rename-failed");
    set_uint(env, failure, "nativeError", rename_error);
    return failure;
  }
  char post_volume[17], post_file[33], parent_volume_after[17], parent_file_after[33];
  DWORD post_attributes = 0, parent_attributes_after = 0;
  unsigned char *post_security = NULL;
  DWORD post_security_size = 0;
  unsigned char *reread = NULL;
  size_t reread_len = 0;
  int published = file_identity(handle, post_volume, post_file, &post_attributes) &&
    strcmp(post_file, temp_file) == 0 && strcmp(post_volume, parent_volume) == 0 &&
    final_segment_matches(handle, leaf) &&
    file_identity(parent, parent_volume_after, parent_file_after, &parent_attributes_after) &&
    strcmp(parent_file_after, parent_file) == 0 &&
    copy_security(handle, &post_security, &post_security_size) &&
    post_security_size == initial_security_size && memcmp(post_security, initial_security, initial_security_size) == 0 &&
    read_handle_bytes(handle, &reread, &reread_len) && reread_len == length &&
    (length == 0 || memcmp(reread, data, length) == 0);
  int directory_flushed = published && flush_handle(parent);
  napi_value retained = NULL;
  if (retain_handle && published && directory_flushed) retained = external_handle(env, handle);
  else CloseHandle(handle);
  free(initial_security);
  if (approved_existing != INVALID_HANDLE_VALUE) CloseHandle(approved_existing);
    CloseHandle(parent);
  free(target);
  if (!published) {
    if (descriptor_local) LocalFree(descriptor);
    free(pre_security);
    free(post_security);
    free(reread);
    napi_value failed;
    napi_create_object(env, &failed);
    set_bool(env, failed, "ok", false);
    set_bool(env, failed, "committed", true);
    set_bool(env, failed, "flushFailed", false);
    set_string(env, failed, "failureReason", "native-io");
    return failed;
  }
  napi_value result;
  napi_create_object(env, &result);
  if (retain_handle && retained == NULL) directory_flushed = 0;
  set_bool(env, result, "ok", directory_flushed == 1);
  if (retained != NULL) napi_set_named_property(env, result, "handle", retained);
  set_bool(env, result, "committed", true);
  set_bool(env, result, "flushFailed", directory_flushed != 1);
  set_string(env, result, "volumeSerial", post_volume);
  set_string(env, result, "fileId", post_file);
  set_string(env, result, "parentFileId", parent_file);
  set_uint(env, result, "attributes", post_attributes);
  if (have_pre_id) set_string(env, result, "preFileId", pre_file);
  set_buffer(env, result, "bytes", reread, reread_len);
  set_buffer(env, result, "securityDescriptor", post_security, post_security_size);
  if (have_pre) set_buffer(env, result, "preSecurityDescriptor", pre_security, pre_security_size);
  if (!directory_flushed) set_string(env, result, "failureReason", "flush-failed");
  if (descriptor_local) LocalFree(descriptor);
  free(pre_security);
  free(post_security);
  free(reread);
  return result;
}

static napi_value ApplySecurityMaterial(napi_env env, napi_callback_info info) {
  size_t argc = 3;
  napi_value args[3];
  napi_get_cb_info(env, info, &argc, args, NULL, NULL);
  char path_utf8[32768];
  uint32_t attributes = 0;
  void *descriptor = NULL;
  size_t descriptor_size = 0;
  if (argc < 3 || !read_utf8(env, args[0], path_utf8, sizeof(path_utf8)) ||
      napi_get_buffer_info(env, args[1], &descriptor, &descriptor_size) != napi_ok ||
      napi_get_value_uint32(env, args[2], &attributes) != napi_ok) return failure_result(env, "invalid-path");
  wchar_t *path = local_path_wide(path_utf8);
  if (path == NULL) return failure_result(env, "invalid-path");
  DWORD promised = attributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE);
  if (!SetFileSecurityW(path, OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
      (PSECURITY_DESCRIPTOR) descriptor)) {
    free(path);
    return failure_result(env, "security-unreadable");
  }
  DWORD current = GetFileAttributesW(path);
  if (current == INVALID_FILE_ATTRIBUTES) { free(path); return failure_result(env, "native-io"); }
  DWORD cleared = current & ~(FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE);
  /* READONLY blocks a later GENERIC_WRITE open. Flush the still-writable handle, then set it. */
  DWORD writable = cleared | (promised & ~FILE_ATTRIBUTE_READONLY);
  if (!SetFileAttributesW(path, writable)) { free(path); return failure_result(env, "native-io"); }
  HANDLE handle = CreateFileW(path, windows_flush_access(), FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
    NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
  int flushed = handle != INVALID_HANDLE_VALUE && flush_handle(handle);
  if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
  if (!flushed) { free(path); return failure_result(env, "flush-failed"); }
  if (!SetFileAttributesW(path, cleared | promised)) { free(path); return failure_result(env, "native-io"); }
  free(path);
  napi_value result;
  napi_create_object(env, &result);
  set_bool(env, result, "ok", true);
  set_uint(env, result, "attributes", promised);
  return result;
}

static napi_value FlushDirectory(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1];
  napi_get_cb_info(env, info, &argc, args, NULL, NULL);
  char path_utf8[32768];
  if (argc < 1 || !read_utf8(env, args[0], path_utf8, sizeof(path_utf8))) return boolean_value(env, false);
  wchar_t *path = local_path_wide(path_utf8);
  if (path == NULL) return boolean_value(env, false);
  HANDLE directory = CreateFileW(path, windows_flush_access(), FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
    NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
  free(path);
  int flushed = directory != INVALID_HANDLE_VALUE && flush_handle(directory);
  if (directory != INVALID_HANDLE_VALUE) CloseHandle(directory);
  return boolean_value(env, flushed == 1);
}

static napi_value WriteFileHandle(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value args[2];
  napi_get_cb_info(env, info, &argc, args, NULL, NULL);
  HANDLE handle = argc < 2 ? NULL : unwrap_handle(env, args[0]);
  void *bytes = NULL;
  size_t length = 0, written = 0;
  if (handle == NULL || napi_get_buffer_info(env, args[1], &bytes, &length) != napi_ok) return boolean_value(env, false);
  while (written < length) {
    DWORD count = 0;
    if (!WriteFile(handle, (unsigned char *) bytes + written,
        (DWORD) (length - written > 1 << 20 ? 1 << 20 : length - written), &count, NULL) || count == 0) return boolean_value(env, false);
    written += count;
  }
  return boolean_value(env, true);
}

static napi_value FlushFileHandle(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1];
  napi_get_cb_info(env, info, &argc, args, NULL, NULL);
  HANDLE handle = argc < 1 ? NULL : unwrap_handle(env, args[0]);
  return boolean_value(env, handle != NULL && flush_handle(handle));
}

static napi_value OpenParentDirectory(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1], result;
  napi_get_cb_info(env, info, &argc, args, NULL, NULL);
  char path_utf8[32768];
  if (argc < 1 || !read_utf8(env, args[0], path_utf8, sizeof(path_utf8))) return failure_result(env, "invalid-path");
  HANDLE handle = open_local_path(path_utf8, FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE);
  FILE_ATTRIBUTE_TAG_INFO attributes;
  if (handle == INVALID_HANDLE_VALUE) return failure_result(env, "native-io");
  if (!GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &attributes, sizeof(attributes)) ||
      (attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 || (attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
    CloseHandle(handle); return failure_result(env, "reparse");
  }
  napi_value external = external_handle(env, handle);
  if (external == NULL) return failure_result(env, "native-io");
  napi_create_object(env, &result);
  set_bool(env, result, "ok", true);
  napi_set_named_property(env, result, "handle", external);
  return result;
}

static napi_value CloseParentDirectory(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1];
  napi_get_cb_info(env, info, &argc, args, NULL, NULL);
  napi_value handle_value;
  if (argc < 1 || napi_get_named_property(env, args[0], "handle", &handle_value) != napi_ok) return boolean_value(env, false);
  WinHandle *item = NULL;
  if (napi_get_value_external(env, handle_value, (void **) &item) != napi_ok || item == NULL || item->handle == NULL) return boolean_value(env, false);
  HANDLE handle = item->handle;
  item->handle = NULL;
  return boolean_value(env, CloseHandle(handle) ? true : false);
}

static napi_value RenameDirectoryNoReplace(napi_env env, napi_callback_info info) {
  size_t argc = 3;
  napi_value args[3], result;
  napi_get_cb_info(env, info, &argc, args, NULL, NULL);
  char source_utf8[1024], target_utf8[1024];
  napi_value handle_value;
  if (argc < 3 || napi_get_named_property(env, args[0], "handle", &handle_value) != napi_ok ||
      !read_utf8(env, args[1], source_utf8, sizeof(source_utf8)) || !read_utf8(env, args[2], target_utf8, sizeof(target_utf8))) {
    return failure_result(env, "invalid-path");
  }
  HANDLE parent = unwrap_handle(env, handle_value);
  wchar_t *source = utf8_to_wide(source_utf8);
  wchar_t *target = utf8_to_wide(target_utf8);
  if (parent == NULL || source == NULL || target == NULL) {
    free(source);
    free(target);
    return failure_result(env, "invalid-path");
  }
  LONG open_status = 0;
  HANDLE directory = open_relative(parent, source, 1, DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &open_status);
  if (directory == INVALID_HANDLE_VALUE) {
    free(source); free(target);
    return failure_result(env, nt_status_missing(open_status) ? "member-missing" : "native-io");
  }
  FILE_ATTRIBUTE_TAG_INFO attributes;
  if (!GetFileInformationByHandleEx(directory, FileAttributeTagInfo, &attributes, sizeof(attributes)) ||
      (attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 || !final_segment_matches(directory, source)) {
    CloseHandle(directory); free(source); free(target); return failure_result(env, "reparse");
  }
  free(source);
  DWORD error = ERROR_SUCCESS;
  int moved = rename_relative(directory, parent, target, 0, &error);
  free(target);
  CloseHandle(directory);
  napi_create_object(env, &result);
  set_string(env, result, "platform", "win32");
  set_string(env, result, "primitive", "windows-rename-noreplace-v1");
  if (!moved) {
    int exists = error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS || error == ERROR_DIR_NOT_EMPTY;
    set_bool(env, result, "ok", false);
    set_bool(env, result, "committed", false);
    set_uint(env, result, "status", exists ? 17 : 1);
    set_string(env, result, "error", exists ? "target exists" : "rename failed");
    return result;
  }
  set_bool(env, result, "ok", true);
  set_bool(env, result, "committed", true);
  set_uint(env, result, "status", 0);
  return result;
}

static napi_value CreateKillOnCloseJob(napi_env env, napi_callback_info info) {
  (void) info;
  WindowsJob *job = calloc(1, sizeof(*job));
  napi_value external;
  if (job == NULL) return failure_result(env, "native-io");
  job->job = CreateJobObjectW(NULL, NULL);
  if (job->job == NULL) { free(job); return failure_result(env, "native-io"); }
  SetHandleInformation(job->job, HANDLE_FLAG_INHERIT, 0);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
  ZeroMemory(&limits, sizeof(limits));
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION;
  if ((limits.BasicLimitInformation.LimitFlags & (JOB_OBJECT_LIMIT_BREAKAWAY_OK | JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK)) != 0 ||
      !SetInformationJobObject(job->job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
    CloseHandle(job->job);
    free(job);
    return failure_result(env, "native-io");
  }
  if (napi_create_external(env, job, close_job, NULL, &external) != napi_ok) {
    CloseHandle(job->job);
    free(job);
    return failure_result(env, "native-io");
  }
  return external;
}

static WindowsJob *unwrap_job(napi_env env, napi_value value) {
  WindowsJob *job = NULL;
  if (napi_get_value_external(env, value, (void **) &job) != napi_ok) return NULL;
  return job;
}

static napi_value AssignProcessToJob(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value args[2];
  napi_get_cb_info(env, info, &argc, args, NULL, NULL);
  WindowsJob *job = argc < 2 ? NULL : unwrap_job(env, args[0]);
  HANDLE process = argc < 2 ? NULL : unwrap_handle(env, args[1]);
  if (job == NULL || job->closed || job->job == NULL || process == NULL) return boolean_value(env, false);
  return boolean_value(env, AssignProcessToJobObject(job->job, process) ? true : false);
}

static napi_value ResumePrimaryThread(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1];
  napi_get_cb_info(env, info, &argc, args, NULL, NULL);
  HANDLE thread = argc < 1 ? NULL : unwrap_handle(env, args[0]);
  if (thread == NULL) return boolean_value(env, false);
  DWORD count = ResumeThread(thread);
  return boolean_value(env, count != (DWORD) -1);
}

static napi_value TerminateProcessExport(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1];
  napi_get_cb_info(env, info, &argc, args, NULL, NULL);
  HANDLE process = argc < 1 ? NULL : unwrap_handle(env, args[0]);
  if (process == NULL) return boolean_value(env, false);
  return boolean_value(env, TerminateProcess(process, 1) ? true : false);
}

static napi_value TerminateJob(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1];
  napi_get_cb_info(env, info, &argc, args, NULL, NULL);
  WindowsJob *job = argc < 1 ? NULL : unwrap_job(env, args[0]);
  if (job == NULL || job->closed || job->job == NULL) return boolean_value(env, false);
  return boolean_value(env, TerminateJobObject(job->job, 1) ? true : false);
}

static napi_value JobActiveProcessCount(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1], result;
  napi_get_cb_info(env, info, &argc, args, NULL, NULL);
  WindowsJob *job = argc < 1 ? NULL : unwrap_job(env, args[0]);
  JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting;
  if (job == NULL || job->closed || job->job == NULL ||
      !QueryInformationJobObject(job->job, JobObjectBasicAccountingInformation, &accounting, sizeof(accounting), NULL)) {
    napi_create_int32(env, -1, &result);
    return result;
  }
  napi_create_uint32(env, accounting.ActiveProcesses, &result);
  return result;
}

static napi_value CloseJobExport(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1];
  napi_get_cb_info(env, info, &argc, args, NULL, NULL);
  WindowsJob *job = argc < 1 ? NULL : unwrap_job(env, args[0]);
  if (job == NULL || job->closed || job->job == NULL) return boolean_value(env, false);
  HANDLE handle = job->job;
  job->closed = 1;
  job->job = NULL;
  return boolean_value(env, CloseHandle(handle) ? true : false);
}

static napi_value ProcessExitStatus(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1], result;
  napi_get_cb_info(env, info, &argc, args, NULL, NULL);
  HANDLE process = argc < 1 ? NULL : unwrap_handle(env, args[0]);
  napi_create_object(env, &result);
  DWORD code = 0;
  /* GetExitCodeProcess may publish the code while the last thread and Job
   * accounting are still finishing. Wait for the process object itself. */
  if (process == NULL || WaitForSingleObject(process, 0) != WAIT_OBJECT_0 || !GetExitCodeProcess(process, &code)) {
    set_bool(env, result, "exited", false);
    return result;
  }
  set_bool(env, result, "exited", true);
  set_uint(env, result, "code", code);
  return result;
}

/* Anonymous pipe HANDLEs belong to this addon, not to the compiler's CRT fd
 * table (which need not be Node's CRT). Peek prevents blocking the JS thread. */
static napi_value ReadPipe(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1], result;
  napi_get_cb_info(env, info, &argc, args, NULL, NULL);
  HANDLE pipe = argc < 1 ? NULL : unwrap_handle(env, args[0]);
  if (pipe == NULL) return failure_result(env, "pipe-closed");
  DWORD available = 0;
  if (!PeekNamedPipe(pipe, NULL, 0, NULL, &available, NULL)) {
    if (GetLastError() != ERROR_BROKEN_PIPE) return failure_result(env, "pipe-read-failed");
    napi_create_object(env, &result);
    set_bool(env, result, "ok", true);
    set_bool(env, result, "eof", true);
    set_buffer(env, result, "bytes", "", 0);
    return result;
  }
  unsigned char bytes[65536];
  DWORD count = 0;
  if (available > 0 && !ReadFile(pipe, bytes, available > sizeof(bytes) ? sizeof(bytes) : available, &count, NULL)) return failure_result(env, "pipe-read-failed");
  napi_create_object(env, &result);
  set_bool(env, result, "ok", true);
  set_bool(env, result, "eof", false);
  set_buffer(env, result, "bytes", bytes, count);
  return result;
}

static wchar_t *quote_argument(const wchar_t *value) {
  if (value == NULL) return NULL;
  size_t length = 2;
  size_t run = 0;
  for (const wchar_t *cursor = value;; cursor += 1) {
    if (*cursor == L'\\') {
      run += 1;
      continue;
    }
    length += (*cursor == L'"' || *cursor == L'\0') ? run * 2 : run;
    run = 0;
    if (*cursor == L'\0') break;
    length += *cursor == L'"' ? 2 : 1;
  }
  wchar_t *quoted = calloc(length + 1, sizeof(wchar_t));
  if (quoted == NULL) return NULL;
  size_t out = 0;
  quoted[out++] = L'"';
  run = 0;
  for (const wchar_t *cursor = value;; cursor += 1) {
    if (*cursor == L'\\') {
      run += 1;
      continue;
    }
    size_t copies = (*cursor == L'"' || *cursor == L'\0') ? run * 2 : run;
    for (size_t index = 0; index < copies; index += 1) quoted[out++] = L'\\';
    run = 0;
    if (*cursor == L'\0') break;
    if (*cursor == L'"') quoted[out++] = L'\\';
    quoted[out++] = *cursor;
  }
  quoted[out++] = L'"';
  quoted[out] = L'\0';
  return quoted;
}

static napi_value SpawnSuspended(napi_env env, napi_callback_info info) {
  size_t argc = 4;
  napi_value args[4];
  napi_get_cb_info(env, info, &argc, args, NULL, NULL);
  char file_utf8[32768], cwd_utf8[32768];
  bool array = false;
  uint32_t count = 0;
  if (argc < 3 || !read_utf8(env, args[0], file_utf8, sizeof(file_utf8)) || !read_utf8(env, args[2], cwd_utf8, sizeof(cwd_utf8)) ||
      napi_is_array(env, args[1], &array) != napi_ok || !array || napi_get_array_length(env, args[1], &count) != napi_ok) {
    return failure_result(env, "invalid-path");
  }
  void *environment = NULL;
  if (argc >= 4) {
    napi_valuetype environment_type;
    if (napi_typeof(env, args[3], &environment_type) != napi_ok) return failure_result(env, "invalid-path");
    if (environment_type != napi_undefined && environment_type != napi_null) {
      size_t environment_length = 0;
      if (napi_get_buffer_info(env, args[3], &environment, &environment_length) != napi_ok || environment == NULL ||
          environment_length < 4 || (environment_length % sizeof(wchar_t)) != 0) {
        return failure_result(env, "invalid-path");
      }
    }
  }
  wchar_t *file = utf8_to_wide(file_utf8);
  wchar_t *cwd = utf8_to_wide(cwd_utf8);
  wchar_t *command = quote_argument(file == NULL ? L"" : file);
  if (file == NULL || cwd == NULL || command == NULL) {
    free(file); free(cwd); free(command);
    return failure_result(env, "invalid-path");
  }
  for (uint32_t index = 0; index < count; index += 1) {
    napi_value item;
    char argument_utf8[32768];
    napi_get_element(env, args[1], index, &item);
    if (!read_utf8(env, item, argument_utf8, sizeof(argument_utf8))) {
      free(file); free(cwd); free(command);
      return failure_result(env, "invalid-path");
    }
    wchar_t *argument = utf8_to_wide(argument_utf8);
    wchar_t *quoted = argument == NULL ? NULL : quote_argument(argument);
    free(argument);
    if (quoted == NULL) { free(file); free(cwd); free(command); return failure_result(env, "invalid-path"); }
    size_t command_len = wcslen(command);
    size_t quoted_len = wcslen(quoted);
    if (command_len > SIZE_MAX - quoted_len - 2) {
      free(quoted); free(file); free(cwd); free(command);
      return failure_result(env, "invalid-path");
    }
    size_t next_chars = command_len + quoted_len + 2;
    wchar_t *next = calloc(next_chars, sizeof(wchar_t));
    if (next == NULL) { free(quoted); free(file); free(cwd); free(command); return failure_result(env, "native-io"); }
    memcpy(next, command, command_len * sizeof(wchar_t));
    next[command_len] = L' ';
    memcpy(next + command_len + 1, quoted, quoted_len * sizeof(wchar_t));
    next[command_len + 1 + quoted_len] = L'\0';
    free(command);
    free(quoted);
    command = next;
  }
  SECURITY_ATTRIBUTES inheritable = { sizeof(inheritable), NULL, TRUE };
  HANDLE stdout_read = NULL, stdout_write = NULL, stderr_read = NULL, stderr_write = NULL;
  HANDLE stdin_null = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (stdin_null == INVALID_HANDLE_VALUE || !CreatePipe(&stdout_read, &stdout_write, &inheritable, 0) || !CreatePipe(&stderr_read, &stderr_write, &inheritable, 0)) {
    if (stdin_null != INVALID_HANDLE_VALUE) CloseHandle(stdin_null);
    if (stdout_read != NULL) CloseHandle(stdout_read);
    if (stdout_write != NULL) CloseHandle(stdout_write);
    if (stderr_read != NULL) CloseHandle(stderr_read);
    if (stderr_write != NULL) CloseHandle(stderr_write);
    free(file); free(cwd); free(command);
    return failure_result(env, "native-io");
  }
  SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0);
  STARTUPINFOEXW startup;
  ZeroMemory(&startup, sizeof(startup));
  startup.StartupInfo.cb = sizeof(startup);
  startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  startup.StartupInfo.hStdInput = stdin_null;
  startup.StartupInfo.hStdOutput = stdout_write;
  startup.StartupInfo.hStdError = stderr_write;
  SIZE_T attributes_size = 0;
  InitializeProcThreadAttributeList(NULL, 1, 0, &attributes_size);
  startup.lpAttributeList = malloc(attributes_size);
  HANDLE inherited[] = { stdin_null, stdout_write, stderr_write };
  int initialized = startup.lpAttributeList != NULL && InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &attributes_size);
  int limited = initialized && UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
    inherited, sizeof(inherited), NULL, NULL);
  PROCESS_INFORMATION process;
  ZeroMemory(&process, sizeof(process));
  DWORD flags = CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT;
  BOOL created = limited && CreateProcessW(file, command, NULL, NULL, TRUE, flags, environment, cwd, &startup.StartupInfo, &process);
  if (initialized) DeleteProcThreadAttributeList(startup.lpAttributeList);
  free(startup.lpAttributeList);
  CloseHandle(stdin_null);
  free(file);
  free(cwd);
  free(command);
  CloseHandle(stdout_write);
  CloseHandle(stderr_write);
  if (!created) {
    CloseHandle(stdout_read);
    CloseHandle(stderr_read);
    return failure_result(env, "native-io");
  }
  napi_value result, process_external, thread_external;
  napi_create_object(env, &result);
  set_bool(env, result, "ok", true);
  set_uint(env, result, "pid", process.dwProcessId);
  napi_set_named_property(env, result, "stdout", external_handle(env, stdout_read));
  napi_set_named_property(env, result, "stderr", external_handle(env, stderr_read));
  process_external = external_handle(env, process.hProcess);
  thread_external = external_handle(env, process.hThread);
  napi_set_named_property(env, result, "process", process_external);
  napi_set_named_property(env, result, "thread", thread_external);
  return result;
}

/* Directory preparation/empty removal holds the approved root and every
 * relative ancestor until the one native mutation and its flush complete. */
static int bound_matches_value(napi_env env, const BoundOpen *node, napi_value expected) {
  napi_value value;
  char volume[17], id[33];
  void *security = NULL;
  size_t security_size = 0;
  uint32_t attributes = 0;
  return napi_get_named_property(env, expected, "volumeSerial", &value) == napi_ok && read_utf8(env, value, volume, sizeof(volume)) &&
    napi_get_named_property(env, expected, "fileId", &value) == napi_ok && read_utf8(env, value, id, sizeof(id)) &&
    napi_get_named_property(env, expected, "attributes", &value) == napi_ok && napi_get_value_uint32(env, value, &attributes) == napi_ok &&
    napi_get_named_property(env, expected, "securityDescriptor", &value) == napi_ok && napi_get_buffer_info(env, value, &security, &security_size) == napi_ok &&
    strcmp(volume, node->volume) == 0 && strcmp(id, node->file_id) == 0 && attributes == node->attributes &&
    security_size == node->security_size && memcmp(security, node->security, security_size) == 0;
}

static napi_value MutatePreparedDirectory(napi_env env, napi_callback_info info) {
  size_t argc = 6;
  napi_value args[6];
  napi_get_cb_info(env, info, &argc, args, NULL, NULL);
  char root_utf8[32768], action[32];
  uint32_t count = 0;
  bool array = false;
  if (argc < 4 || !read_utf8(env, args[0], root_utf8, sizeof(root_utf8)) ||
      !read_utf8(env, args[3], action, sizeof(action)) || napi_is_array(env, args[1], &array) != napi_ok || !array ||
      napi_get_array_length(env, args[1], &count) != napi_ok || count == 0 || count > 256) return failure_result(env, "invalid-path");
  int removing = strcmp(action, "remove-created") == 0;
  int private = strcmp(action, "create-private") == 0;
  if ((!removing && !private && strcmp(action, "create-inherited") != 0) || (removing && argc < 5)) return failure_result(env, "invalid-path");
  /* Validate the complete segment array before any possible creation. */
  wchar_t **segments = calloc(count, sizeof(*segments));
  if (segments == NULL) return failure_result(env, "native-io");
  const char *failure = NULL;
  for (uint32_t index = 0; index < count; index += 1) {
    napi_value item;
    char text[1024];
    if (napi_get_element(env, args[1], index, &item) != napi_ok || !read_utf8(env, item, text, sizeof(text)) ||
        (segments[index] = utf8_to_wide(text)) == NULL || validate_segment(segments[index]) != NULL) { failure = "invalid-path"; break; }
  }
  BoundOpen *nodes = calloc((size_t) count + 1, sizeof(*nodes));
  uint32_t held = 0;
  int created = 0, existing = 0, unconfirmed = 0, removed = 0, absent = 0;
  if (nodes == NULL) failure = "native-io";
  HANDLE root = failure == NULL ? open_local_path(root_utf8, windows_flush_access(), FILE_SHARE_READ | FILE_SHARE_WRITE) : INVALID_HANDLE_VALUE;
  if (failure == NULL && root == INVALID_HANDLE_VALUE) failure = "invalid-root";
  if (failure == NULL) {
    if (!capture_bound(root, &nodes[0])) { CloseHandle(root); free(nodes[0].security); nodes[0].security = NULL; failure = "native-io"; }
    else {
      held = 1;
      wchar_t filesystem[32];
      if (!GetVolumeInformationByHandleW(root, NULL, 0, NULL, NULL, NULL, filesystem, 32) || _wcsicmp(filesystem, L"NTFS") != 0) failure = "unsupported-filesystem";
      char canonical[32768];
      if ((nodes[0].attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != FILE_ATTRIBUTE_DIRECTORY ||
          !bound_matches_value(env, &nodes[0], args[2]) || !schema_final_path(root, canonical, sizeof(canonical)) || strcmp(canonical, root_utf8) != 0) failure = "root-changed";
    }
  }
  for (uint32_t index = 0; failure == NULL && index < count; index += 1) {
    int last = index + 1 == count;
    LONG status = 0;
    HANDLE opened = INVALID_HANDLE_VALUE;
    if (last && !removing) {
      PSECURITY_DESCRIPTOR descriptor = private ? protected_descriptor() : NULL;
      if (private && descriptor == NULL) { failure = "security-unreadable"; break; }
      opened = create_new_relative(nodes[held - 1].handle, segments[index], descriptor, &status, 1);
      if (descriptor != NULL) LocalFree(descriptor);
      if (opened != INVALID_HANDLE_VALUE) { created = 1; unconfirmed = 1; }
      else if (status != (LONG) 0xC0000035L) { failure = "native-io"; break; }
    }
    if (opened == INVALID_HANDLE_VALUE) {
      opened = open_relative(nodes[held - 1].handle, segments[index], 1,
        last && removing ? DELETE | windows_flush_access() : windows_flush_access(), FILE_SHARE_READ | FILE_SHARE_WRITE, &status);
      if (opened == INVALID_HANDLE_VALUE) {
        if (removing && nt_status_missing(status)) { absent = 1; break; }
        failure = nt_status_missing(status) ? "member-missing" : "native-io"; break;
      }
      if (last && !removing) existing = 1;
    }
    if (!capture_bound(opened, &nodes[held])) {
      CloseHandle(opened); free(nodes[held].security); nodes[held].security = NULL; failure = "native-io"; break;
    }
    held += 1;
    BoundOpen *node = &nodes[held - 1];
    if ((node->attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != FILE_ATTRIBUTE_DIRECTORY ||
        strcmp(node->volume, nodes[0].volume) != 0 || !final_segment_matches(opened, segments[index])) { failure = "unsafe-directory"; break; }
    if (!last && argc >= 6) {
      napi_value expected;
      napi_valuetype type;
      if (napi_get_element(env, args[5], index, &expected) != napi_ok || napi_typeof(env, expected, &type) != napi_ok ||
          (type != napi_null && type != napi_undefined && !bound_matches_value(env, node, expected))) { failure = "directory-changed"; break; }
    }
    if (last) {
      if (created) unconfirmed = 0;
      if (private && !current_user_owns((PSECURITY_DESCRIPTOR) node->security)) { failure = "security-not-preserved"; break; }
      if (removing && !bound_matches_value(env, node, args[4])) { failure = "directory-changed"; break; }
    }
  }
  for (uint32_t index = 0; failure == NULL && index < held; index += 1) {
    if (!bound_still_same(&nodes[index], 0)) failure = "directory-changed";
  }
  if (failure == NULL && removing && !absent) {
    FILE_DISPOSITION_INFO disposition;
    disposition.DeleteFile = TRUE;
    if (!SetFileInformationByHandle(nodes[held - 1].handle, FileDispositionInfo, &disposition, sizeof(disposition))) failure = "remove-refused";
    else {
      HANDLE leaf = nodes[held - 1].handle;
      nodes[held - 1].handle = NULL;
      if (!CloseHandle(leaf)) { failure = "native-io"; unconfirmed = 1; }
      else { removed = 1; if (!flush_handle(nodes[held - 2].handle)) failure = "flush-failed"; }
    }
  } else if (failure == NULL && created) {
    int child_flushed = flush_handle(nodes[held - 1].handle);
    int parent_flushed = flush_handle(nodes[held - 2].handle);
    if (!child_flushed || !parent_flushed) failure = "flush-failed";
  }
  napi_value result;
  napi_create_object(env, &result);
  set_bool(env, result, "created", created != 0);
  set_bool(env, result, "existing", existing != 0);
  set_bool(env, result, "unconfirmed", unconfirmed != 0);
  set_bool(env, result, "removed", removed != 0);
  set_bool(env, result, "alreadyAbsent", absent != 0);
  if (held == count + 1 && !unconfirmed && !removing) {
    BoundOpen *leaf = &nodes[held - 1];
    set_string(env, result, "volumeSerial", leaf->volume);
    set_string(env, result, "fileId", leaf->file_id);
    set_uint(env, result, "attributes", leaf->attributes);
    set_buffer(env, result, "securityDescriptor", leaf->security, leaf->security_size);
    set_bool(env, result, "currentUserOwner", current_user_owns((PSECURITY_DESCRIPTOR) leaf->security) == 1);
  }
  for (uint32_t index = 0; index < held; index += 1) {
    if (nodes[index].handle != NULL && nodes[index].handle != INVALID_HANDLE_VALUE) {
      if (!CloseHandle(nodes[index].handle) && failure == NULL) failure = "native-io";
      nodes[index].handle = NULL;
    }
  }
  set_bool(env, result, "ok", failure == NULL);
  if (failure != NULL) set_string(env, result, "failureReason", failure);
  release_bound(nodes, held);
  for (uint32_t index = 0; index < count; index += 1) free(segments[index]);
  free(segments);
  return result;
}

static napi_value CreateProtectedDirectory(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1];
  napi_get_cb_info(env, info, &argc, args, NULL, NULL);
  char path_utf8[32768];
  if (argc < 1 || !read_utf8(env, args[0], path_utf8, sizeof(path_utf8))) return failure_result(env, "invalid-path");
  wchar_t *path = local_path_wide(path_utf8);
  if (path == NULL) return failure_result(env, "invalid-path");
  wchar_t *leaf = wcsrchr(path, L'\\');
  if (leaf == NULL || validate_segment(++leaf) != NULL) { free(path); return failure_result(env, "invalid-path"); }
  char *slash = strrchr(path_utf8, '/'), *backslash = strrchr(path_utf8, '\\');
  if (backslash != NULL && (slash == NULL || backslash > slash)) slash = backslash;
  if (slash == NULL) { free(path); return failure_result(env, "invalid-path"); }
  if (slash == path_utf8 + 2) slash[1] = '\0'; else *slash = '\0';
  HANDLE parent = open_local_path(path_utf8, windows_flush_access(), FILE_SHARE_READ | FILE_SHARE_WRITE);
  if (parent == INVALID_HANDLE_VALUE) { free(path); return failure_result(env, "invalid-root"); }
  FILE_ATTRIBUTE_TAG_INFO parent_attributes;
  if (!GetFileInformationByHandleEx(parent, FileAttributeTagInfo, &parent_attributes, sizeof(parent_attributes)) ||
      (parent_attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 || (parent_attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
    CloseHandle(parent); free(path); return failure_result(env, "reparse");
  }
  PSECURITY_DESCRIPTOR descriptor = protected_descriptor();
  if (descriptor == NULL) { CloseHandle(parent); free(path); return failure_result(env, "security-unreadable"); }
  LONG status = 0;
  HANDLE handle = create_new_relative(parent, leaf, descriptor, &status, 1);
  if (handle == INVALID_HANDLE_VALUE) {
    LocalFree(descriptor); CloseHandle(parent); free(path);
    return failure_result(env, status == (LONG) 0xC0000035L ? "exists" : "native-io");
  }
  unsigned char *security = NULL;
  DWORD security_size = 0;
  int protected = copy_security(handle, &security, &security_size) &&
    protected_security_matches((PSECURITY_DESCRIPTOR) security, descriptor) && final_segment_matches(handle, leaf);
  free(security); LocalFree(descriptor); free(path);
  if (!protected) { delete_open_file(handle); CloseHandle(handle); CloseHandle(parent); return failure_result(env, "security-not-preserved"); }
  int flushed = flush_handle(handle) && flush_handle(parent);
  CloseHandle(handle); CloseHandle(parent);
  if (!flushed) return failure_result(env, "flush-failed");
  napi_value result;
  napi_create_object(env, &result);
  set_bool(env, result, "ok", true);
  return result;
}

static napi_value ReplaceExistingFile(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value args[2];
  napi_get_cb_info(env, info, &argc, args, NULL, NULL);
  char source_utf8[32768], target_utf8[32768];
  if (argc < 2 || !read_utf8(env, args[0], source_utf8, sizeof(source_utf8)) ||
      !read_utf8(env, args[1], target_utf8, sizeof(target_utf8))) return failure_result(env, "invalid-path");
  wchar_t *source = local_path_wide(source_utf8);
  wchar_t *target = local_path_wide(target_utf8);
  if (source == NULL || target == NULL) {
    free(source);
    free(target);
    return failure_result(env, "invalid-path");
  }
  wchar_t *source_slash = wcsrchr(source, L'\\');
  if (source_slash == NULL) source_slash = wcsrchr(source, L'/');
  wchar_t *target_slash = wcsrchr(target, L'\\');
  if (target_slash == NULL) target_slash = wcsrchr(target, L'/');
  if (source_slash == NULL || target_slash == NULL || source_slash[1] == L'\0' || target_slash[1] == L'\0') {
    free(source);
    free(target);
    return failure_result(env, "invalid-path");
  }
  *source_slash = L'\0';
  *target_slash = L'\0';
  if (CompareStringOrdinal(source, -1, target, -1, FALSE) != CSTR_EQUAL) {
    free(source);
    free(target);
    return failure_result(env, "invalid-path");
  }
  const wchar_t *source_leaf = source_slash + 1;
  const wchar_t *target_leaf = target_slash + 1;
  HANDLE parent = CreateFileW(source, windows_flush_access() | DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
    NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
  if (parent == INVALID_HANDLE_VALUE) {
    free(source);
    free(target);
    return failure_result(env, win32_open_reason(GetLastError()));
  }
  char parent_volume[17], parent_file[33], source_volume[17], source_file[33];
  DWORD parent_attributes = 0, source_attributes = 0;
  LONG source_status = 0;
  HANDLE file = open_relative(parent, source_leaf, 0, DELETE | windows_flush_access(), FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &source_status);
  int identified = file != INVALID_HANDLE_VALUE && file_identity(parent, parent_volume, parent_file, &parent_attributes) &&
    file_identity(file, source_volume, source_file, &source_attributes) && strcmp(source_volume, parent_volume) == 0 &&
    (parent_attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
  if (!identified) {
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    CloseHandle(parent);
    free(source);
    free(target);
    return failure_result(env, file == INVALID_HANDLE_VALUE && nt_status_missing(source_status) ? "member-missing" : "native-io");
  }
  DWORD rename_error = ERROR_SUCCESS;
  int moved = rename_relative(file, parent, target_leaf, 1, &rename_error);
  char post_volume[17], post_file[33], parent_volume_after[17], parent_file_after[33];
  DWORD post_attributes = 0, parent_attributes_after = 0;
  int stable = moved && file_identity(file, post_volume, post_file, &post_attributes) && strcmp(post_file, source_file) == 0 &&
    final_segment_matches(file, target_leaf) &&
    file_identity(parent, parent_volume_after, parent_file_after, &parent_attributes_after) &&
    strcmp(parent_file_after, parent_file) == 0 && flush_handle(file) && flush_handle(parent);
  CloseHandle(file);
  CloseHandle(parent);
  free(source);
  free(target);
  if (!moved) return failure_result(env, "native-io");
  napi_value result;
  napi_create_object(env, &result);
  set_bool(env, result, "ok", stable == 1);
  set_bool(env, result, "committed", true);
  set_string(env, result, "fileId", stable ? post_file : source_file);
  set_string(env, result, "parentFileId", parent_file);
  if (!stable) set_string(env, result, "failureReason", "native-io");
  return result;
}

NAPI_MODULE_INIT() {
  napi_value value;
  napi_property_descriptor descriptors[] = {
    { "readFileBoundNative", 0, ReadFileBoundNative, 0, 0, 0, napi_default, 0 },
    { "readCapturedFile", 0, ReadCapturedFile, 0, 0, 0, napi_default, 0 },
    { "captureSecurity", 0, CaptureSecurity, 0, 0, 0, napi_default, 0 },
    { "observeFilesystemTreeNative", 0, ObserveFilesystemTreeNative, 0, 0, 0, napi_default, 0 },
    { "windowsRestoreCapable", 0, WindowsRestoreCapable, 0, 0, 0, napi_default, 0 },
    { "publishProtectedFile", 0, PublishProtectedFile, 0, 0, 0, napi_default, 0 },
    { "applySecurityMaterial", 0, ApplySecurityMaterial, 0, 0, 0, napi_default, 0 },
    { "flushDirectory", 0, FlushDirectory, 0, 0, 0, napi_default, 0 },
    { "flushFileHandle", 0, FlushFileHandle, 0, 0, 0, napi_default, 0 },
    { "writeFileHandle", 0, WriteFileHandle, 0, 0, 0, napi_default, 0 },
    { "openParentDirectory", 0, OpenParentDirectory, 0, 0, 0, napi_default, 0 },
    { "closeParentDirectory", 0, CloseParentDirectory, 0, 0, 0, napi_default, 0 },
    { "renameDirectoryNoReplace", 0, RenameDirectoryNoReplace, 0, 0, 0, napi_default, 0 },
    { "replaceExistingFile", 0, ReplaceExistingFile, 0, 0, 0, napi_default, 0 },
    { "createKillOnCloseJob", 0, CreateKillOnCloseJob, 0, 0, 0, napi_default, 0 },
    { "createProtectedDirectory", 0, CreateProtectedDirectory, 0, 0, 0, napi_default, 0 },
    { "mutatePreparedDirectory", 0, MutatePreparedDirectory, 0, 0, 0, napi_default, 0 },
    { "assignProcessToJob", 0, AssignProcessToJob, 0, 0, 0, napi_default, 0 },
    { "resumePrimaryThread", 0, ResumePrimaryThread, 0, 0, 0, napi_default, 0 },
    { "terminateProcess", 0, TerminateProcessExport, 0, 0, 0, napi_default, 0 },
    { "terminateJob", 0, TerminateJob, 0, 0, 0, napi_default, 0 },
    { "jobActiveProcessCount", 0, JobActiveProcessCount, 0, 0, 0, napi_default, 0 },
    { "closeJob", 0, CloseJobExport, 0, 0, 0, napi_default, 0 },
    { "processExitStatus", 0, ProcessExitStatus, 0, 0, 0, napi_default, 0 },
    { "readPipe", 0, ReadPipe, 0, 0, 0, napi_default, 0 },
    { "spawnSuspended", 0, SpawnSuspended, 0, 0, 0, napi_default, 0 },
  };
  napi_define_properties(env, exports, sizeof(descriptors) / sizeof(descriptors[0]), descriptors);
  napi_create_string_utf8(env, "win32", NAPI_AUTO_LENGTH, &value);
  napi_set_named_property(env, exports, "platform", value);
  return exports;
}
