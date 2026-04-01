#ifndef MS_TABLE_H
#define MS_TABLE_H

#include "common.h"
#include "value.h"
#include "object.h"

typedef struct {
	MsString *key;
	MsValue value;
} MsTableEntry;

typedef struct {
	MsTableEntry *entries;
	int count;
	int capacity;
} MsTable;

void ms_table_init(MsTable *table);
void ms_table_free(MsTable *table);
bool ms_table_set(MsTable *table, MsString *key, MsValue value);
bool ms_table_get(MsTable *table, MsString *key, MsValue *out_value);
bool ms_table_remove(MsTable *table, MsString *key);
void ms_table_add_all(MsTable *from, MsTable *to);
MsString *ms_table_find_string(MsTable *table, const char *chars,
			       int length, uint32_t hash);
void ms_table_remove_white(MsTable *table);
void ms_table_mark(MsTable *table);

#endif
