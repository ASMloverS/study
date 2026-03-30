#ifndef MSLANGC_FRONTEND_RESOLUTION_TABLE_H_
#define MSLANGC_FRONTEND_RESOLUTION_TABLE_H_

#include <stddef.h>
#include <stdint.h>

typedef enum MsBindingKind {
  MS_BINDING_GLOBAL,
  MS_BINDING_LOCAL
} MsBindingKind;

typedef struct MsResolvedBinding {
  size_t node_id;
  MsBindingKind kind;
  uint8_t slot;
  int scope_depth;
} MsResolvedBinding;

typedef struct MsResolutionTable {
  MsResolvedBinding *bindings;
  size_t count;
  size_t capacity;
} MsResolutionTable;

void ms_resolution_table_init(MsResolutionTable *table);
void ms_resolution_table_clear(MsResolutionTable *table);
void ms_resolution_table_destroy(MsResolutionTable *table);
int ms_resolution_table_set(MsResolutionTable *table,
                            size_t node_id,
                            MsBindingKind kind,
                            uint8_t slot,
                            int scope_depth);
int ms_resolution_table_get(const MsResolutionTable *table,
                            size_t node_id,
                            MsResolvedBinding *out_binding);

#endif