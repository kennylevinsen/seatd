#ifndef _SEATD_LIST_H
#define _SEATD_LIST_H

#include <stddef.h>

struct list {
	size_t capacity;
	size_t length;
	void **items;
};

void list_init(struct list *);
void list_free(struct list *list);
void list_add(struct list *list, void *item);
void list_insert(struct list *list, size_t index, void *item);
void list_del(struct list *list, size_t index);
void list_concat(struct list *list, struct list *source);
void list_truncate(struct list *list);
void *list_pop_front(struct list *list);
void *list_pop_back(struct list *list);
size_t list_find(struct list *list, const void *item);

#endif
