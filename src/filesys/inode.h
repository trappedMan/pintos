#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include "devices/block.h"
#include "filesys/off_t.h"
#include <stdbool.h>

struct bitmap;

struct sector_index {
	int kind;
	int idx1;
	int idx2;
};
struct indirect_inode {
	block_sector_t table[1 << 7];
};

struct inode_disk {
	off_t length;
	unsigned magic;
	bool isdir;
	block_sector_t table_direct[123];
	block_sector_t sector_indirect;
	block_sector_t sector_double_indirect;
};

void inode_init(void);
bool inode_create(block_sector_t, off_t, bool);
struct inode *inode_open(block_sector_t);
struct inode *inode_reopen(struct inode *);
block_sector_t inode_get_inumber(const struct inode *);
void inode_close(struct inode *);
void inode_remove(struct inode *);
off_t inode_read_at(struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at(struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write(struct inode *);
void inode_allow_write(struct inode *);
off_t inode_length(struct inode *);
bool is_direc(struct inode*);

void set_sector_index(off_t, struct sector_index*);
void init_sector_indirect(struct indirect_inode* block);
bool add_new_sector(struct inode_disk*, block_sector_t, struct sector_index);
void free_sectors_inode(struct inode_disk*);
bool update_inode(struct inode_disk*, off_t, off_t);

#endif /* filesys/inode.h */
