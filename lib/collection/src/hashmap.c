#include <string.h>
#include <util/hashmap.h>

#define HASHMAP_THRESHOLD(cap)	(((cap) >> 1) + ((cap) >> 2))	// 75%

static void _destroy(HashMap* this) {
	for(size_t i = 0; i < this->capacity; i++) {
		LinkedList* list = this->table[i];
		if(!list)
			continue;

		Iterator* iter = list->iter;
		LinkedListIterContext context;
		iter->init(&context, list);
		while(iter->has_next(&context)) {
			MapEntry* entry = iter->next(&context);
			list->free(entry);
		}

		linkedlist_destroy(list);
	}

	this->free(this->table);
}

static void* get(HashMap* this, void* key) {
	size_t index = this->hash(key) % this->capacity;
	LinkedList* list = this->table[index];
	if(!list)
		return NULL;

	size_t size = list->size;
	for(size_t i = 0; i < size; i++) {
		MapEntry* entry = list->get(list, i);
		if(this->equals(entry->key, key))
			return entry->value;
	}

	return NULL;
}

static bool put(HashMap* this, void* key, void* value) {
	if(this->size + 1 > this->threshold) {
		// Create new this
		HashMap* map = hashmap_create(this->type, this->pool,
				this->capacity * 2);
		if(!map)
			return false;

		// Copy
		MapEntry* entry;
		for_each(entry, this->entry_set) {
			if(!map->put(map, entry->key, entry->value)) {
				_destroy(map);
				return false;
			}
		}

		// Destory
		_destroy(this);

		// Paste
		memcpy(this, map, sizeof(HashMap));
		map_destroy((Map*)map);
	}

	size_t index = this->hash(key) % this->capacity;
	LinkedList* list = this->table[index];
	if(!list) {
		list = linkedlist_create(this->type, this->pool);
		if(!list)
			return false;

		this->table[index] = list;
	} else {
		size_t size = list->size;
		for(size_t i = 0; i < size; i++) {
			MapEntry* entry = list->get(list, i);
			if(this->equals(entry->key, key))
				return false;
		}
	}

	MapEntry* entry = this->malloc(sizeof(MapEntry));
	if(!entry) {
		if(list->is_empty(list))
			linkedlist_destroy(list);

		return false;
	}

	entry->key = key;
	entry->value = value;

	if(!list->add(list, entry)) {
		this->free(entry);

		if(list->is_empty(list))
			linkedlist_destroy(list);

		return false;
	}
	this->size++;

	return true;
}

static bool update(HashMap* this, void* key, void* value) {
	size_t index = this->hash(key) % this->capacity;
	LinkedList* list = this->table[index];
	if(!list)
		return false;

	size_t size = list->size;
	for(size_t i = 0; i < size; i++) {
		MapEntry* entry = list->get(list, i);
		if(this->equals(entry->key, key)) {
			entry->value = value;
			return true;
		}
	}

	return false;
}

static void* _remove(HashMap* this, void* key) {
	size_t index = this->hash(key) % this->capacity;
	LinkedList* list = this->table[index];
	if(!list)
		return NULL;

	size_t size = list->size;
	for(size_t i = 0; i < size; i++) {
		MapEntry* entry = list->get(list, i);
		if(this->equals(entry->key, key)) {
			void* value = entry->value;
			list->remove_at(list, i);
			this->free(entry);

			if(list->is_empty(list)) {
				linkedlist_destroy(list);
				this->table[index] = NULL;
			}
			this->size--;

			return value;
		}
	}

	return NULL;
}

static bool contains_key(HashMap* this, void* key) {
	return get(this, key) != NULL;
}

static bool contains_value(HashMap* this, void* value) {
	for(size_t index = 0; index < this->capacity; index++) {
		LinkedList* list = this->table[index];
		if(!list)
			continue;

		if(list->index_of(list, value) >= 0)
			return true;
	}
	return false;
}

static void iterator_init(MapIterContext* context, EntrySet* set) {
	HashMap* map = context->_map = set->map;
	context->index = 0;
	while(context->index < map->capacity && !map->table[context->index])
		context->index++;

	context->list_index = 0;
}

