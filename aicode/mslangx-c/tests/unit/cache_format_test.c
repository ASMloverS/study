#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ms/cache/cache_format.h"

#include "test_assert.h"

static int test_constants(void) {
  uint8_t magic[MS_CACHE_MAGIC_SIZE];
  int format_version = MS_CACHE_FORMAT_VERSION;
  int abi_version = MS_CACHE_ABI_VERSION;
  int source_kind = MS_CACHE_ENTRY_KIND_SOURCE;
  int module_kind = MS_CACHE_ENTRY_KIND_MODULE;
  size_t u16_size = MS_CACHE_U16_SIZE;
  size_t u32_size = MS_CACHE_U32_SIZE;
  size_t u64_size = MS_CACHE_U64_SIZE;
  size_t i64_size = MS_CACHE_I64_SIZE;
  size_t double_size = MS_CACHE_DOUBLE_SIZE;

  memcpy(magic, ms_cache_magic, sizeof(magic));
  TEST_ASSERT(magic[0] == 'M');
  TEST_ASSERT(magic[1] == 'S');
  TEST_ASSERT(magic[2] == 'L');
  TEST_ASSERT(magic[3] == 'C');
  TEST_ASSERT(magic[4] == 'M');
  TEST_ASSERT(magic[5] == 'S');
  TEST_ASSERT(magic[6] == 'C');
  TEST_ASSERT(magic[7] == '\0');
  TEST_ASSERT(format_version == 1);
  TEST_ASSERT(abi_version == 1);
  TEST_ASSERT(source_kind != module_kind);
  TEST_ASSERT(u16_size == sizeof(uint16_t));
  TEST_ASSERT(u32_size == sizeof(uint32_t));
  TEST_ASSERT(u64_size == sizeof(uint64_t));
  TEST_ASSERT(i64_size == sizeof(int64_t));
  TEST_ASSERT(double_size == sizeof(double));
  return 0;
}

static int test_primitive_round_trips(void) {
  uint8_t buffer[32];
  uint16_t u16_value = 0x1234u;
  uint16_t u16_round_trip = 0;
  uint32_t u32_value = 0x89abcdefu;
  uint32_t u32_round_trip = 0;
  uint64_t u64_value = 0x0123456789abcdefull;
  uint64_t u64_round_trip = 0;
  int64_t i64_value = -0x0123456789abcll;
  int64_t i64_round_trip = 0;
  double double_value = 12345.5;
  double double_round_trip = 0.0;
  uint64_t double_bits = 0;
  uint64_t double_round_trip_bits = 0;

  memset(buffer, 0xcc, sizeof(buffer));

  TEST_ASSERT(ms_cache_write_u16_le(buffer, sizeof(buffer), 0, u16_value));
  TEST_ASSERT(buffer[0] == 0x34u);
  TEST_ASSERT(buffer[1] == 0x12u);
  TEST_ASSERT(ms_cache_read_u16_le(buffer, sizeof(buffer), 0, &u16_round_trip));
  TEST_ASSERT(u16_round_trip == u16_value);

  TEST_ASSERT(ms_cache_write_u32_le(buffer, sizeof(buffer), 4, u32_value));
  TEST_ASSERT(buffer[4] == 0xefu);
  TEST_ASSERT(buffer[5] == 0xcdu);
  TEST_ASSERT(buffer[6] == 0xabu);
  TEST_ASSERT(buffer[7] == 0x89u);
  TEST_ASSERT(ms_cache_read_u32_le(buffer, sizeof(buffer), 4, &u32_round_trip));
  TEST_ASSERT(u32_round_trip == u32_value);

  TEST_ASSERT(ms_cache_write_u64_le(buffer, sizeof(buffer), 8, u64_value));
  TEST_ASSERT(buffer[8] == 0xefu);
  TEST_ASSERT(buffer[9] == 0xcdu);
  TEST_ASSERT(buffer[10] == 0xabu);
  TEST_ASSERT(buffer[11] == 0x89u);
  TEST_ASSERT(buffer[12] == 0x67u);
  TEST_ASSERT(buffer[13] == 0x45u);
  TEST_ASSERT(buffer[14] == 0x23u);
  TEST_ASSERT(buffer[15] == 0x01u);
  TEST_ASSERT(ms_cache_read_u64_le(buffer, sizeof(buffer), 8, &u64_round_trip));
  TEST_ASSERT(u64_round_trip == u64_value);

  TEST_ASSERT(ms_cache_write_i64_le(buffer, sizeof(buffer), 16, i64_value));
  TEST_ASSERT(ms_cache_read_i64_le(buffer, sizeof(buffer), 16, &i64_round_trip));
  TEST_ASSERT(i64_round_trip == i64_value);

  memcpy(&double_bits, &double_value, sizeof(double_bits));
  TEST_ASSERT(ms_cache_write_double_le(buffer, sizeof(buffer), 0, double_value));
  TEST_ASSERT(ms_cache_read_double_le(buffer, sizeof(buffer), 0, &double_round_trip));
  memcpy(&double_round_trip_bits, &double_round_trip, sizeof(double_round_trip_bits));
  TEST_ASSERT(double_round_trip_bits == double_bits);

  return 0;
}

