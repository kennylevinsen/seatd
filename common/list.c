#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "list.h"

void list_init(struct list *list) {
	list->capacity = 10;
	list->length = 0;
	list->items = malloc(sizeof(void *) * list->capacity);
}

static void list_resize(struct list *list) {
	if (list->length == list->capacity) {
		list->capacity *= 2;
		list->items = realloc(list->items, sizeof(void *) * list->capacity);
	}
}

void list_free(struct list *list) {
	list->capacity = 0;
	list->length = 0;
	free(list->items);
}

void list_add(struct list *list, void *item) {
	list_resize(list);
	list->items[list->length++] = item;
}

void list_insert(struct list *list, size_t index, void *item) {
	list_resize(list);
	memmove(&list->items[index + 1], &list->items[index],
		sizeof(void *) * (list->length - index));
	list->length++;
	list->items[index] = item;
}

void list_del(struct list *list, size_t index) {
	list->length--;
	memmove(&list->items[index], &list->items[index + 1],
		sizeof(void *) * (list->length - index));
}

size_t list_find(struct list *list, const void *item) {
	for (size_t i = 0; i < list->length; i++) {
		if (list->items[i] == item) {
			return i;
		}
	}
	return -1;
}

void list_concat(struct list *list, struct list *source) {
	if (list->length + source->length > list->capacity) {
		while (list->length + source->length > list->capacity) {
			list->capacity *= 2;
		}
		list->items = realloc(list->items, sizeof(void *) * list->capacity);
	}
	memmove(&list->items[list->length], source->items, sizeof(void *) * (source->length));
	list->length += source->length;
}

void list_truncate(struct list *list) {
	list->length = 0;
}

void *list_pop_front(struct list *list) {
	if (list->length == 0) {
		return NULL;
	}
	void *item = list->items[0];
	list_del(list, 0);
	return item;
}
