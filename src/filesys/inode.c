#define INODE_MAGIC 0x494e4f44
#define SECTOR_MAGIC 0xffffffff
#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "filesys/cache.h"


/* In-memory inode. */
struct inode
{
    struct list_elem elem; /* Element in inode list. */
    block_sector_t sector; /* Sector number of disk location. */
    int open_cnt;          /* Number of openers. */
    bool removed;          /* True if deleted, false otherwise. */
    int deny_write_cnt;    /* 0: writes ok, >0: deny writes. */
    //struct inode_disk data;
    struct lock lock_inode;
};


/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t byte_to_sector(const struct inode_disk *i, off_t pos)
{
    //ASSERT (inode != NULL);
    struct sector_index sec_idx;
    if (pos >= i->length) return -1;
    set_sector_index(pos, &sec_idx);
    switch (sec_idx.kind) {
    case 0:
        return i->table_direct[sec_idx.idx1];
        break;
    case 1:
    {
        block_sector_t retsec;
        struct indirect_inode* ind = (struct indirect_inode*)malloc(BLOCK_SECTOR_SIZE);
        block_sector_t sec = i->sector_indirect;
        if (sec == SECTOR_MAGIC) {
            free(ind);
            return -1;
            break;
        }
        buffer_cache_read(sec, ind, 0, sizeof(struct indirect_inode), 0);
        retsec = ind->table[sec_idx.idx1];
        free(ind);
        return retsec;
        break;
    }
    case 2:
    {
        block_sector_t retsec;
        struct indirect_inode* ind = (struct indirect_inode*)malloc(BLOCK_SECTOR_SIZE);
        block_sector_t sec = i->sector_double_indirect;
        if (sec == SECTOR_MAGIC) {
            free(ind);
            return -1;
            break;
        }
		buffer_cache_read(sec, ind, 0, sizeof(struct indirect_inode), 0);
        sec = ind->table[sec_idx.idx1];
        if (sec == SECTOR_MAGIC) {
            free(ind);
            return -1;
            break;
        }
        buffer_cache_read(sec, ind, 0, sizeof(struct indirect_inode), 0);
        retsec = ind->table[sec_idx.idx2];
        free(ind);
        return retsec;
        break;
    }
    }

    return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void inode_init(void)
{
    list_init(&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool inode_create(block_sector_t sector, off_t length, bool is_dir)
{
    struct inode_disk* disk_inode = NULL;
    bool success = false;

    ASSERT(length >= 0);

    /* If this assertion fails, the inode structure is not exactly
       one sector in size, and you should fix that. */
    ASSERT(sizeof * disk_inode == BLOCK_SECTOR_SIZE);

    disk_inode = calloc(1, sizeof * disk_inode);
    if (disk_inode != NULL)
    {
        //size_t sectors = bytes_to_sectors (length);
        init_sector_indirect(disk_inode);
        disk_inode->isdir = is_dir;
        disk_inode->magic = INODE_MAGIC;
        bool tmp = update_inode(disk_inode, disk_inode->length, length);
        if (tmp) {
            buffer_cache_write(sector, disk_inode, 0, BLOCK_SECTOR_SIZE, 0);
            success = true;
        }
        free(disk_inode);
    }
    return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *inode_open(block_sector_t sector)
{
    struct list_elem* e;
    struct inode* inode;
    /* Check whether this inode is already open. */
    for (e = list_begin(&open_inodes); e != list_end(&open_inodes);
        e = list_next(e))
    {
        inode = list_entry(e, struct inode, elem);
        if (inode->sector == sector)
        {
            inode_reopen(inode);
            return inode;
        }
    }

    /* Allocate memory. */
    inode = malloc(sizeof * inode);
    if (inode == NULL)
        return NULL;

    /* Initialize. */
    list_push_front(&open_inodes, &inode->elem);
    inode->sector = sector;
    inode->open_cnt = 1;
    inode->deny_write_cnt = 0;
    inode->removed = false;
    lock_init(&inode->lock_inode);
    return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen(struct inode *inode)
{
    if (inode != NULL)
        inode->open_cnt++;
    return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber(const struct inode *inode)
{
    return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void inode_close(struct inode *inode)
{
    struct inode_disk i;
    /* Ignore null pointer. */
    if (inode == NULL)
        return;

    /* Release resources if this was the last opener. */
    if (--inode->open_cnt == 0)
    {
        /* Remove from inode list and release lock. */
        list_remove(&inode->elem);

        /* Deallocate blocks if removed. */
        if (inode->removed)
        {
            buffer_cache_read(inode->sector, &i, 0, BLOCK_SECTOR_SIZE, 0);
            //free_map_release(inode->sector, 1);
            free_sectors_inode(&i);
            free_map_release(inode->sector, 1);
        }

        free(inode);
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void inode_remove(struct inode *inode)
{
    ASSERT(inode != NULL);
    inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t inode_read_at(struct inode *inode, void *buffer_, off_t size, off_t offset)
{
    uint8_t* buffer = buffer_;
    off_t bytes_read = 0;
    uint8_t* bounce = NULL;
    struct inode_disk i_disk;
    //synchronization needed in r/w
    lock_acquire(&inode->lock_inode);
    //buffer_cache_read(&i_disk->sector,&i_disk,0,sizeof(struct i_disk),0);
    buffer_cache_read(inode->sector, &i_disk, 0, sizeof(struct inode_disk), 0);
    lock_release(&inode->lock_inode);
    while (size > 0)
    {
        /* Disk sector to read, starting byte offset within sector. */
        block_sector_t sector_idx = byte_to_sector(&i_disk, offset);
        int sector_ofs = offset % BLOCK_SECTOR_SIZE;

        /* Bytes left in inode, bytes left in sector, lesser of the two. */
        //off_t inode_left = inode_length (inode) - offset;
        off_t inode_left = i_disk.length - offset;
        int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
        int min_left = inode_left < sector_left ? inode_left : sector_left;

        /* Number of bytes to actually copy out of this sector. */
        int chunk_size = size < min_left ? size : min_left;
        if (chunk_size <= 0)
            break;

        buffer_cache_read(sector_idx, buffer, bytes_read, chunk_size, sector_ofs);
        /* Advance. */
        size -= chunk_size;
        offset += chunk_size;
        bytes_read += chunk_size;
    }
    return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t inode_write_at(struct inode *inode, const void *buffer_, off_t size, off_t offset)
{
    const uint8_t* buffer = buffer_;
    off_t bytes_written = 0;
    uint8_t* bounce = NULL;
    struct inode_disk i_disk;

    if (inode->deny_write_cnt)
        return 0;

    lock_acquire(&inode->lock_inode);
    //update_inode(&i_disk,inode_disk.length,offset+size);
    //buffer_cache_write(inode_sector,&i_disk,0,BLOCK_SECTOR_SIZE,0);
    buffer_cache_read(inode->sector, &i_disk, 0, sizeof(struct inode_disk), 0);
    if (i_disk.length >= offset + size) {
        lock_release(&inode->lock_inode);
    }
    else {
        update_inode(&i_disk, i_disk.length, offset + size);
        buffer_cache_write(inode->sector, &i_disk, 0, BLOCK_SECTOR_SIZE, 0);
        lock_release(&inode->lock_inode);
    }


    while (size > 0)
    {
        /* Sector to write, starting byte offset within sector. */
        block_sector_t sector_idx = byte_to_sector(&i_disk, offset);
        int sector_ofs = offset % BLOCK_SECTOR_SIZE;

        /* Bytes left in inode, bytes left in sector, lesser of the two. */
        off_t inode_left = i_disk.length - offset;
        int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
        int min_left = inode_left < sector_left ? inode_left : sector_left;

        /* Number of bytes to actually write into this sector. */
        int chunk_size = size < min_left ? size : min_left;
        if (chunk_size <= 0)
            break;
        buffer_cache_write(sector_idx, buffer, bytes_written, chunk_size, sector_ofs);

        /* Advance. */
        size -= chunk_size;
        offset += chunk_size;
        bytes_written += chunk_size;
    }
    //free (bounce);

    return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void inode_deny_write(struct inode *inode)
{
    inode->deny_write_cnt++;
    ASSERT(inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void inode_allow_write(struct inode *inode)
{
    ASSERT(inode->deny_write_cnt > 0);
    ASSERT(inode->deny_write_cnt <= inode->open_cnt);
    inode->deny_write_cnt--;
}

bool is_direc(struct inode* i) {
    if (i->removed) return false;
    bool success = false;
    struct inode_disk* i_d = (struct inode_disk*)malloc(BLOCK_SECTOR_SIZE);
    buffer_cache_read(i->sector, i_d, 0, BLOCK_SECTOR_SIZE, 0);
    if (i_d->isdir) success = true;
    free(i_d);
    return success;
}

void set_sector_index(off_t p, struct sector_index *sec_idx)
{
    p = p / BLOCK_SECTOR_SIZE;

    if (p < 123) {
        sec_idx->kind = 0;
        sec_idx->idx1 = p;
    }
    else if (123 <= p && p < 251) {
        p = p - 123;
        sec_idx->kind = 1;
        sec_idx->idx1 = p;
    }
    else if (251 <= p && p < (1 << 14) + 251) {
        p = p - 251;
        sec_idx->kind = 2;
        sec_idx->idx1 = p / (1 << 7);
        sec_idx->idx2 = p % (1 << 7);
    }
    else
        sec_idx->kind = -1;
}

void init_sector_indirect(struct indirect_inode *insec){
    for (int i = 0; i < (1 << 7); i++)
        insec->table[i] = SECTOR_MAGIC;
}

bool add_new_sector(struct inode_disk *i, block_sector_t new, struct sector_index sec_idx)
{

    if (sec_idx.kind == 0) {
        i->table_direct[sec_idx.idx1] = new;
        return true;
    }

    struct indirect_inode s, d;
    block_sector_t* tmp;

    if (sec_idx.kind == 1) {
        tmp = &i->sector_indirect;
        if (*tmp != SECTOR_MAGIC)
            buffer_cache_read(*tmp, &d, 0, sizeof(struct indirect_inode), 0);
        else {
            if (free_map_allocate(1, tmp)) init_sector_indirect(&d);
            else return false;
        }
        if (d.table[sec_idx.idx1] == SECTOR_MAGIC)
            d.table[sec_idx.idx1] = new;
        buffer_cache_write(*tmp, &d, 0, sizeof(struct indirect_inode), 0);
        return true;
    }
    else if (sec_idx.kind == 2) {
        tmp = &i->sector_double_indirect;
        if (*tmp != SECTOR_MAGIC)
            buffer_cache_read(*tmp, &s, 0, sizeof(struct indirect_inode), 0);
        else {
            if (free_map_allocate(1, tmp)) init_sector_indirect(&s);
            else return false;
        }
        tmp = &s.table[sec_idx.idx1];
        if (*tmp != SECTOR_MAGIC) {
            buffer_cache_read(*tmp, &d, 0, sizeof(struct indirect_inode), 0);
            if (d.table[sec_idx.idx2] == SECTOR_MAGIC) {
                d.table[sec_idx.idx2] = new;
            }
            buffer_cache_write(*tmp, &d, 0, sizeof(struct indirect_inode), 0);
        }
        else {
            if (free_map_allocate(1, tmp)) {
                init_sector_indirect(&d);
                if (d.table[sec_idx.idx2] == SECTOR_MAGIC)
                    d.table[sec_idx.idx2] = new;
                buffer_cache_write(i->sector_double_indirect, &s, 0, sizeof(struct indirect_inode), 0);
                buffer_cache_write(*tmp, &d, 0, sizeof(struct indirect_inode), 0);
            }
            else return false;
        }
        return true;
    }
    return false;
}

bool update_inode(struct inode_disk* i_d, off_t s, off_t e) {
    block_sector_t tmp;
    struct sector_index sec_idx;
    static char temp[BLOCK_SECTOR_SIZE];

    i_d->length = e;
    s /= BLOCK_SECTOR_SIZE;
    s *= BLOCK_SECTOR_SIZE;
    e -= 1;
    e /= BLOCK_SECTOR_SIZE;
    e *= BLOCK_SECTOR_SIZE;

    while (s <= e) {
        tmp = byte_to_sector(i_d, s);
        if (tmp == SECTOR_MAGIC) {
            if (free_map_allocate(1, &tmp)) {
                set_sector_index(s, &sec_idx);
                if (!add_new_sector(i_d, tmp, sec_idx)) return false;
                buffer_cache_write(tmp, temp, 0, BLOCK_SECTOR_SIZE, 0);
                s += BLOCK_SECTOR_SIZE;
            }
            else return false;
        }
        else
            s += BLOCK_SECTOR_SIZE;
    }
    return true;
}

void free_sectors_inode(struct inode_disk *i_d)
{
    struct indirect_inode* insec1, * insec2;
    int i, j;

    //second indirection
    if (i_d->sector_double_indirect != SECTOR_MAGIC) {
        i = 0;
        insec1 = (struct indirect_inode*)malloc(BLOCK_SECTOR_SIZE);
        buffer_cache_read(i_d->sector_double_indirect, insec1, 0, sizeof(struct indirect_inode), 0);
        while (insec1->table[i] != SECTOR_MAGIC) {
            j = 0;
            insec2 = (struct indirect_inode*)malloc(BLOCK_SECTOR_SIZE);
            buffer_cache_read(insec2->table[j], insec2, 0, sizeof(struct indirect_inode), 0);
            while (insec2->table[j] != SECTOR_MAGIC) {
                free_map_release(insec2->table[j], 1);
                j++;
            }
            free_map_release(insec1->table[i], 1);
            free(insec2);
            i++;
        }
        free(insec1);
        return;
    }

    //first indirection
    if (i_d->sector_indirect != SECTOR_MAGIC) {
        i = 0;
        insec1 = (struct indirect_inode*)malloc(BLOCK_SECTOR_SIZE);
        buffer_cache_read(i_d->sector_indirect, insec1, 0, sizeof(struct indirect_inode), 0);
        while (insec1->table[i] != SECTOR_MAGIC) {
            free_map_release(insec1->table[i], 1);
            i++;
        }
        free(insec1);
        return;
    }

    i = 0;
    //direct
    while (i_d->table_direct[i] != SECTOR_MAGIC) {
        free_map_release(i_d->table_direct[i], 1);
        i++;
    }
    return;
}

off_t inode_length(struct inode *inode)
{
    off_t res;
    struct inode_disk *disk_inode = (struct inode_disk *)malloc(BLOCK_SECTOR_SIZE);
    buffer_cache_read(inode->sector, disk_inode, 0, BLOCK_SECTOR_SIZE, 0);
    res = disk_inode->length;
    free(disk_inode);
    return res;
}
