#include "ms/frontend/resolution_table.h"

#include <stdlib.h>

static int ms_resolution_table_ensure_binding_capacity(MsResolutionTable* table,
                                                       size_t min_capacity) {
  MsResolvedBinding* bindings;
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

  bindings = (MsResolvedBinding*) realloc(table->bindings,
                                          new_capacity * sizeof(*bindings));
  if (bindings == NULL) {
    return 0;
  }

  table->bindings = bindings;
  table->capacity = new_capacity;
  return 1;
}

static int ms_resolution_table_ensure_function_capacity(MsResolutionTable* table,
                                                        size_t min_capacity) {
  MsFunctionResolution* functions;
  size_t new_capacity;

  if (table == NULL) {
    return 0;
  }
  if (min_capacity <= table->function_capacity) {
    return 1;
  }

  new_capacity = table->function_capacity == 0 ? 8 : table->function_capacity;
  while (new_capacity < min_capacity) {
    if (new_capacity > (SIZE_MAX / 2)) {
      new_capacity = min_capacity;
      break;
    }
    new_capacity *= 2;
  }

  functions = (MsFunctionResolution*) realloc(table->functions,
                                              new_capacity * sizeof(*functions));
  if (functions == NULL) {
    return 0;
  }

  table->functions = functions;
  table->function_capacity = new_capacity;
  return 1;
}

static MsFunctionResolution* ms_resolution_table_find_function(
    MsResolutionTable* table,
    size_t node_id) {
  size_t i;

  if (table == NULL) {
    return NULL;
  }

  for (i = 0; i < table->function_count; ++i) {
    if (table->functions[i].node_id == node_id) {
      return &table->functions[i];
    }
  }

  return NULL;
}

static const MsFunctionResolution* ms_resolution_table_find_const_function(
    const MsResolutionTable* table,
    size_t node_id) {
  size_t i;

  if (table == NULL) {
    return NULL;
  }

  for (i = 0; i < table->function_count; ++i) {
    if (table->functions[i].node_id == node_id) {
      return &table->functions[i];
    }
  }

  return NULL;
}

static int ms_resolution_table_ensure_upvalue_capacity(
    MsFunctionResolution* function,
    size_t min_capacity) {
  MsFunctionUpvalue* upvalues;
  size_t new_capacity;

  if (function == NULL) {
    return 0;
  }
  if (min_capacity <= function->upvalue_capacity) {
    return 1;
  }

  new_capacity = function->upvalue_capacity == 0 ? 4 : function->upvalue_capacity;
  while (new_capacity < min_capacity) {
    if (new_capacity > (SIZE_MAX / 2)) {
      new_capacity = min_capacity;
      break;
    }
    new_capacity *= 2;
  }

  upvalues = (MsFunctionUpvalue*) realloc(function->upvalues,
                                          new_capacity * sizeof(*upvalues));
  if (upvalues == NULL) {
    return 0;
  }

  function->upvalues = upvalues;
  function->upvalue_capacity = new_capacity;
  return 1;
}

static int ms_resolution_table_ensure_captured_local_capacity(
    MsFunctionResolution* function,
    size_t min_capacity) {
  uint8_t* captured_locals;
  size_t new_capacity;

  if (function == NULL) {
    return 0;
  }
  if (min_capacity <= function->captured_local_capacity) {
    return 1;
  }

  new_capacity = function->captured_local_capacity == 0 ? 4 :
      function->captured_local_capacity;
  while (new_capacity < min_capacity) {
    if (new_capacity > (SIZE_MAX / 2)) {
      new_capacity = min_capacity;
      break;
    }
    new_capacity *= 2;
  }

  captured_locals = (uint8_t*) realloc(function->captured_locals,
                                       new_capacity * sizeof(*captured_locals));
  if (captured_locals == NULL) {
    return 0;
  }

  function->captured_locals = captured_locals;
  function->captured_local_capacity = new_capacity;
  return 1;
}

