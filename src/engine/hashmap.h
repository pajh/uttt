/*
 * Fixed-capacity transposition cache.  It owns preallocated bucket and entry
 * arrays so search does not allocate per node.  Like board.h, it defines
 * functions and therefore belongs in exactly one translation unit.
 */
   
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* searchKey() in ai_minimax.c owns this byte-level key format. */
#define KEY_SIZE 26
typedef uint32_t u32;

typedef struct HMEntry_s HMEntry;

typedef struct HMEntry_s {
    u32 hash;
    unsigned char key[KEY_SIZE];
    u32 data;
    HMEntry* next_entry;
} HMEntry;

typedef struct HashMap_s {

    HMEntry** bucket_buffer;
    u32 buckets_size;
    u32 buckets_used;

    HMEntry* entry_buffer;
    u32 entry_buffer_size;
    u32 entries_used;

    // Metrics
    u32 lookups;
    u32 lookup_found;
    
} HashMap;

HashMap* createHM(u32 buckets, u32 entries) {
    
    HashMap *hm = (HashMap *) calloc(1, sizeof(HashMap));

    if (!hm) { perror("hashmap allocation"); exit(1); }
    hm-> bucket_buffer = (HMEntry**) calloc( buckets, sizeof( HMEntry *) );
    hm->buckets_size = buckets;
    hm->buckets_used = 0;    

    hm-> entry_buffer = (HMEntry*) calloc( entries, sizeof( HMEntry ) );
    hm->entry_buffer_size = entries;
    hm->entries_used = 0;
    if (!hm->bucket_buffer || !hm->entry_buffer) { perror("hashmap buffers"); exit(1); }
    return hm;
}

void destroyHM(HashMap* hm) {
    free(hm->bucket_buffer);
    free(hm->entry_buffer);
    free(hm);
    hm = NULL;
    return;
}

void clearHM( HashMap* hm ) {
    
    memset( (void *)hm->bucket_buffer, 0, hm->buckets_size * sizeof( HMEntry *) );
    hm->buckets_used = 0;    

    // Does entries need to be zerod?
    hm->entries_used = 0;
    hm->lookups = 0;
    hm->lookup_found = 0;
}

void printMetrics(HashMap* hm) {
    fprintf(stderr,"Buckets %d/%d | Entries %d/%d | Cache %d/%d\n", hm->buckets_used, hm->buckets_size, hm->entries_used, hm->entry_buffer_size, hm->lookup_found, hm->lookups);
}

#define HASHMAP_HASH_INIT 2166136261u
static inline uint32_t hash_data(const unsigned char* data )
{
	size_t nblocks = KEY_SIZE / 8;
	uint64_t hash = HASHMAP_HASH_INIT;
	for (size_t i = 0; i < nblocks; ++i)
	{
		hash ^= (uint64_t)data[0] << 0 | (uint64_t)data[1] << 8 |
			 (uint64_t)data[2] << 16 | (uint64_t)data[3] << 24 |
			 (uint64_t)data[4] << 32 | (uint64_t)data[5] << 40 |
			 (uint64_t)data[6] << 48 | (uint64_t)data[7] << 56;
		hash *= 0xbf58476d1ce4e5b9;
		data += 8;
	}

	uint64_t last = KEY_SIZE & 0xff;
	switch (KEY_SIZE % 8)
	{
	case 7:
		last |= (uint64_t)data[6] << 56; /* fallthrough */
	case 6:
		last |= (uint64_t)data[5] << 48; /* fallthrough */
	case 5:
		last |= (uint64_t)data[4] << 40; /* fallthrough */
	case 4:
		last |= (uint64_t)data[3] << 32; /* fallthrough */
	case 3:
		last |= (uint64_t)data[2] << 24; /* fallthrough */
	case 2:
		last |= (uint64_t)data[1] << 16; /* fallthrough */
	case 1:
		last |= (uint64_t)data[0] << 8;
		hash ^= last;
		hash *= 0xd6e8feb86659fd93;
	}
	// compress to a 32-bit result. also serves as a finalizer.
	return hash ^ hash >> 32;
}

void addHMEntry(HashMap *hm, unsigned char* key, u32 data ) {
    // Initial can add duplicates so be carefule FIXME

    if (hm->entries_used == hm->entry_buffer_size) return;

    u32 hash = hash_data(key);

    // Fill out next entry;
    HMEntry* my_entry = &hm->entry_buffer[hm->entries_used++];
    my_entry->data = data;
    my_entry->hash = hash;
    memcpy(my_entry->key, key, KEY_SIZE);

    // Find bucket offset
    int bucket_offset = hash % hm->buckets_size;
    //printf("Inserting at bucket offset:%d\n", bucket_offset);
    
    if (hm->bucket_buffer[bucket_offset] == NULL) hm->buckets_used++;
    my_entry->next_entry = hm->bucket_buffer[bucket_offset];
    hm->bucket_buffer[bucket_offset] = my_entry;    
}

int findHMEntry(HashMap* hm, unsigned char* key, u32* data) {

    u32 hash = hash_data(key);
    hm->lookups++;
    int bucket_offset = hash % hm->buckets_size;

    //printf("Find:starting at bucket %d\n", bucket_offset);

    HMEntry* current = hm->bucket_buffer[bucket_offset];
    while (current != NULL) {
        //if (current->next_entry)
        //    _mm_prefetch ( (char const*) current->next_entry, 2);
        if (current->hash == hash) {
            //printf("Trying memcmp\n");
            if (  ( memcmp(key, current->key, KEY_SIZE) == 0 ) ) {
                *data = current->data;
                hm->lookup_found++;
                return 1;
            }
        }
        current = current->next_entry;
    }
    return 0; // Not found
}
