#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "cache.h"
#include "jbod.h"

static cache_entry_t *cache = NULL;
static int cache_size = 0;
static int num_queries = 0;
static int num_hits = 0;

int cache_create(int num_entries) {
  // allocates space for the cache and sets all values to 0 if there is more than 1 entry and less than 4097 entries and the cache is not already enabled
  if (num_entries <= 4096 && num_entries >= 2 && !cache_enabled()){
    cache_size = num_entries;
    cache = calloc(num_entries, sizeof(cache_entry_t));
    return 1;
  }
  return -1;
}

int cache_destroy(void) {
  // frees "cache", sets "cache" to NULL, and sets "cache_size" to 0 if the cache is enabled
  if (cache_enabled()){
    free(cache);
    cache = NULL;
    cache_size = 0;
    return 1;
  }
  return -1;
}

int cache_lookup(int disk_num, int block_num, uint8_t *buf) {
  // makes sure that the cache is enabled and "buf" is not NULL
  if (cache_enabled() && buf != NULL){
    num_queries++;
    // loops through the cache looking for an entry with the same "disk_num" and "block_num"
    for (int i=0; i < cache_size; i++){
      if (cache[i].disk_num == disk_num && cache[i].block_num == block_num && cache[i].valid == true){
	// copies the cache block into "buf" if a matching entry is found
        for (int j=0; j < JBOD_BLOCK_SIZE; j++){
	  buf[j] = cache[i].block[j];
	}
	cache[i].num_accesses++;
	num_hits++;
	return 1;
      }
    }
  }
  return -1;
}

void cache_update(int disk_num, int block_num, const uint8_t *buf) {
  for (int i=0; i < cache_size; i++){
    // loops through the cache looking for an entry with the same "disk_num" and "block_num"
    if (cache[i].disk_num == disk_num && cache[i].block_num == block_num && cache[i].valid == true){
      cache[i].num_accesses++;
      // copies "buf" into the "block" value of "cache"
      for (int j=0; j < JBOD_BLOCK_SIZE; j++){
	cache[i].block[j] = buf[j];
      }
    }
  }
}

void replace_cache_entry(int pos, int disk_num, int block_num, const uint8_t *buf){
  // replaces a cache entry by changing every value to the values of the new entry
  cache[pos].valid = true;
  cache[pos].disk_num = disk_num;
  cache[pos].block_num = block_num;
  cache[pos].num_accesses = 1;
  for (int i=0; i < JBOD_BLOCK_SIZE; i++){
    cache[pos].block[i] = buf[i];
  }
}

int cache_insert(int disk_num, int block_num, const uint8_t *buf) {
  // makes sure that the cache is enabled and that "disk_num" and "block_num" are valid
  if (buf != NULL && cache_enabled() && disk_num >= 0 && disk_num <= JBOD_NUM_DISKS && block_num >= 0 && block_num <= JBOD_NUM_BLOCKS_PER_DISK){
    // checks if the entry already exists and updates it if it does
    for (int i=0; i < cache_size; i++){
      // loops through the cache looking for an entry with the same "disk_num" and "block_num"
      if (cache[i].disk_num == disk_num && cache[i].block_num == block_num && cache[i].valid == true){
	cache_update(disk_num, block_num, buf);
	return -1;
      }
    }
    // least_accessed[0] = number of accesses of the least accesed entry
    // least_accessed[1] = position of the entry in the cache
    int least_accessed[2] = {cache[0].num_accesses, 0};
    for (int i=0; i < cache_size; i++){
      // inserts the entry into an empty slot in the cache if there is one
      if (cache[i].valid == false){
        replace_cache_entry(i, disk_num, block_num, buf);
	return 1;
      }
      // finds the least accessed entry in the cache
      if (cache[i].num_accesses < least_accessed[0]){
	least_accessed[0] = cache[i].num_accesses;
	least_accessed[1] = i;
      }
    }
    replace_cache_entry(least_accessed[1], disk_num, block_num, buf);
    return 1;
  }
  return -1;
}

bool cache_enabled(void) {
  return cache != NULL && cache_size > 0;
}

void cache_print_hit_rate(void) {
  fprintf(stderr, "num_hits: %d, num_queries: %d\n", num_hits, num_queries);
  fprintf(stderr, "Hit rate: %5.1f%%\n", 100 * (float) num_hits / num_queries);
}
