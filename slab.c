#define _CRT_SECURE_NO_WARNINGS
#include"slab.h"
#include"buddy.h"
#include<Windows.h>
#include<stdio.h>
#include<stdlib.h>
#include<math.h>
#include<string.h>


#define NAME_SIZE 25
#define NUM_BUFFERED_CACHES 13
#define MIN_NUM_OBJECTS_IN_SLAB 4
#define MIN_POW2_BUFFERED_CACHE 5
#define MAX_POW2_BUFFERED_CACHE 17

typedef struct slot_s {

	struct slot_s* next;

}Slot;

typedef struct slab_s {

	struct slab_s* next;
	void* memStart;
	void* objectsStart;
	Slot* freeSlots;
	size_t unusedSpace;
	unsigned numOfObjects;
	unsigned freeObjects;
	kmem_cache_t* owner;

}Slab;

typedef union cacheslot CacheSlot;

typedef struct cacheBlock_s {

	unsigned numOfFreeCacheSpaces;
	unsigned cacheSpaces;
	unsigned numOfBlocks;
	CacheSlot* freeCache;
	struct cacheBlock_s* next;

}CacheBlock;

typedef struct kmem_cache_s {

	char name[NAME_SIZE];
	struct kmem_cache_s* next;
	Slab* emptySlabs;
	Slab* partialSlabs;
	Slab* fullSLabs;
	size_t objectSize;
	size_t slabSize;
	CacheBlock* owner;
	void(*ctor)(void *);
	void(*dtor)(void *);
	char allowShrinkage;
	unsigned offset;
	//-1 can't create slab, -2 can't shrink,-3 failed to allocate object ,-4 object not found in slabs,-5 can't destroy cache
	char errorCode;
	HANDLE mutex;

}kmem_cache_t;

typedef union cacheslot {
	union cacheslot* next;
	kmem_cache_t cache;
}CacheSlot;

typedef struct slabmanager_s {

	BuddyManager* buddyManager;
	CacheBlock* partialBlocks;
	CacheBlock* fullBlocks;
	kmem_cache_t* caches;
	kmem_cache_t* bufferCaches[NUM_BUFFERED_CACHES];
	unsigned numOfBlocks;
	HANDLE mutex;
	HANDLE bufferCreaterMutex;

}SlabManager;

SlabManager* slabManager;

int CheckCacheExists(kmem_cache_t* cache)
{
	kmem_cache_t* tmpCache = slabManager->caches;
	while (tmpCache && tmpCache != cache)
		tmpCache = tmpCache->next;
	if (tmpCache == NULL)
	{
		printf("Not a cache.\n");
		return FALSE;
	}
	return TRUE;
}

void CreateCacheBlock()
{
	double num_of_blocks = ceil((sizeof(CacheBlock) + sizeof(CacheSlot)) / (double)BLOCK_SIZE);
	CacheBlock* cacheBlock = BuddyAlloc(slabManager->buddyManager, num_of_blocks);
	if (cacheBlock == NULL)
		return;
	cacheBlock->next = NULL;
	cacheBlock->numOfFreeCacheSpaces = cacheBlock->cacheSpaces = (num_of_blocks*BLOCK_SIZE - sizeof(CacheBlock)) / sizeof(CacheSlot);
	cacheBlock->numOfBlocks = num_of_blocks;
	cacheBlock->freeCache = NULL;
	for (unsigned i = 0; i < cacheBlock->numOfFreeCacheSpaces; i++)
	{
		CacheSlot* tmpSlot = (unsigned)cacheBlock + sizeof(CacheBlock) + i * sizeof(CacheSlot);
		tmpSlot->next = cacheBlock->freeCache;
		cacheBlock->freeCache = tmpSlot;
	}

	cacheBlock->next = slabManager->partialBlocks;
	slabManager->partialBlocks = cacheBlock;

}

void kmem_init(void* space, int block_num)
{
	BuddyManager* buddyManager = BuddyInit(space, block_num);
	double num_of_blocks = ceil(sizeof(SlabManager) / (double)BLOCK_SIZE);
	slabManager = BuddyAlloc(buddyManager, num_of_blocks);
	slabManager->numOfBlocks = num_of_blocks;
	slabManager->partialBlocks = slabManager->fullBlocks = NULL;
	slabManager->buddyManager = buddyManager;
	slabManager->caches = NULL;
	slabManager->mutex = CreateMutex(NULL, FALSE, NULL);
	slabManager->bufferCreaterMutex = CreateMutex(NULL, FALSE, NULL);
	for (int i = 0; i < NUM_BUFFERED_CACHES; i++)
		slabManager->bufferCaches[i] = NULL;
	CreateCacheBlock();
}

