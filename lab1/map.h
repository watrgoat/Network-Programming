#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAP_SIZE 1024

typedef struct Entry {
    char *key;
    void *value;
    struct Entry *next;
} Entry;

typedef struct {
    Entry *buckets[MAP_SIZE];
} HashMap;

unsigned int hash(const char *key)
{
    unsigned int h = 0;

    while (*key)
        h = h * 31 + *key++;

    return h % MAP_SIZE;
}

void map_put(HashMap *map, char *key, void *value)
{
    unsigned int i = hash(key);

    Entry *e = (Entry *)malloc(sizeof(Entry));
    e->key = strdup(key);
    e->value = value;

    e->next = map->buckets[i];
    map->buckets[i] = e;
}

void *map_get(HashMap *map, const char *key)
{
    unsigned int i = hash(key);

    Entry *e = map->buckets[i];

    while (e) {
        if (strcmp(e->key, key) == 0)
            return e->value;

        e = e->next;
    }

    return NULL;
}
