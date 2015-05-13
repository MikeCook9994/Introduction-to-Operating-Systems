#define SUPERBLOCKLOC 1
#define BITMAPLOC 1 /* garbage block */ + 1 /* super block */ + (sb->ninodes / IPB) /* number of blocks occupied by inodes */ + 1 /* bitmap */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <string.h>
#include "fs.h"

int fsd;
struct superblock * sb;
struct dinode * inodeList;
struct stat * stats;
char * databitmap;
int * linkcount;
int * badinodes;
int * allocatedBlocks;

/* Hard seeks to a place in the image file */
int seek(int location) {
	int returnval;
	returnval = lseek(fsd, location * BSIZE, SEEK_SET);
	assert(returnval != -1);
	return returnval;
}

/* wrapper function for read */
int peruse(void * buf, int count) {
	int returnval;
	returnval = read(fsd, buf, count);
	assert(returnval != -1);
	return returnval;
}

/* sets a bit in the data bitmap */
void setbit(int datablock) {

	char byte;
	byte = databitmap[datablock / 8];
	
	char mask;
	mask = 1 << datablock % 8;

	databitmap[datablock / 8] = byte | mask;
}

/* clears an inodes */
void clearinode(struct dinode * inode) {
	memset(inode, 0, sizeof(struct dinode));
}

/* clears references to inodes that were cleared */
void clearbaddirents(struct dinode * dir) {

}

/* moves a file with no directory references to the lost and found */
void lostandfound(struct dinode * dir) {

}

/* checks the directory for the . and .. directories and counts its 
   references to other inodes */
void checkDirectory(struct dinode * dirinode, int inumber) {

	struct dirent * dir;
	dir = malloc(dirinode->size);
	
	seek(dirinode->addrs[0]);
	peruse(dir, dirinode->size);

	if(dir->name[0] != '.' && dir->name[1] != '\0') {
		dir->name[0] = '.';
		dir->name[1] = '\0';
	}

	if((dir + 1)->name[0] != '.' && (dir + 1)->name[1] != '.' && (dir + 1)->name[2] != '\0') {
		(dir + 1)->name[0] = '.';
		(dir + 1)->name[1] = '.';
		(dir + 1)->name[1] = '\0';
	}
}

/* checks the validity of a inodes fields */
int checkInodeState(struct dinode * inode) {

	/* checks the inode reference count */
	if(inode->type == 0)
		return 0;
	if(inode->nlink == 0 || inode->nlink >= MAXFILE) {
		clearinode(inode);
		return -1;	
	}
	/* checks the inode type */
	if(inode->type > 3 || inode->type < 0) {
		clearinode(inode);
		return -1;
	}
	if(inode->size < 0 || inode->size > (MAXFILE * BSIZE)) {
		clearinode(inode);
		return -1;
	}
	return 0;
}

/* builds a new bit map using the current state of the inodes */
void constructbitmap(void) {

	struct dinode * tmp;
	uint addr;
	uint * indirectblock;

	int i;
	for(i = 0; i < BITMAPLOC + 1 /* garbage bitmap block */; i++) {
		setbit(i);
	}	

	int j;
	for(i = ROOTINO; i < sb->ninodes; i++) {

		tmp = inodeList + i;

		for(j = 0; j < NDIRECT; j++) {
			addr = tmp->addrs[j];
			if(addr == 0)
				continue;
			setbit(addr);
		}

		addr = tmp->addrs[NDIRECT];
		if(addr == 0) 
			continue;
		setbit(addr);

		indirectblock = malloc(BSIZE);
		seek(addr);
		peruse(indirectblock, BSIZE);
		for(j = 0; j < BSIZE / 4; j++) {
			addr = *(indirectblock + j);
			if(addr == 0)
				continue;
			setbit(addr);
		}
	}
}

/* sniffs the super block for inconsistent contents. */
int sniffsb(uint * size, uint nblocks, uint ninodes) {

	/* repairs the super block by correcting the size value if the 
	size times block size does not equal the file size */
	if(*size * BSIZE != (int) stats->st_size) {
		*size = stats->st_size / BSIZE;
		seek(SUPERBLOCKLOC);
		write(fsd, size, 4);
	}
	/* verifies the size is a multiple of the block size */
	if(*size % BSIZE != 0)
		return -1;
	/* verifies the number of nodes is a multiple of the number of 
	inodes per block */
	if(ninodes % IPB != 0)
		return -1;
	/* verifies that the total number of blocks is greater than or equal to 
	the the number of data blocks plus the number of inode blocks */
	if(nblocks + (ninodes / IPB) > *size)
		return -1;
	/* the block doesn't smell */
	return 0;
}

/* fuck this program */
int main(int charc, char * argv[]) {

	/* validates that an input file name is provided */
	if(charc < 2) {
		printf("Error!\n");
		exit(0);
	}

	/* allocates space for the superblock and file stats structs */
	sb = malloc(sizeof(struct superblock));
	assert(sb != NULL);	

	stats = malloc(sizeof(struct stat));	
	assert(stats != NULL);	

	/* attempts to open the provided fs image */
	fsd = open(argv[1], O_RDWR);
	assert(fsd != -1);

	/* stats the file */
	fstat(fsd, stats);

	/*scans over the FS image to the super block*/
	seek(SUPERBLOCKLOC);

	/* populates the superblock struct and prints its contents */
	peruse(sb, sizeof(struct superblock));

	/* a pointer to size value stored in the superblock that allows the super 
	   block to be repaired */
	uint * sbsize;
	sbsize = &sb->size;

	/* calls the function that sniffs the super block */ 
	if(sniffsb(sbsize, sb->nblocks, sb->ninodes) != 0) {
		printf("Error\n");
		exit(0);
	}

	/* allocates space for enumerating data bitmap */	
	inodeList = malloc(sizeof(struct dinode) * sb->ninodes);	
	assert(inodeList != NULL);

	/* sets you to the root inode */
	seek(IBLOCK(0));

	/* populates the list of inodes for future access */
	peruse(inodeList, sizeof(struct dinode) * sb->ninodes);

	/* allocates memory for the bit map */
	databitmap = malloc(sb->nblocks / 8 + 1);
	assert(databitmap != NULL);

	/* checks through the inodes to construct a correct inode bitmap */
	constructbitmap();

	/* writes the bit map to the file */
	seek(BITMAPLOC);
	write(fsd, databitmap, BSIZE);

	linkcount = malloc(sb->ninodes * sizeof(int));
	assert(linkcount != NULL);
	memset(linkcount, 0, sb->ninodes * sizeof(int));

	badinodes = malloc(sb->ninodes * sizeof(int));
	assert(badinodes != NULL);

	int i;
	for(i = ROOTINO; i < sb->ninodes; i++) {
		if(checkInodeState(inodeList + i) == -1)
			*(badinodes + i - 1) = i;
		if((inodeList + i)->type == 1)
			checkDirectory(inodeList + i, i);
	}

	constructbitmap();

	seek(BITMAPLOC);
	write(fsd, databitmap, BSIZE);	

	for(i = ROOTINO; i < sb->ninodes; i++) {
		(inodeList + i)->nlink = *(linkcount + i);
		if(((inodeList + i)->type != 0) && ((inodeList + i)->nlink != 0))
			lostandfound(inodeList + i);
		if((inodeList + i)->type == 1)
			clearbaddirents(inodeList + i);
	}

	close(fsd);	

	return 0;	
}
