// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKETS 13

struct bucket {
  struct spinlock lock;
  struct buf head;
};

struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // 哈希桶数组
  struct bucket buckets[NBUCKETS];
} bcache;

// 哈希函数
static int
hash(uint blockno)
{
  return blockno % NBUCKETS;
}

void
binit(void)
{
  char lockname[16];

  initlock(&bcache.lock, "bcache");

  // 初始化所有桶锁及链表头
  for(int i = 0; i < NBUCKETS; i++){
    snprintf(lockname, sizeof(lockname), "bcache.bucket_%d", i);
    initlock(&bcache.buckets[i].lock, lockname);
    bcache.buckets[i].head.next = &bcache.buckets[i].head;
    bcache.buckets[i].head.prev = &bcache.buckets[i].head;
  }

  // 将所有 NBUF 分配给桶 0（或者散列到各个桶中均可）
  for(int i = 0; i < NBUF; i++){
    struct buf *b = &bcache.buf[i];
    initsleeplock(&b->lock, "buffer");
    b->refcnt = 0;
    b->ticks = 0;
    b->dev = -1;
    b->blockno = -1;
    // 挂载到 buckets[0] 链表中
    b->next = bcache.buckets[0].head.next;
    b->prev = &bcache.buckets[0].head;
    bcache.buckets[0].head.next->prev = b;
    bcache.buckets[0].head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int b_idx = hash(blockno);

  // 1. Fast Path: 仅获取对应桶的锁
  acquire(&bcache.buckets[b_idx].lock);

  for(b = bcache.buckets[b_idx].head.next; b != &bcache.buckets[b_idx].head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.buckets[b_idx].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  release(&bcache.buckets[b_idx].lock);

  // 2. Slow Path: 缓存未命中，获取全局大锁保护驱逐/窃取过程
  acquire(&bcache.lock);

  // 再次获取当前桶锁（Double-Check）
  acquire(&bcache.buckets[b_idx].lock);
  for(b = bcache.buckets[b_idx].head.next; b != &bcache.buckets[b_idx].head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.buckets[b_idx].lock);
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // 寻找 LRU 可替换块：优先当前桶，次之遍历其他桶
  struct buf *lru_b = 0;
  uint min_ticks = 0xffffffff;
  int target_bucket = -1;

  // 遍历所有桶寻找最久未被使用的 refcnt == 0 的 buf
  for(int i = 0; i < NBUCKETS; i++){
    int idx = (b_idx + i) % NBUCKETS; // 从当前桶开始遍历
    
    // 如果不是当前桶，需要加锁保护访问
    if(idx != b_idx)
      acquire(&bcache.buckets[idx].lock);

    for(struct buf *tmp = bcache.buckets[idx].head.next; tmp != &bcache.buckets[idx].head; tmp = tmp->next){
      if(tmp->refcnt == 0 && tmp->ticks < min_ticks){
        min_ticks = tmp->ticks;
        lru_b = tmp;
        target_bucket = idx;
      }
    }

    if(idx != b_idx)
      release(&bcache.buckets[idx].lock);
  }

  if(!lru_b)
    panic("bget: no buffers");

  // 如果找到的块在其他桶，将其从原桶摘下，移入当前桶
  if(target_bucket != b_idx){
    acquire(&bcache.buckets[target_bucket].lock);

    // 从原桶链表中移除
    lru_b->prev->next = lru_b->next;
    lru_b->next->prev = lru_b->prev;

    release(&bcache.buckets[target_bucket].lock);

    // 插入当前桶链表
    lru_b->next = bcache.buckets[b_idx].head.next;
    lru_b->prev = &bcache.buckets[b_idx].head;
    bcache.buckets[b_idx].head.next->prev = lru_b;
    bcache.buckets[b_idx].head.next = lru_b;
  }

  lru_b->dev = dev;
  lru_b->blockno = blockno;
  lru_b->valid = 0;
  lru_b->refcnt = 1;

  release(&bcache.buckets[b_idx].lock);
  release(&bcache.lock);

  acquiresleep(&lru_b->lock);
  return lru_b;
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int bucket_idx = hash(b->blockno);
  acquire(&bcache.buckets[bucket_idx].lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    b->ticks = ticks; // 获取全局系统的 ticks
  }
  release(&bcache.buckets[bucket_idx].lock);
}
void
bpin(struct buf *b) {
  int b_idx = hash(b->blockno);
  acquire(&bcache.buckets[b_idx].lock);
  b->refcnt++;
  release(&bcache.buckets[b_idx].lock);
}

void
bunpin(struct buf *b) {
  int b_idx = hash(b->blockno);
  acquire(&bcache.buckets[b_idx].lock);
  b->refcnt--;
  release(&bcache.buckets[b_idx].lock);
}

