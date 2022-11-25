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
  printf("READING %d\n", len);//deb
  int temp = 0;
  int read_len = len;
  while (read_len != 0){
    read_len -= temp;
    temp = read(fd, buf, read_len);
    if (temp == -1){
      return false;
    }
  }
  printf("READING SUCCESS");//deb
  return true;
}

/* attempts to write n bytes to fd; returns true on success and false on failure 
It may need to call the system call "write" multiple times to reach the size len.
*/
static bool nwrite(int fd, int len, uint8_t *buf) {
  printf("WRITING\n");//deb
  int temp = 0;
  int write_len = len;
  while (write_len != 0){
    write_len -= temp;
    temp = write(fd, buf, write_len);
    if (temp == -1){
      return false;
    }
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
  printf("RECEIVING\n");//deb
  uint8_t op_bytes[4];
  if (nread(sd, 4, op_bytes) == false){
    return false;
  }
  if (nread(sd, 1, ret) == false){
    return false;
  }
  if (*ret > 1){
    if (nread(sd, JBOD_BLOCK_SIZE, block) == false){
      return false;
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
  printf("SENDING\n");//deb
  uint8_t buf = 0;
  uint8_t op_bytes[4];
  op_bytes[0] = op & 0x000F;
  op_bytes[1] = (op & 0x00F0) >> 8;
  op_bytes[2] = (op & 0x0F00) >> 16;
  op_bytes[3] = (op & 0xF000) >> 24;
  if (nwrite(buf, 1, op_bytes) == false){
    return false;
  }
  uint8_t *temp = 0;
  if (block != NULL){
    *temp = 2;
  }
  if (nwrite(buf, 1, temp) == false){
    return false;
  }
  if (block != NULL){
    if (nwrite(buf, JBOD_BLOCK_SIZE, block) == false){
      return false;
    }
  }
  uint8_t *bufp = &buf;
  if(nwrite(sd, JBOD_BLOCK_SIZE+HEADER_LEN, bufp) == false){
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
  printf("CONNECTING\n");//deb
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
  printf("DISCONNECTING\n");//deb
  close(cli_sd);
  cli_sd = -1;
}



/* sends the JBOD operation to the server (use the send_packet function) and receives 
(use the recv_packet function) and processes the response. 

The meaning of each parameter is the same as in the original jbod_operation function. 
return: 0 means success, -1 means failure.
*/
int jbod_client_operation(uint32_t op, uint8_t *block) {
  printf("OPERATION\n");//deb
  if (cli_sd != -1){
    send_packet(cli_sd, op, block);
    uint32_t *op = 0;
    uint8_t *ret = 0;
    recv_packet(cli_sd, op, ret, block);
    if ((*ret & 1) == 1) {
      return -1;
    }
    return 0;
  }
  return -1;
}
