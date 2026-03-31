#ifndef MSLANGC_FRONTEND_RESOLUTION_TABLE_H_
#define MSLANGC_FRONTEND_RESOLUTION_TABLE_H_

#include <stddef.h>
#include <stdint.h>

typedef enum MsBindingKind {
  MS_BINDING_GLOBAL,
  MS_BINDING_LOCAL,
  MS_BINDING_UPVALUE
} MsBindingKind;

typedef enum MsFunctionFlags {
  MS_FUNCTION_FLAG_NONE = 0,
  MS_FUNCTION_FLAG_METHOD = 1 << 0,
  MS_FUNCTION_FLAG_INITIALIZER = 1 << 1,
  MS_FUNCTION_FLAG_HAS_SELF = 1 << 2,
  MS_FUNCTION_FLAG_HAS_SUPER = 1 << 3
} MsFunctionFlags;

typedef struct MsResolvedBinding {
  size_t node_id;
  size_t function_node_id;
  MsBindingKind kind;
  uint8_t slot;
  int scope_depth;
  int lexical_depth;
  int is_captured;
} MsResolvedBinding;

typedef struct MsFunctionUpvalue {
  uint8_t index;
  uint8_t is_local;
  uint8_t slot;
  int lexical_depth;
} MsFunctionUpvalue;

typedef struct MsFunctionResolution {
  size_t node_id;
  size_t enclosing_node_id;
  size_t local_count;
  MsFunctionUpvalue *upvalues;
  size_t upvalue_count;
  size_t upvalue_capacity;
  unsigned flags;
} MsFunctionResolution;

typedef struct MsResolutionTable {
  MsResolvedBinding *bindings;
  size_t count;
  size_t capacity;
  MsFunctionResolution *functions;
  size_t function_count;
  size_t function_capacity;
} MsResolutionTable;

void ms_resolution_table_init(MsResolutionTable *table);
void ms_resolution_table_clear(MsResolutionTable *table);
void ms_resolution_table_destroy(MsResolutionTable *table);
int ms_resolution_table_set(MsResolutionTable *table,
                            size_t node_id,
                            size_t function_node_id,
                            MsBindingKind kind,
                            uint8_t slot,
                            int scope_depth,
                            int lexical_depth,
                            int is_captured);
int ms_resolution_table_mark_captured(MsResolutionTable *table, size_t node_id);
int ms_resolution_table_get(const MsResolutionTable *table,
                            size_t node_id,
                            MsResolvedBinding *out_binding);
int ms_resolution_table_set_function(MsResolutionTable *table,
                                     size_t node_id,
                                     size_t enclosing_node_id,
                                     unsigned flags);
int ms_resolution_table_set_function_local_count(MsResolutionTable *table,
                                                 size_t node_id,
                                                 size_t local_count);
int ms_resolution_table_add_upvalue(MsResolutionTable *table,
                                    size_t function_node_id,
                                    uint8_t is_local,
                                    uint8_t slot,
                                    int lexical_depth,
                                    uint8_t *out_index);
int ms_resolution_table_get_function(const MsResolutionTable *table,
                                     size_t node_id,
                                     MsFunctionResolution *out_function);

#endif