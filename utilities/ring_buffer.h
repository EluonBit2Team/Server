#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_RING_SIZE 200              
#define MAX_RING_DATA_SIZE 2048         

typedef struct{                                
  int sz_data;                            
  char data[MAX_RING_DATA_SIZE];        
} rign_item_t;    

typedef struct{
  int tag_head;                         
  int tag_tail;                        
  rign_item_t item[MAX_RING_SIZE];       
} ring_t; 


#endif