static int test_primitive_bounds(void) {
  uint8_t buffer[4];
  uint32_t value = 0;

  TEST_ASSERT(!ms_cache_write_u32_le(buffer, sizeof(buffer), 1, 0x11223344u));
  TEST_ASSERT(!ms_cache_read_u32_le(buffer, sizeof(buffer), 1, &value));
  return 0;
}

static int test_metadata_storage(void) {
  MsCacheSourceMetadata metadata;

  metadata.display_path = "src/example.ms";
  metadata.byte_size = 4096u;
  metadata.modification_time = -123456789ll;

  TEST_ASSERT(strcmp(metadata.display_path, "src/example.ms") == 0);
  TEST_ASSERT(metadata.byte_size == 4096u);
  TEST_ASSERT(metadata.modification_time == -123456789ll);
  return 0;
}

static int test_derive_cache_path(const char *source_path,
                                  const char *expected_path) {
  char *cache_path = NULL;

  TEST_ASSERT(ms_cache_derive_path(source_path, &cache_path));
  TEST_ASSERT(strcmp(cache_path, expected_path) == 0);
  free(cache_path);
  return 0;
}

static int test_cache_path_derivation(void) {
#if defined(_WIN32)
  TEST_ASSERT(test_derive_cache_path("script.ms", "__mscache__\\script.msc") == 0);
  TEST_ASSERT(test_derive_cache_path("src\\nested\\example.ms",
                                     "src\\nested\\__mscache__\\example.msc") == 0);
  TEST_ASSERT(test_derive_cache_path("dir.with.dots\\name.extra.ms",
                                     "dir.with.dots\\__mscache__\\name.extra.msc") == 0);
  TEST_ASSERT(test_derive_cache_path("C:\\script.ms", "C:\\__mscache__\\script.msc") == 0);
#else
  TEST_ASSERT(test_derive_cache_path("script.ms", "__mscache__/script.msc") == 0);
  TEST_ASSERT(test_derive_cache_path("src/nested/example.ms",
                                     "src/nested/__mscache__/example.msc") == 0);
  TEST_ASSERT(test_derive_cache_path("dir.with.dots/name.extra.ms",
                                     "dir.with.dots/__mscache__/name.extra.msc") == 0);
  TEST_ASSERT(test_derive_cache_path("/script.ms", "/__mscache__/script.msc") == 0);
#endif
  TEST_ASSERT(ms_cache_derive_path("script.txt", NULL) == 0);
  return 0;
}

int main(void) {
  TEST_ASSERT(test_constants() == 0);
  TEST_ASSERT(test_primitive_round_trips() == 0);
  TEST_ASSERT(test_primitive_bounds() == 0);
  TEST_ASSERT(test_metadata_storage() == 0);
  TEST_ASSERT(test_cache_path_derivation() == 0);
  return 0;
}
