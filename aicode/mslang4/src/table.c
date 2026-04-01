#include "table.h"
#include "memory.h"
#include <string.h>

static MsTableEntry *ms_table_find_entry(MsTableEntry *entries, int capacity,
					 MsString *key)
{
	uint32_t index = key->hash % capacity;
	MsTableEntry *tombstone = NULL;
	for (;;) {
		MsTableEntry *entry = &entries[index];
		if (entry->key == NULL) {
			if (ms_is_nil(entry->value)) {
				return tombstone != NULL ? tombstone : entry;
			} else {
				if (tombstone == NULL)
					tombstone = entry;
			}
		} else if (entry->key == key) {
			return entry;
		}
		index = (index + 1) % capacity;
	}
}

static void ms_table_adjust_capacity(MsTable *table, int new_capacity)
{
	MsTableEntry *new_entries = MS_ALLOCATE(MsTableEntry, new_capacity);
	for (int i = 0; i < new_capacity; i++) {
		new_entries[i].key = NULL;
		new_entries[i].value = ms_nil_val();
	}
	table->count = 0;
	for (int i = 0; i < table->capacity; i++) {
		MsTableEntry *entry = &table->entries[i];
		if (entry->key == NULL)
			continue;
		MsTableEntry *dest = ms_table_find_entry(new_entries,
							 new_capacity,
							 entry->key);
		dest->key = entry->key;
		dest->value = entry->value;
		table->count++;
	}
	MS_FREE_ARRAY(MsTableEntry, table->entries, table->capacity);
	table->entries = new_entries;
	table->capacity = new_capacity;
}

void ms_table_init(MsTable *table)
{
	table->entries = NULL;
	table->count = 0;
	table->capacity = 0;
}

void ms_table_free(MsTable *table)
{
	MS_FREE_ARRAY(MsTableEntry, table->entries, table->capacity);
	ms_table_init(table);
}

bool ms_table_set(MsTable *table, MsString *key, MsValue value)
{
	if (table->count + 1 > table->capacity * MS_TABLE_MAX_LOAD) {
		int capacity = MS_GROW_CAPACITY(table->capacity);
		ms_table_adjust_capacity(table, capacity);
	}
	MsTableEntry *entry = ms_table_find_entry(table->entries,
						  table->capacity, key);
	bool is_new_key = entry->key == NULL;
	if (is_new_key)
		table->count++;
	entry->key = key;
	entry->value = value;
	return is_new_key;
}

bool ms_table_get(MsTable *table, MsString *key, MsValue *out_value)
{
	if (table->count == 0)
		return false;
	MsTableEntry *entry = ms_table_find_entry(table->entries,
						  table->capacity, key);
	if (entry->key == NULL)
		return false;
	*out_value = entry->value;
	return true;
}

bool ms_table_remove(MsTable *table, MsString *key)
{
	if (table->count == 0)
		return false;
	MsTableEntry *entry = ms_table_find_entry(table->entries,
						  table->capacity, key);
	if (entry->key == NULL)
		return false;
	entry->key = NULL;
	entry->value = ms_bool_val(true);
	table->count--;
	return true;
}

MsString *ms_table_find_string(MsTable *table, const char *chars, int length,
			       uint32_t hash)
{
	if (table->count == 0)
		return NULL;
	uint32_t index = hash % table->capacity;
	for (;;) {
		MsTableEntry *entry = &table->entries[index];
		if (entry->key == NULL) {
			if (ms_is_nil(entry->value))
				return NULL;
		} else if (entry->key->length == length &&
			   entry->key->hash == hash &&
			   memcmp(entry->key->chars, chars, length) == 0) {
			return entry->key;
		}
		index = (index + 1) % table->capacity;
	}
}

void ms_table_add_all(MsTable *from, MsTable *to)
{
	for (int i = 0; i < from->capacity; i++) {
		MsTableEntry *entry = &from->entries[i];
		if (entry->key != NULL)
			ms_table_set(to, entry->key, entry->value);
	}
}

void ms_table_remove_white(MsTable *table)
{
	for (int i = 0; i < table->capacity; i++) {
		MsTableEntry *entry = &table->entries[i];
		if (entry->key != NULL && !entry->key->base.isMarked)
			ms_table_remove(table, entry->key);
	}
}

void ms_table_mark(MsTable *table)
{
	for (int i = 0; i < table->capacity; i++) {
		MsTableEntry *entry = &table->entries[i];
		if (entry->key != NULL)
			entry->key->base.isMarked = true;
	}
}