void ms_resolution_table_init(MsResolutionTable* table) {
  if (table == NULL) {
    return;
  }

  table->bindings = NULL;
  table->count = 0;
  table->capacity = 0;
  table->functions = NULL;
  table->function_count = 0;
  table->function_capacity = 0;
}

void ms_resolution_table_clear(MsResolutionTable* table) {
  size_t i;

  if (table == NULL) {
    return;
  }

  for (i = 0; i < table->function_count; ++i) {
    free(table->functions[i].upvalues);
    free(table->functions[i].captured_locals);
    table->functions[i].upvalues = NULL;
    table->functions[i].upvalue_count = 0;
    table->functions[i].upvalue_capacity = 0;
    table->functions[i].captured_locals = NULL;
    table->functions[i].captured_local_count = 0;
    table->functions[i].captured_local_capacity = 0;
  }

  table->count = 0;
  table->function_count = 0;
}

void ms_resolution_table_destroy(MsResolutionTable* table) {
  if (table == NULL) {
    return;
  }

  ms_resolution_table_clear(table);
  free(table->functions);
  free(table->bindings);
  table->functions = NULL;
  table->bindings = NULL;
  table->function_capacity = 0;
  table->capacity = 0;
}

int ms_resolution_table_set(MsResolutionTable* table,
                            size_t node_id,
                            size_t function_node_id,
                            MsBindingKind kind,
                            uint8_t slot,
                            int scope_depth,
                            int lexical_depth,
                            int is_captured) {
  size_t i;

  if (table == NULL) {
    return 0;
  }

  for (i = 0; i < table->count; ++i) {
    if (table->bindings[i].node_id == node_id) {
      table->bindings[i].function_node_id = function_node_id;
      table->bindings[i].kind = kind;
      table->bindings[i].slot = slot;
      table->bindings[i].scope_depth = scope_depth;
      table->bindings[i].lexical_depth = lexical_depth;
      table->bindings[i].is_captured = is_captured;
      return 1;
    }
  }

  if (!ms_resolution_table_ensure_binding_capacity(table, table->count + 1)) {
    return 0;
  }

  table->bindings[table->count].node_id = node_id;
  table->bindings[table->count].function_node_id = function_node_id;
  table->bindings[table->count].kind = kind;
  table->bindings[table->count].slot = slot;
  table->bindings[table->count].scope_depth = scope_depth;
  table->bindings[table->count].lexical_depth = lexical_depth;
  table->bindings[table->count].is_captured = is_captured;
  table->count += 1;
  return 1;
}

int ms_resolution_table_mark_captured(MsResolutionTable* table, size_t node_id) {
  size_t i;

  if (table == NULL) {
    return 0;
  }

  for (i = 0; i < table->count; ++i) {
    if (table->bindings[i].node_id == node_id) {
      table->bindings[i].is_captured = 1;
      return 1;
    }
  }

  return 1;
}