static bool iterator_has_next(MapIterContext* context) {
	HashMap* map = context->_map;
	if(context->index >= map->capacity)
		return false;

	LinkedList* list = map->table[context->index];
	if(list && context->list_index < list->size)
		return true;

	context->index++;
	while(context->index < map->capacity && !map->table[context->index])
		context->index++;

	if(context->index < map->capacity) {
		context->list_index = 0;
		return true;
	} else {
		return false;
	}
}

static void* entry_iterator_next(MapIterContext* context) {
	HashMap* map = context->_map;
	LinkedList* list = map->table[context->index];
	MapEntry* entry = list->get(list, context->list_index++);

	context->entry.key = entry->key;
	context->entry.value = entry->value;

	return &context->entry;
}

static void* iterator_remove(MapIterContext* context) {
	HashMap* map = context->_map;
	context->list_index--;

	LinkedList* list = map->table[context->index];
	MapEntry* entry = list->remove_at(list, context->list_index);
	context->entry.key = entry->key;
	context->entry.value = entry->value;
	map->free(entry);

	if(list->is_empty(list)) {
		linkedlist_destroy(list);
		list = NULL;
	}

	map->size--;

	return &context->entry;
}

static void* key_iterator_next(MapIterContext* context) {
	MapEntry* entry = entry_iterator_next(context);
	if(!entry)
		return NULL;

	return entry->key;
}

static void* value_iterator_next(MapIterContext* context) {
	MapEntry* entry = entry_iterator_next(context);
	if(!entry)
		return NULL;

	return entry->value;
}

static Iterator entry_iterator = {
	.init		= (void*)iterator_init,
	.has_next	= (void*)iterator_has_next,
	.next		= (void*)entry_iterator_next,
	.remove		= (void*)iterator_remove,
};

static Iterator key_iterator = {
	.init		= (void*)iterator_init,
	.has_next	= (void*)iterator_has_next,
	.next		= (void*)key_iterator_next,
	.remove		= (void*)iterator_remove,
};

static Iterator value_iterator = {
	.init		= (void*)iterator_init,
	.has_next	= (void*)iterator_has_next,
	.next		= (void*)value_iterator_next,
	.remove		= (void*)iterator_remove,
};

static inline EntrySet* entryset_create(HashMap* this, Iterator* iter) {
	EntrySet* set = this->malloc(sizeof(EntrySet));
	if(!set)
		return NULL;

	set->map = this;
	set->iter = iter;
	return set;
}

static inline void entryset_destroy(HashMap* this) {
	if(this->entry_set)
		this->free(this->entry_set);
	if(this->key_set)
		this->free(this->key_set);
	if(this->value_set)
		this->free(this->value_set);
}

HashMap* hashmap_create(DataType type, PoolType pool, size_t initial_capacity) {
	HashMap* map = (HashMap*)map_create(type, pool, sizeof(HashMap));
	if(!map)
		return NULL;

	size_t capacity = 1;
	while(capacity < initial_capacity)
		capacity <<= 1;

	map->table = map->malloc(sizeof(LinkedList*) * capacity);
	if(!map->table)
		goto failed;

	memset(map->table, 0x0, sizeof(LinkedList*) * capacity);

	map->capacity		= capacity;
	map->threshold		= HASHMAP_THRESHOLD(capacity);

	map->get		= (void*)get;
	map->put		= (void*)put;
	map->update		= (void*)update;
	map->remove		= (void*)_remove;
	map->contains_key	= (void*)contains_key;
	map->contains_value	= (void*)contains_value;

	map->entry_set		= entryset_create(map, &entry_iterator);
	map->key_set		= entryset_create(map, &key_iterator);
	map->value_set		= entryset_create(map, &value_iterator);
	if(!map->entry_set || !map->key_set || !map->value_set)
		goto failed;

	return map;

failed:
	entryset_destroy(map);
	map->free(map);
	return NULL;
}

void hashmap_destroy(HashMap* this) {
	_destroy(this);
	entryset_destroy(this);
        map_destroy((Map*)this);
}