int CreateSlab(kmem_cache_t* cache)
{
	int numOfObjects = (cache->slabSize - sizeof(Slab)) / (double)cache->objectSize;
	int visak = cache->slabSize - (numOfObjects*cache->objectSize + sizeof(Slab));
	void* tmp = BuddyAlloc(slabManager->buddyManager, cache->slabSize / BLOCK_SIZE);
	if (tmp == NULL)
	{
		cache->errorCode = -1;
		return FALSE;
	}
	Slab* tmpSlab = (unsigned)tmp + cache->offset;
	tmpSlab->memStart = tmp;
	tmpSlab->freeObjects = tmpSlab->numOfObjects = numOfObjects;
	tmpSlab->unusedSpace = visak;
	tmpSlab->owner = cache;
	tmpSlab->freeSlots = NULL;
	tmpSlab->next = NULL;
	tmpSlab->objectsStart = (unsigned)tmpSlab->memStart + cache->offset + sizeof(Slab);
	for (int i = 0; i < numOfObjects; i++)
	{
		Slot* tmpSlot = (unsigned)tmpSlab->objectsStart + i * cache->objectSize;
		tmpSlot->next = tmpSlab->freeSlots;
		tmpSlab->freeSlots = tmpSlot;
	}
	tmpSlab->next = cache->emptySlabs;
	cache->emptySlabs = tmpSlab;
	cache->offset = (cache->offset + CACHE_L1_LINE_SIZE) < tmpSlab->unusedSpace ? (cache->offset + CACHE_L1_LINE_SIZE) : 0;
	return TRUE;
}

kmem_cache_t * kmem_cache_create(const char* name, size_t size, void(*ctor)(void*), void(*dtor)(void*))
{
	WaitForSingleObject(slabManager->mutex, INFINITE);
	if (slabManager->partialBlocks == NULL)
		CreateCacheBlock();
	CacheBlock* block = slabManager->partialBlocks;
	if (block == NULL)
		return NULL;
	kmem_cache_t* cache = block->freeCache;
	block->freeCache = block->freeCache->next;
	block->numOfFreeCacheSpaces--;
	if (block->numOfFreeCacheSpaces == 0)
	{
		slabManager->partialBlocks = block->next;
		block->next = slabManager->fullBlocks;
		slabManager->fullBlocks = block;
	}


	cache->next = slabManager->caches;
	slabManager->caches = cache;
	ReleaseMutex(slabManager->mutex);

	cache->allowShrinkage = 1;
	cache->ctor = ctor;
	cache->dtor = dtor;
	cache->emptySlabs = cache->partialSlabs = cache->fullSLabs = NULL;
	cache->mutex = CreateMutex(NULL, FALSE, NULL);
	strcpy(cache->name, name);
	cache->objectSize = size < sizeof(Slot) ? sizeof(Slot) : size;
	cache->offset = 0;
	cache->owner = block;
	cache->slabSize = 0;
	cache->errorCode = 0;

	int pow2 = 0;
	while ((sizeof(Slab) + MIN_NUM_OBJECTS_IN_SLAB * size) > (pow(2, pow2)* BLOCK_SIZE))
		pow2++;
	cache->slabSize = pow(2, pow2)*BLOCK_SIZE;
	WaitForSingleObject(slabManager->mutex, INFINITE);
	CreateSlab(cache);
	ReleaseMutex(slabManager->mutex);
	return cache;
}

int kmem_cache_shrink(kmem_cache_t * cachep)
{
	WaitForSingleObject(slabManager->mutex, INFINITE);
	if (!CheckCacheExists(cachep))
	{
		ReleaseMutex(slabManager->mutex);
		return 0;
	}
	ReleaseMutex(slabManager->mutex);

	WaitForSingleObject(cachep->mutex, INFINITE);
	if (!cachep->allowShrinkage)
	{
		cachep->errorCode = -2;
		ReleaseMutex(cachep->mutex);
		return 0;
	}
	cachep->allowShrinkage = 2;
	WaitForSingleObject(slabManager->mutex, INFINITE);
	int numOfBLocks = 0;
	while (cachep->emptySlabs)
	{
		Slab* tmp = cachep->emptySlabs;
		cachep->emptySlabs = cachep->emptySlabs->next;
		numOfBLocks += cachep->slabSize / BLOCK_SIZE;
		BuddyFree(slabManager->buddyManager, tmp->memStart, cachep->slabSize / BLOCK_SIZE);
	} 
	ReleaseMutex(slabManager->mutex);
	ReleaseMutex(cachep->mutex);
	return numOfBLocks;
}

