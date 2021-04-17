#pragma once
#include"slab.h"

#define NUM_LISTS 53

typedef union block {

	union block* next;
	char data[BLOCK_SIZE];

}BuddyBlock;

typedef struct buddyM {

	BuddyBlock* lists[NUM_LISTS];
	unsigned freeBlocks;
	unsigned biggestBlockIndex;
	void* start;

}BuddyManager;

BuddyManager* BuddyInit(void* mem, unsigned blocks);
BuddyBlock* BuddyAlloc(BuddyManager* manager, unsigned numBlocks);
void BuddyFree(BuddyManager* manager, BuddyBlock* mem, unsigned numBlocks);
void BuddyCombine(BuddyManager* manager, BuddyBlock* mem, unsigned index);
void printBuddy(BuddyManager* manager);