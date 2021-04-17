#include "buddy.h"
#include<stdio.h>
#include<math.h>


BuddyManager* BuddyInit(void* mem, unsigned blocks)
{
	int num_of_blocks = ceil(sizeof(BuddyManager)/ (double)BLOCK_SIZE);
	if (blocks < num_of_blocks+1)
	{
		printf("Premalo bloka da bi se napravio buddy sistem.\n");
		exit(-1);
	}
	if (BLOCK_SIZE < sizeof(BuddyBlock*))
	{
		printf("Velicina bloka je premala.\n");
		exit(-2);
	}
	BuddyManager* manager = (unsigned)mem + (blocks - num_of_blocks)*BLOCK_SIZE;
	manager->start = mem;
	int i;
	for (i = 0; pow(2, i) < blocks; i++);
	manager->biggestBlockIndex = i;
	manager->freeBlocks = 0;
	for (i = 0; i < NUM_LISTS; i++)
		manager->lists[i] = NULL;
	for (i = 0; i < blocks- num_of_blocks; i++)
		BuddyFree(manager, (unsigned)mem + BLOCK_SIZE * i, 1);
	return manager;
}

void BuddyFree(BuddyManager* manager, BuddyBlock* mem, unsigned numBlocks)
{
	int pow2 = 0;
	for (pow2 = 0; pow(2, pow2) < numBlocks; pow2++);
	manager->freeBlocks += pow(2,pow2);
	BuddyCombine(manager, mem, pow2);
}

void BuddyCombine(BuddyManager* manager, BuddyBlock* mem, unsigned index)
{
	unsigned position = ((unsigned)mem - (unsigned)manager->start) / BLOCK_SIZE;
	unsigned isLowerBlock = (position & (unsigned)pow(2, index)) >> index;
	BuddyBlock* buddy;
	if (!isLowerBlock)
		buddy = (unsigned)mem + (unsigned)pow(2, index) * BLOCK_SIZE;
	else
		buddy = (unsigned)mem - (unsigned)pow(2, index) * BLOCK_SIZE;
	BuddyBlock* cur = manager->lists[index];
	BuddyBlock* prev = NULL;
	while (cur)
	{
		if (cur == buddy)
		{
			if (!prev)
				manager->lists[index] = cur->next;
			else
				prev->next = cur->next;
			BuddyCombine(manager, buddy < mem ? buddy : mem, index + 1);
			return;
		}
		prev = cur;
		cur = cur->next;
	}
	if (manager->lists[index] == NULL)
		mem->next = NULL;
	else
		mem->next = manager->lists[index];
	manager->lists[index] = mem;
}

BuddyBlock* BuddyAlloc(BuddyManager* manager, unsigned numBlocks)
{
	BuddyBlock* ret = NULL;
	int targetPow2 = 0;
	for (targetPow2 = 0; pow(2, targetPow2) < numBlocks; targetPow2++);
	int firstPowFree = targetPow2;
	while (manager->lists[firstPowFree] == NULL && firstPowFree != manager->biggestBlockIndex)
	{
		firstPowFree++;
	}
	if (firstPowFree == manager->biggestBlockIndex)
		return ret;
	manager->freeBlocks -= pow(2, targetPow2);
	if (firstPowFree == targetPow2)
	{
		ret = manager->lists[targetPow2];
		manager->lists[targetPow2] = manager->lists[targetPow2]->next;
		return ret;
	}
	ret = manager->lists[firstPowFree];
	manager->lists[firstPowFree] = manager->lists[firstPowFree]->next;
	for (; firstPowFree != targetPow2; firstPowFree--)
	{
		BuddyBlock* tmp = ret;
		ret = (unsigned)tmp + (unsigned)pow(2, firstPowFree - 1) * BLOCK_SIZE;
		if (manager->lists[firstPowFree - 1] == NULL)
			tmp->next = NULL;
		else
			tmp->next = manager->lists[firstPowFree - 1];
		manager->lists[firstPowFree - 1] = tmp;
	}
	return ret;
}

void printBuddy(BuddyManager* manager)
{
	for (int i = 0; i < manager->biggestBlockIndex; i++)
	{
		BuddyBlock* cur = manager->lists[i];
		printf("Blocks of size %d:\n", (unsigned)pow(2, i));
		while (cur)
		{
			unsigned j = ((unsigned)cur - (unsigned)manager->start) / BLOCK_SIZE;
			printf("%u : %u\n", j, cur);
			cur = cur->next;
		}
	}
}