void* kmem_cache_alloc(kmem_cache_t * cachep)
{
	WaitForSingleObject(slabManager->mutex, INFINITE);
	if (!CheckCacheExists(cachep))
	{
		ReleaseMutex(slabManager->mutex);
		return NULL;
	}
	ReleaseMutex(slabManager->mutex);

	WaitForSingleObject(cachep->mutex, INFINITE);
	Slab* tmp = cachep->partialSlabs;
	int flag = 0;
	if (tmp == NULL)
	{
		tmp = cachep->emptySlabs;
		flag = 1;
	}
	if (tmp == NULL)
	{
		WaitForSingleObject(slabManager->mutex, INFINITE);
		int code = CreateSlab(cachep);
		if (cachep->allowShrinkage == 2)
			cachep->allowShrinkage = 0;
		ReleaseMutex(slabManager->mutex);
		if (!code)
		{
			cachep->errorCode = -3;
			ReleaseMutex(cachep->mutex);
			return NULL;
		}
		tmp = cachep->emptySlabs;
		flag = 1;
	}
	void* object = tmp->freeSlots;
	tmp->freeSlots = tmp->freeSlots->next;
	tmp->freeObjects--;
	if (cachep->ctor != NULL)
		cachep->ctor(object);

	if (!flag && !tmp->freeObjects)
	{
		Slab* prev = NULL;
		Slab* tmp2 = cachep->partialSlabs;
		while (tmp2 && tmp2 != tmp)
		{
			prev = tmp2;
			tmp2 = tmp2->next;
		}
		if (prev == NULL)
			cachep->partialSlabs = tmp->next;
		else
			prev->next = tmp->next;
		tmp->next = cachep->fullSLabs;
		cachep->fullSLabs = tmp;
	}
	else if (flag)
	{
		Slab* prev = NULL;
		Slab* tmp2 = cachep->emptySlabs;
		while (tmp2 && tmp2 != tmp)
		{
			prev = tmp2;
			tmp2 = tmp2->next;
		}
		if (prev == NULL)
			cachep->emptySlabs = tmp->next;
		else
			prev->next = tmp->next;
		tmp->next = cachep->partialSlabs;
		cachep->partialSlabs = tmp;
	}
	ReleaseMutex(cachep->mutex);
	return object;
}

int kmemFree(kmem_cache_t * cachep, void* objp)
{
	WaitForSingleObject(cachep->mutex, INFINITE);
	Slab* tmpSlab = cachep->partialSlabs;
	char flagFound = 0;

	while (tmpSlab)
	{
		Slot* tmpSlot = tmpSlab->objectsStart;
		int i = 0;
		while (i < tmpSlab->numOfObjects)
		{
			if (tmpSlot == objp)
			{
				flagFound = 1;
				break;
			}
			tmpSlot = (unsigned)tmpSlot + cachep->objectSize;
			i++;
		}
		if (flagFound)
			break;
		tmpSlab = tmpSlab->next;
	}

	if (!flagFound)
	{
		tmpSlab = cachep->fullSLabs;
		while (tmpSlab)
		{
			Slot* tmpSlot = tmpSlab->objectsStart;
			int i = 0;
			while (i < tmpSlab->numOfObjects)
			{
				if (tmpSlot == objp)
				{
					flagFound = 2;
					break;
				}
				tmpSlot = (unsigned)tmpSlot + cachep->objectSize;
				i++;
			}
			if (flagFound)
				break;
			tmpSlab = tmpSlab->next;
		}
		if (!flagFound)
		{
			cachep->errorCode = -4;
			ReleaseMutex(cachep->mutex);
			return FALSE;
		}
	}

	if (cachep->dtor != NULL)
		cachep->dtor(objp);
	Slot* tmpSlot = objp;
	tmpSlot->next = tmpSlab->freeSlots;
	tmpSlab->freeSlots = tmpSlot;
	tmpSlab->freeObjects++;

	if (tmpSlab->freeObjects == tmpSlab->numOfObjects)
	{
		Slab* prev = NULL;
		Slab* tmp2 = cachep->partialSlabs;
		while (tmp2 && tmp2 != tmpSlab)
		{
			prev = tmp2;
			tmp2 = tmp2->next;
		}
		if (prev == NULL)
			cachep->partialSlabs = tmpSlab->next;
		else
			prev->next = tmpSlab->next;
		tmpSlab->next = cachep->emptySlabs;
		cachep->emptySlabs = tmpSlab;
	}
	else if (flagFound == 2)
	{
		Slab* prev = NULL;
		Slab* tmp2 = cachep->fullSLabs;
		while (tmp2 && tmp2 != tmpSlab)
		{
			prev = tmp2;
			tmp2 = tmp2->next;
		}
		if (prev == NULL)
			cachep->fullSLabs = tmpSlab->next;
		else
			prev->next = tmpSlab->next;
		tmpSlab->next = cachep->partialSlabs;
		cachep->partialSlabs = tmpSlab;
	}

	ReleaseMutex(cachep->mutex);
	return TRUE;
}

