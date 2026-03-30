#include "ms/frontend/resolution_table.h"

#include <stdlib.h>

static int ms_resolution_table_ensure_capacity(MsResolutionTable *table,
                                               size_t min_capacity) {
  MsResolvedBinding *bindings;
  size_t new_capacity;

  if (table == NULL) {
    return 0;
  }
  if (min_capacity <= table->capacity) {
    return 1;
  }

  new_capacity = table->capacity == 0 ? 16 : table->capacity;
  while (new_capacity < min_capacity) {
    if (new_capacity > (SIZE_MAX / 2)) {
      new_capacity = min_capacity;
      break;
    }
    new_capacity *= 2;
  }

  bindings = (MsResolvedBinding *) realloc(table->bindings,
                                           new_capacity * sizeof(*bindings));
  if (bindings == NULL) {
    return 0;
  }

  table->bindings = bindings;
  table->capacity = new_capacity;
  return 1;
}

void ms_resolution_table_init(MsResolutionTable *table) {
  if (table == NULL) {
    return;
  }

  table->bindings = NULL;
  table->count = 0;
  table->capacity = 0;
}

void ms_resolution_table_clear(MsResolutionTable *table) {
  if (table == NULL) {
    return;
  }

  table->count = 0;
}

void ms_resolution_table_destroy(MsResolutionTable *table) {
  if (table == NULL) {
    return;
  }

  free(table->bindings);
  table->bindings = NULL;
  table->count = 0;
  table->capacity = 0;
}

int ms_resolution_table_set(MsResolutionTable *table,
                            size_t node_id,
                            MsBindingKind kind,
                            uint8_t slot,
                            int scope_depth) {
  size_t i;

  if (table == NULL) {
    return 0;
  }

  for (i = 0; i < table->count; ++i) {
    if (table->bindings[i].node_id == node_id) {
      table->bindings[i].kind = kind;
      table->bindings[i].slot = slot;
      table->bindings[i].scope_depth = scope_depth;
      return 1;
    }
  }

  if (!ms_resolution_table_ensure_capacity(table, table->count + 1)) {
    return 0;
  }

  table->bindings[table->count].node_id = node_id;
  table->bindings[table->count].kind = kind;
  table->bindings[table->count].slot = slot;
  table->bindings[table->count].scope_depth = scope_depth;
  table->count += 1;
  return 1;
}

int ms_resolution_table_get(const MsResolutionTable *table,
                            size_t node_id,
                            MsResolvedBinding *out_binding) {
  size_t i;

  if (table == NULL || out_binding == NULL) {
    return 0;
  }

  for (i = 0; i < table->count; ++i) {
    if (table->bindings[i].node_id == node_id) {
      *out_binding = table->bindings[i];
      return 1;
    }
  }

  return 0;
}