#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <err.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include "net.h"
#include "jbod.h"

/* the client socket descriptor for the connection to the server */
int cli_sd = -1;

/* attempts to read n (len) bytes from fd; returns true on success and false on failure. 
It may need to call the system call "read" multiple times to reach the given size len. 
*/
static bool nread(int fd, int len, uint8_t *buf) {
  int temp;
  int read_len = len;
  int pos = 0;
  while (read_len != 0){
    temp = read(fd, &buf[pos], read_len);
    if (temp == -1){
      return false;
    }
    read_len -= temp;
    pos += temp;
  }
  return true;
}

/* attempts to write n bytes to fd; returns true on success and false on failure 
It may need to call the system call "write" multiple times to reach the size len.
*/
static bool nwrite(int fd, int len, uint8_t *buf) {
  int temp;
  int write_len = len;
  int pos = 0;
  while (write_len != 0){
    temp = write(fd, &buf[pos], write_len);
    if (temp == -1){
      return false;
    }
    write_len -= temp;
    pos += temp;
  }
  return true;
}


/* Through this function call the client attempts to receive a packet from sd 
(i.e., receiving a response from the server.). It happens after the client previously 
forwarded a jbod operation call via a request message to the server.  
It returns true on success and false on failure. 
The values of the parameters (including op, ret, block) will be returned to the caller of this function: 

op - the address to store the jbod "opcode"  
ret - the address to store the info code (lowest bit represents the return value of the server side calling the corresponding jbod_operation function. 2nd lowest bit represent whether data block exists after HEADER_LEN.)
block - holds the received block content if existing (e.g., when the op command is JBOD_READ_BLOCK)

In your implementation, you can read the packet header first (i.e., read HEADER_LEN bytes first), 
and then use the length field in the header to determine whether it is needed to read 
a block of data from the server. You may use the above nread function here.  
*/
static bool recv_packet(int sd, uint32_t *op, uint8_t *ret, uint8_t *block) {
  uint8_t header[HEADER_LEN];
  if (nread(sd, HEADER_LEN, header) == false){
    return false;
  }
  *op = (header[3] << 24) | (header[2] << 16) | (header[1] << 8) | header[0];
  *ret = header[4];
  if (*ret > 1){
    uint8_t buf[JBOD_BLOCK_SIZE];
    if (nread(sd, JBOD_BLOCK_SIZE, buf) == false){
      return false;
    }
    for (int i=0; i<JBOD_BLOCK_SIZE; i++){
      block[i] = buf[i];
    }
  }
  return true;
}



/* The client attempts to send a jbod request packet to sd (i.e., the server socket here); 
returns true on success and false on failure. 

op - the opcode. 
block- when the command is JBOD_WRITE_BLOCK, the block will contain data to write to the server jbod system;
otherwise it is NULL.

The above information (when applicable) has to be wrapped into a jbod request packet (format specified in readme).
You may call the above nwrite function to do the actual sending.  
*/
static bool send_packet(int sd, uint32_t op, uint8_t *block) {
  int buf_len;
  uint8_t op_bytes[4];
  op_bytes[3] = op & 0xFF;
  op_bytes[2] = (op >> 8) & 0xFF;
  op_bytes[1] = (op >> 16) & 0xFF;
  op_bytes[0] = (op >> 24) & 0xFF;
  if (block != NULL){
    buf_len = JBOD_BLOCK_SIZE+HEADER_LEN;
  } else{
    buf_len = HEADER_LEN;
  }
  uint8_t buf[buf_len];
  for (int i=0; i<4; i++){
    buf[i] = op_bytes[i];
  }
  if (block != NULL){
    buf[4] = 2;
    for (int i=0; i<JBOD_BLOCK_SIZE; i++){
      buf[i+HEADER_LEN] = block[i];
    }
  } else{
    buf[4] = 0;
  }
  if(nwrite(sd, buf_len, buf) == false){
    return false;
  }
  return true;
}



/* attempts to connect to server and set the global cli_sd variable to the
 * socket; returns true if successful and false if not. 
 * this function will be invoked by tester to connect to the server at given ip and port.
 * you will not call it in mdadm.c
*/
bool jbod_connect(const char *ip, uint16_t port) {
  cli_sd = socket(AF_INET, SOCK_STREAM, 0);
  if (cli_sd == -1){
    return false;
  }
  struct sockaddr_in caddr;
  caddr.sin_family = AF_INET;
  caddr.sin_port = htons(port);
  if (inet_aton(ip, &caddr.sin_addr) == 0){
    return false;
  }
  if (connect(cli_sd, (const struct sockaddr *)&caddr, sizeof(caddr)) == -1){
    return false;
  }
  return true;
}




/* disconnects from the server and resets cli_sd */
void jbod_disconnect(void) {
  close(cli_sd);
  cli_sd = -1;
}



/* sends the JBOD operation to the server (use the send_packet function) and receives 
(use the recv_packet function) and processes the response. 

The meaning of each parameter is the same as in the original jbod_operation function. 
return: 0 means success, -1 means failure.
*/
int jbod_client_operation(uint32_t op, uint8_t *block) {
  if (cli_sd != -1){
    if (send_packet(cli_sd, op, block) == false){
      return -1;
    }
    uint8_t ret[1];
    uint32_t temp_op[1];
    if (recv_packet(cli_sd, temp_op, ret, block) == false){
      return -1;
    }
    if (ret[0] == 1) {
      return -1;
    }
    return 0;
  }
  return -1;
}