void kmem_cache_free(kmem_cache_t * cachep, void* objp)
{
	WaitForSingleObject(slabManager->mutex, INFINITE);
	if (!CheckCacheExists(cachep))
	{
		ReleaseMutex(slabManager->mutex);
		return;
	}
	ReleaseMutex(slabManager->mutex);
	kmemFree(cachep, objp);
}

void* kmalloc(size_t size)
{
	int pow2 = MIN_POW2_BUFFERED_CACHE;
	while (pow(2, pow2) < size && pow2 <= MAX_POW2_BUFFERED_CACHE)
		pow2++;
	if (pow2 > MAX_POW2_BUFFERED_CACHE)
	{
		WaitForSingleObject(slabManager->mutex, INFINITE);
		printf("Size has to be a power of 2 between 5 and 17.\n");
		ReleaseMutex(slabManager->mutex);
		return NULL;
	}
	char name[10];
	sprintf(name, "size-%d", pow2);
	WaitForSingleObject(slabManager->bufferCreaterMutex, INFINITE);
	if (slabManager->bufferCaches[pow2 - MIN_POW2_BUFFERED_CACHE] == NULL)
	{
		slabManager->bufferCaches[pow2 - MIN_POW2_BUFFERED_CACHE] = kmem_cache_create(name, size, NULL, NULL);
		if (slabManager->bufferCaches[pow2 - MIN_POW2_BUFFERED_CACHE] == NULL)
		{
			ReleaseMutex(slabManager->bufferCreaterMutex);
			return NULL;
		}
	}
	ReleaseMutex(slabManager->bufferCreaterMutex);
	return kmem_cache_alloc(slabManager->bufferCaches[pow2 - MIN_POW2_BUFFERED_CACHE]);
}

void kfree(const void* objp)
{
	WaitForSingleObject(slabManager->bufferCreaterMutex, INFINITE);
	for (int i = 0; i < NUM_BUFFERED_CACHES; i++)
		if (slabManager->bufferCaches[i] != NULL)
			if (kmemFree(slabManager->bufferCaches[i], objp))
			{
				if (slabManager->bufferCaches[i]->emptySlabs)
				{
					WaitForSingleObject(slabManager->bufferCaches[i]->mutex, INFINITE);
					slabManager->bufferCaches[i]->allowShrinkage = 1;
					ReleaseMutex(slabManager->bufferCaches[i]->mutex);
					kmem_cache_shrink(slabManager->bufferCaches[i]);
				}
				ReleaseMutex(slabManager->bufferCreaterMutex);
				return;
			}
	ReleaseMutex(slabManager->bufferCreaterMutex);
}

