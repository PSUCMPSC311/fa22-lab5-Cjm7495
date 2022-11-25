#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cache.h"
#include "jbod.h"
#include "mdadm.h"
#include "net.h"

int mdadm_mount(void) {
  // moves the bits to the correct position for the mount command and uses the driver function to execute the command
  int temp = jbod_client_operation(JBOD_MOUNT << 12, NULL);
  if (temp == 0){
    return 1;
  } else {
    return -1;
  }
}

int mdadm_unmount(void) {
  // moves the bits to the correct position for the unmount command and uses the driver function to execute the command
  int temp = jbod_client_operation(JBOD_UNMOUNT << 12, NULL);
  if (temp == 0){
    return 1;
  } else {
    return -1;
  }
}

int mdadm_write_permission(void){
  // moves the bits to the correct position for the write permission command and uses the driver function to execute the command
  int temp = jbod_client_operation(JBOD_WRITE_PERMISSION << 12, NULL);
  if (temp == 0){
    return 1;
  } else {
    return -1;
  }
}


int mdadm_revoke_write_permission(void){
  // moves the bits to the correct position for the revoke write permission command and uses the driver function to execute the command
  int temp = jbod_client_operation(JBOD_REVOKE_WRITE_PERMISSION << 12, NULL);
  if (temp == 0){
    return 1;
  } else {
    return -1;
  }
}


int mdadm_read(uint32_t start_addr, uint32_t read_len, uint8_t *read_buf) {
  // makes sure the inputs are valid
  if (read_len <= 2048 && mdadm_mount() == -1 && start_addr + read_len <= JBOD_DISK_SIZE*JBOD_NUM_DISKS){
    if (read_len == 0 && read_buf == NULL){
      return 0;
    } else if (read_buf == NULL){
      return -1;
    }
    int result;
    int bytes_left = read_len;
    int disk_start  = (0xF0000 & start_addr) >> 16;
    uint8_t block[JBOD_BLOCK_SIZE];
    // loops through the disks starting at the starting disk
    for (int i = disk_start; i < JBOD_NUM_DISKS; i++){
      // moves to the current disk by using the seek to disk command along with the address of the current disk
      result = jbod_client_operation((JBOD_SEEK_TO_DISK << 12) | (i << 8), NULL);
      if (result == -1){
	return -1;
      }
      // starts at the starting block address if the start disk is the current disk, starts at 0 otherwise
      int block_start;
      if (i == disk_start){
	block_start = (0x0FF00 & start_addr) >> 8;
      } else {
	block_start = 0;
      }
      // uses the seek to block command to set the current block
      result = jbod_client_operation((JBOD_SEEK_TO_BLOCK << 12) | block_start, NULL);
      if (result == -1){
	return -1;
       }
      // loops through the blocks of the current disk starting at "block_start"
      for (int j = block_start; j < JBOD_NUM_BLOCKS_PER_DISK; j++){
	// checks the cache for the current block in the current disk
	result = cache_lookup(i, j, block);
	bool next_block = false;
	if (result == 1){
	  next_block = true;
	} else {
	  // uses the read block command to read the current block to "block"
	  result = jbod_client_operation(JBOD_READ_BLOCK << 12, block);
	  if (result == -1){
	    return -1;
	  }
	  // inserts the current block into the cache
	  cache_insert(i, j, block);
	}
	// starts at the starting byte address if the start block is the current block and the start disk is the current disk
	int byte_start;
	if (j == block_start && i == disk_start){
	  byte_start = start_addr & 0x000FF;
	} else {
	  byte_start = 0;
	}
	// sets the current block to the next block if the cache is used
	if (next_block) {
	  jbod_client_operation((JBOD_SEEK_TO_BLOCK << 12) | (j+1), NULL);
	}
	// loops through the bytes in "block" and inserts them into "read_buf"
	for (int k = byte_start; k < JBOD_BLOCK_SIZE; k++){
	  read_buf[read_len-bytes_left] = block[k];
	  bytes_left -= 1;
	  // ends the read if there are no bytes left to read
	  if (bytes_left == 0){
	    return read_len;
	  }
	}
      }
    }
    return read_len;
  } else {
    return -1;
  }
}

int mdadm_write(uint32_t start_addr, uint32_t write_len, const uint8_t *write_buf) {
  // makes sure the inputs are valid
  if (write_len <= 2048 && start_addr + write_len <= JBOD_DISK_SIZE*JBOD_NUM_DISKS && mdadm_write_permission() == -1){
    if (write_len == 0 && write_buf == NULL){
      return 0;
    } else if (write_buf == NULL){
      return -1;
    }
    int result;
    int bytes_left = write_len;
    int disk_start  = (0xF0000 & start_addr) >> 16;
    uint8_t write_block[JBOD_BLOCK_SIZE];
    // loops through the disks starting at the starting disk
    for (int i = disk_start; i < JBOD_NUM_DISKS; i++){
      // starts at the starting block address if the start disk is the current disk, starts at 0 otherwise
      int block_start;
      if (i == disk_start){
	block_start = (0x0FF00 & start_addr) >> 8;
      } else {
	block_start = 0;
      }
      // loops through the blocks of the current disk starting at "block_start"
      for (int j = block_start; j < JBOD_NUM_BLOCKS_PER_DISK; j++){
	// starts at the starting byte address if the start block is the current block and the start disk is the current disk
	int byte_start;
	if (j == block_start && i == disk_start){
	  byte_start = start_addr & 0x000FF;
	} else {
	  byte_start = 0;
	}
        // sets "write_block" equal to the current block
	uint32_t current_addr = (i << 16) | (j << 8);
	result = mdadm_read(current_addr, JBOD_BLOCK_SIZE, write_block);
	if (result == -1){
	  return -1;
	}
	// uses the seek to disk command to set the current disk
	result = jbod_client_operation((JBOD_SEEK_TO_DISK << 12) | (i << 8), NULL);
        if (result == -1){
	  return -1;
        }
	// uses the seek to block command to set the current block
	result = jbod_client_operation((JBOD_SEEK_TO_BLOCK << 12) | j, NULL);
        if (result == -1){
	  return -1;
        }
	// loops through the bytes in "write_buf" and inserts them into "write_block" in the correct positions
	for (int k = byte_start; k < JBOD_BLOCK_SIZE; k++){
	  write_block[k] = write_buf[write_len-bytes_left];
	  bytes_left -= 1;
	  // stops looping if there are no bytes left
	  if (bytes_left == 0){
	    break;
	  }
	}
	// writes "write_block" to the current block
	result = jbod_client_operation(JBOD_WRITE_BLOCK << 12, write_block);
	if (result == -1){
	  return -1;
	}
	// inserts "write_block" into the cache
	cache_insert(i, j, write_block);
	// stops writing if there are no bytes left
	if (bytes_left == 0){
	  return write_len;
	}
      }
    }
    return write_len;
  } else {
    return -1;
  }
}