int ms_resolution_table_get(const MsResolutionTable* table,
                            size_t node_id,
                            MsResolvedBinding* out_binding) {
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

int ms_resolution_table_set_function(MsResolutionTable* table,
                                     size_t node_id,
                                     size_t enclosing_node_id,
                                     unsigned flags) {
  MsFunctionResolution* function;

  if (table == NULL) {
    return 0;
  }

  function = ms_resolution_table_find_function(table, node_id);
  if (function != NULL) {
    free(function->upvalues);
    free(function->captured_locals);
    function->upvalues = NULL;
    function->upvalue_count = 0;
    function->upvalue_capacity = 0;
    function->captured_locals = NULL;
    function->captured_local_count = 0;
    function->captured_local_capacity = 0;
    function->enclosing_node_id = enclosing_node_id;
    function->local_count = 0;
    function->flags = flags;
    return 1;
  }

  if (!ms_resolution_table_ensure_function_capacity(table,
                                                    table->function_count + 1)) {
    return 0;
  }

  function = &table->functions[table->function_count];
  function->node_id = node_id;
  function->enclosing_node_id = enclosing_node_id;
  function->local_count = 0;
  function->upvalues = NULL;
  function->upvalue_count = 0;
  function->upvalue_capacity = 0;
  function->captured_locals = NULL;
  function->captured_local_count = 0;
  function->captured_local_capacity = 0;
  function->flags = flags;
  table->function_count += 1;
  return 1;
}

int ms_resolution_table_set_function_local_count(MsResolutionTable* table,
                                                 size_t node_id,
                                                 size_t local_count) {
  MsFunctionResolution* function = ms_resolution_table_find_function(table, node_id);

  if (function == NULL) {
    return 0;
  }
  if (local_count > function->local_count) {
    function->local_count = local_count;
  }
  return 1;
}

int ms_resolution_table_mark_function_local_captured(MsResolutionTable* table,
                                                     size_t node_id,
                                                     uint8_t slot) {
  MsFunctionResolution* function;
  size_t i;

  if (table == NULL) {
    return 0;
  }

  function = ms_resolution_table_find_function(table, node_id);
  if (function == NULL) {
    return 0;
  }

  for (i = 0; i < function->captured_local_count; ++i) {
    if (function->captured_locals[i] == slot) {
      return 1;
    }
  }

  if (!ms_resolution_table_ensure_captured_local_capacity(
          function, function->captured_local_count + 1)) {
    return 0;
  }

  function->captured_locals[function->captured_local_count] = slot;
  function->captured_local_count += 1;
  return 1;
}

int ms_resolution_table_add_upvalue(MsResolutionTable* table,
                                    size_t function_node_id,
                                    uint8_t is_local,
                                    uint8_t slot,
                                    int lexical_depth,
                                    uint8_t* out_index) {
  MsFunctionResolution* function;
  size_t i;

  if (table == NULL || out_index == NULL) {
    return 0;
  }

  function = ms_resolution_table_find_function(table, function_node_id);
  if (function == NULL) {
    return 0;
  }

  for (i = 0; i < function->upvalue_count; ++i) {
    if (function->upvalues[i].is_local == is_local &&
        function->upvalues[i].slot == slot) {
      *out_index = function->upvalues[i].index;
      return 1;
    }
  }

  if (function->upvalue_count > UINT8_MAX ||
      !ms_resolution_table_ensure_upvalue_capacity(function,
                                                   function->upvalue_count + 1)) {
    return 0;
  }

  function->upvalues[function->upvalue_count].index =
      (uint8_t) function->upvalue_count;
  function->upvalues[function->upvalue_count].is_local = is_local;
  function->upvalues[function->upvalue_count].slot = slot;
  function->upvalues[function->upvalue_count].lexical_depth = lexical_depth;
  *out_index = (uint8_t) function->upvalue_count;
  function->upvalue_count += 1;
  return 1;
}

int ms_resolution_table_get_function(const MsResolutionTable* table,
                                     size_t node_id,
                                     MsFunctionResolution* out_function) {
  const MsFunctionResolution* function;

  if (table == NULL || out_function == NULL) {
    return 0;
  }

  function = ms_resolution_table_find_const_function(table, node_id);
  if (function == NULL) {
    return 0;
  }

  *out_function = *function;
  return 1;
}

int ms_resolution_table_function_local_is_captured(
    const MsResolutionTable* table,
    size_t node_id,
    uint8_t slot,
    int* out_is_captured) {
  const MsFunctionResolution* function;
  size_t i;

  if (table == NULL || out_is_captured == NULL) {
    return 0;
  }

  function = ms_resolution_table_find_const_function(table, node_id);
  if (function == NULL) {
    return 0;
  }

  *out_is_captured = 0;
  for (i = 0; i < function->captured_local_count; ++i) {
    if (function->captured_locals[i] == slot) {
      *out_is_captured = 1;
      return 1;
    }
  }

  return 1;
}