void kmem_cache_destroy(kmem_cache_t * cachep)
{
	WaitForSingleObject(slabManager->mutex, INFINITE);
	if (!CheckCacheExists(cachep))
	{
		ReleaseMutex(slabManager->mutex);
		return;
	}
	WaitForSingleObject(cachep->mutex, INFINITE);
	if (cachep->fullSLabs != NULL || cachep->partialSlabs != NULL)
	{
		cachep->errorCode = -5;
		ReleaseMutex(cachep->mutex);
		ReleaseMutex(slabManager->mutex);
		return;
	}

	while (cachep->emptySlabs)
	{
		Slab* tmp = cachep->emptySlabs;
		cachep->emptySlabs = cachep->emptySlabs->next;
		BuddyFree(slabManager->buddyManager, tmp->memStart, cachep->slabSize / BLOCK_SIZE);
	}

	kmem_cache_t* prev = NULL;
	kmem_cache_t* tmp = slabManager->caches;
	while (tmp && tmp != cachep)
	{
		prev = tmp;
		tmp = tmp->next;
	}
	if (prev == NULL)
		slabManager->caches = cachep->next;
	else
		prev->next = cachep->next;
	ReleaseMutex(cachep->mutex);
	CacheBlock* owner = cachep->owner;
	int wasFull = owner->numOfFreeCacheSpaces == 0 ? 1 : 0;
	CacheSlot* slot = cachep;
	slot->next = owner->freeCache;
	owner->freeCache = slot;
	owner->numOfFreeCacheSpaces++;
	if (wasFull)
	{
		CacheBlock* prevBlock = NULL;
		CacheBlock* tmpBlock = slabManager->fullBlocks;
		while (tmpBlock && tmpBlock != owner)
		{
			prevBlock = tmpBlock;
			tmpBlock = tmpBlock->next;
		}
		if (prevBlock == NULL)
			slabManager->fullBlocks = owner->next;
		else
			prevBlock->next = owner->next;
		if (owner->numOfFreeCacheSpaces == owner->cacheSpaces)
			BuddyFree(slabManager->buddyManager, owner, owner->numOfBlocks);
		else
		{
			owner->next = slabManager->partialBlocks;
			slabManager->partialBlocks = owner;
		}
	}
	else if (owner->numOfFreeCacheSpaces == owner->cacheSpaces)
	{
		CacheBlock* prevBlock = NULL;
		CacheBlock* tmpBlock = slabManager->partialBlocks;
		while (tmpBlock && tmpBlock != owner)
		{
			prevBlock = tmpBlock;
			tmpBlock = tmpBlock->next;
		}
		if (prevBlock == NULL)
			slabManager->partialBlocks = owner->next;
		else
			prevBlock->next = owner->next;
		BuddyFree(slabManager->buddyManager, owner, owner->numOfBlocks);
	}
	ReleaseMutex(slabManager->mutex);
}

void kmem_cache_info(kmem_cache_t * cachep)
{
	WaitForSingleObject(slabManager->mutex, INFINITE);
	if (!CheckCacheExists(cachep))
	{
		ReleaseMutex(slabManager->mutex);
		return;
	}

	WaitForSingleObject(cachep->mutex, INFINITE);
	printf("Name:%s\n", cachep->name);
	printf("Object size:%d\n", cachep->objectSize);
	int numOfSlabs = 0;
	int numOfObjects = 0;
	int numOfTakenObjects = 0;
	Slab* tmp = cachep->emptySlabs;
	while (tmp)
	{
		numOfSlabs++;
		numOfObjects = tmp->numOfObjects;
		tmp = tmp->next;
	}
	tmp = cachep->partialSlabs;
	while (tmp)
	{
		numOfSlabs++;
		numOfObjects = tmp->numOfObjects;
		numOfTakenObjects += tmp->numOfObjects - tmp->freeObjects;
		tmp = tmp->next;
	}
	tmp = cachep->fullSLabs;
	while (tmp)
	{
		numOfSlabs++;
		numOfObjects = tmp->numOfObjects;
		numOfTakenObjects += tmp->numOfObjects;
		tmp = tmp->next;
	}
	printf("Full size:%d\n", numOfSlabs*cachep->slabSize / BLOCK_SIZE);
	printf("Number of slabs:%d\n", numOfSlabs);
	printf("Number of objects per slab:%d\n", numOfObjects);
	if (numOfSlabs != 0)
		printf("Percentage:%f\%\n\n", numOfTakenObjects / (double)(numOfObjects*numOfSlabs));
	else
		printf("Percentage:?\n\n");
	ReleaseMutex(cachep->mutex);
	ReleaseMutex(slabManager->mutex);
}

int kmem_cache_error(kmem_cache_t * cachep)
{
	WaitForSingleObject(slabManager->mutex, INFINITE);
	if (!CheckCacheExists(cachep))
	{
		ReleaseMutex(slabManager->mutex);
		return 0;
	}
	
	WaitForSingleObject(cachep->mutex, INFINITE);
	int error = cachep->errorCode;
	ReleaseMutex(cachep->mutex);

	switch (error)
	{
	case -1:
		printf("Couldn't create slab not enough memory left.\n");
		break;
	case -2:
		printf("Couldn't shrink cache expended after last shrinkage.\n");
		break;
	case -3:
		printf("Failed to allocate object.\n");
		break;
	case -4:
		printf("Object wasn't found in any slab during deallocation.\n");
		break;
	case -5:
		printf("Can't delete cache that contains objects.\n");
		break;
	}
	ReleaseMutex(slabManager->mutex);
	return error;
}