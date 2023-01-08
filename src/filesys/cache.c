#include "filesys/cache.h"
#include "threads/palloc.h"
#include <debug.h>
#include <string.h>

//variables given
struct buffer_cache_entry cache[NUM_CACHE];
struct lock buffer_cache_lock;

struct buffer_cache_entry* clk; //replacement by clock

void buffer_cache_init(void){
    for (int i = 0; i < NUM_CACHE; ++i){
        memset(&cache[i], 0, sizeof(struct buffer_cache_entry));
        lock_init(&cache[i].lock_per_entry);
    }
    lock_init(&buffer_cache_lock);
    clk = cache;
}

void buffer_cache_terminate(void){
    //clock = NULL;
    buffer_cache_flush_all();   //needed for persistence
}

bool buffer_cache_read(block_sector_t sec, void* buf, off_t pos, int size, int sector_pos){
    struct buffer_cache_entry *tmp = buffer_cache_lookup(sec);
    if (tmp) {
        lock_acquire(&tmp->lock_per_entry);
        memcpy(buf + pos, tmp->buffer + sector_pos, size);
        tmp->reference_bit = true;
        lock_release(&tmp->lock_per_entry);

    }
    else{
        lock_acquire(&buffer_cache_lock);
        struct buffer_cache_entry* target = buffer_cache_select_victim();
        lock_acquire(&target->lock_per_entry);
        buffer_cache_flush_entry(target);
        target->valid_bit = true;
        target->reference_bit = true;
        target->dirty_bit = false;
        target->disk_sector = sec;

        block_read(fs_device, sec, target->buffer);
        memcpy(buf + pos, target->buffer + sector_pos, size);

        lock_release(&target->lock_per_entry);
        lock_release(&buffer_cache_lock);
    }
    return true;
}


bool buffer_cache_write(block_sector_t sec, void* buf, off_t pos, int size, int sector_pos){
    struct buffer_cache_entry *tmp = buffer_cache_lookup(sec);
    if (tmp){
        lock_acquire(&tmp->lock_per_entry);
        memcpy(tmp->buffer + sector_pos, buf + pos, size);
        tmp->reference_bit = true;
        tmp->dirty_bit = true;
        lock_release(&tmp->lock_per_entry);

    }
    else{
        lock_acquire(&buffer_cache_lock);
        struct buffer_cache_entry* target = buffer_cache_select_victim();
        lock_acquire(&target->lock_per_entry);
        buffer_cache_flush_entry(target);
        target->valid_bit = true;
        target->reference_bit = true;
        target->dirty_bit = true;
        target->disk_sector = sec;

        block_read(fs_device, sec, target->buffer);
        memcpy(target->buffer + sector_pos, buf + pos, size);

        lock_release(&target->lock_per_entry);
        lock_release(&buffer_cache_lock);
    }
    return true;
}


struct buffer_cache_entry *buffer_cache_lookup(block_sector_t sec){
    int i;
    lock_acquire(&buffer_cache_lock);
    for (i = 0; i < NUM_CACHE; i++) //find appropriate cache
        if (cache[i].valid_bit && cache[i].disk_sector == sec)
            break;
    lock_release(&buffer_cache_lock);
    if (i >= NUM_CACHE) return NULL;
    else return &cache[i];
}

struct buffer_cache_entry *buffer_cache_select_victim(void){
    struct buffer_cache_entry* victim;
    bool flag = false;

    for (;!flag;clk++) {
        if (clk == cache + NUM_CACHE) clk = cache; //rotate cache space
        lock_acquire(&clk->lock_per_entry);
        if (!clk->valid_bit || !clk->reference_bit){
            victim = clk;
            lock_release(&clk->lock_per_entry);
            flag = true;
            continue;
        }
        clk->reference_bit = false;
        lock_release(&clk->lock_per_entry);
    }
    return victim;
    /*
    while (1){
        clk=cache;
        lock_acquire(&clk->lock);
        if (!clk->valid_bit || !clk->reference_bit)
        lock_release(&clk->lock);
    }*/
}

void buffer_cache_flush_entry(struct buffer_cache_entry *e){
    if (e->valid_bit&&e->dirty_bit){
        e->dirty_bit = false;
        block_write(fs_device, e->disk_sector, e->buffer);
        for (int i=0;i<BLOCK_SECTOR_SIZE;i++)
            e->buffer[i]=NULL;
    }
}

void buffer_cache_flush_all(){
    for (int i = 0; i < NUM_CACHE; i++) {
        lock_acquire(&cache[i].lock_per_entry);
        buffer_cache_flush_entry(&cache[i]);
        lock_release(&cache[i].lock_per_entry);
    }
}