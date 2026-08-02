#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "e1000_dev.h"
#include "net.h"

#define TX_RING_SIZE 16
static struct tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *tx_mbufs[TX_RING_SIZE];

#define RX_RING_SIZE 16
static struct rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *rx_mbufs[RX_RING_SIZE];

// remember where the e1000's registers live.
static volatile uint32 *regs;

struct spinlock e1000_lock;

// called by pci_init().
// xregs is the memory address at which the
// e1000's registers are mapped.
void
e1000_init(uint32 *xregs)
{
  int i;

  initlock(&e1000_lock, "e1000");

  regs = xregs;

  // Reset the device
  regs[E1000_IMS] = 0; // disable interrupts
  regs[E1000_CTL] |= E1000_CTL_RST;
  regs[E1000_IMS] = 0; // redisable interrupts
  __sync_synchronize();

  // [E1000 14.5] Transmit initialization
  memset(tx_ring, 0, sizeof(tx_ring));
  for (i = 0; i < TX_RING_SIZE; i++) {
    tx_ring[i].status = E1000_TXD_STAT_DD;
    tx_mbufs[i] = 0;
  }
  regs[E1000_TDBAL] = (uint64) tx_ring;
  if(sizeof(tx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_TDLEN] = sizeof(tx_ring);
  regs[E1000_TDH] = regs[E1000_TDT] = 0;
  
  // [E1000 14.4] Receive initialization
  memset(rx_ring, 0, sizeof(rx_ring));
  for (i = 0; i < RX_RING_SIZE; i++) {
    rx_mbufs[i] = mbufalloc(0);
    if (!rx_mbufs[i])
      panic("e1000");
    rx_ring[i].addr = (uint64) rx_mbufs[i]->head;
  }
  regs[E1000_RDBAL] = (uint64) rx_ring;
  if(sizeof(rx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_RDH] = 0;
  regs[E1000_RDT] = RX_RING_SIZE - 1;
  regs[E1000_RDLEN] = sizeof(rx_ring);

  // filter by qemu's MAC address, 52:54:00:12:34:56
  regs[E1000_RA] = 0x12005452;
  regs[E1000_RA+1] = 0x5634 | (1<<31);
  // multicast table
  for (int i = 0; i < 4096/32; i++)
    regs[E1000_MTA + i] = 0;

  // transmitter control bits.
  regs[E1000_TCTL] = E1000_TCTL_EN |  // enable
    E1000_TCTL_PSP |                  // pad short packets
    (0x10 << E1000_TCTL_CT_SHIFT) |   // collision stuff
    (0x40 << E1000_TCTL_COLD_SHIFT);
  regs[E1000_TIPG] = 10 | (8<<10) | (6<<20); // inter-pkt gap

  // receiver control bits.
  regs[E1000_RCTL] = E1000_RCTL_EN | // enable receiver
    E1000_RCTL_BAM |                 // enable broadcast
    E1000_RCTL_SZ_2048 |             // 2048-byte rx buffers
    E1000_RCTL_SECRC;                // strip CRC
  
  // ask e1000 for receive interrupts.
  regs[E1000_RDTR] = 0; // interrupt after every received packet (no timer)
  regs[E1000_RADV] = 0; // interrupt after every packet (no timer)
  regs[E1000_IMS] = (1 << 7); // RXDW -- Receiver Descriptor Write Back
}

int
e1000_transmit(struct mbuf *m)
{
  acquire(&e1000_lock);

  //获取当前待填充的 Tx 描述符索引 (regs[E1000_TDT])
  uint32 idx = regs[E1000_TDT];
  //检查该描述符位置上一次的数据包是否已经发送完成
  //如果 status 中没有设置 E1000_TXD_STAT_DD，说明环已被填满，网卡还没处理完
  if ((tx_ring[idx].status & E1000_TXD_STAT_DD) == 0) {
    release(&e1000_lock);
    return -1; // 环已满，发送失败
  }
  //如果该描述符之前持有未释放的 mbuf，现在可以安全释放了
  if (tx_mbufs[idx]) {
    mbuffree(tx_mbufs[idx]);
    tx_mbufs[idx] = 0;
  }

  //填写描述符信息
  tx_ring[idx].addr = (uint64)m->head;       // 数据包内存物理地址
  tx_ring[idx].length = m->len;              // 数据包长度
  // 设置 CMD 标志: EOP (End of Packet) + RS (Report Status，发完后设置 DD)
  tx_ring[idx].cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS;
  tx_ring[idx].status = 0;                   // 清空 status
  tx_mbufs[idx] = m;
  //更新 TDT 寄存器，指向下一个位置
  regs[E1000_TDT] = (idx + 1) % TX_RING_SIZE;
  release(&e1000_lock);
  return 0;
  
  return 0;
}

static void
e1000_recv(void)
{
  uint32 idx = (regs[E1000_RDT] + 1) % RX_RING_SIZE;
  while (rx_ring[idx].status & E1000_RXD_STAT_DD) {
    struct mbuf *m = rx_mbufs[idx];
    // 设置 mbuf 的长度为实际接收到的字节数
    m->len = rx_ring[idx].length;

    // 分配一个新的 mbuf 填补刚刚空出来的 Rx 描述符位置
    struct mbuf *new_m = mbufalloc(0);
    if (new_m == 0) {
      // 内存不足分配失败，通常说明系统繁忙
      break;
    }
    // 更新索引位置上的 mbuf 和描述符地址
    rx_mbufs[idx] = new_m;
    rx_ring[idx].addr = (uint64)new_m->head;
    rx_ring[idx].status = 0; // 重置 status 标志位

    //更新RDT寄存器，告诉网卡这个描述符现在又可用了
    regs[E1000_RDT] = idx;

    // 将接收到的数据包提交给上层网络协议栈处理
    net_rx(m);
    //移动到下一个描述符
    idx = (regs[E1000_RDT] + 1) % RX_RING_SIZE;
}
}

void
e1000_intr(void)
{
  // tell the e1000 we've seen this interrupt;
  // without this the e1000 won't raise any
  // further interrupts.
  regs[E1000_ICR] = 0xffffffff;

  e1000_recv();
}
