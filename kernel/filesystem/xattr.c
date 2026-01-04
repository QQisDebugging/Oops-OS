// xattr.c - 文件扩展属性实现
// 支持为文件附加任意 key-value 元数据

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"

// 扩展属性常量
#define XATTR_NAME_MAX   64    // 属性名最大长度
#define XATTR_VALUE_MAX  256   // 属性值最大长度
#define XATTR_MAX_ATTRS  8     // 每个文件最大属性数
#define XATTR_TABLE_SIZE 64    // xattr 表大小

// 单个扩展属性
struct xattr_entry {
  char name[XATTR_NAME_MAX];
  char value[XATTR_VALUE_MAX];
  int value_len;
  int used;
};

// 文件的扩展属性集合
struct xattr_inode {
  uint dev;
  uint inum;
  struct xattr_entry attrs[XATTR_MAX_ATTRS];
  int used;
};

// 全局 xattr 表
struct {
  struct spinlock lock;
  struct xattr_inode table[XATTR_TABLE_SIZE];
} xattr_table;

void
xattrinit(void)
{
  initlock(&xattr_table.lock, "xattr");
  for(int i = 0; i < XATTR_TABLE_SIZE; i++) {
    xattr_table.table[i].used = 0;
  }
}

// 查找或创建 inode 的 xattr 条目
static struct xattr_inode*
xattr_find_or_create(uint dev, uint inum, int create)
{
  struct xattr_inode *xi, *empty = 0;

  for(int i = 0; i < XATTR_TABLE_SIZE; i++) {
    xi = &xattr_table.table[i];
    if(xi->used && xi->dev == dev && xi->inum == inum) {
      return xi;
    }
    if(!xi->used && !empty) {
      empty = xi;
    }
  }

  if(create && empty) {
    empty->dev = dev;
    empty->inum = inum;
    empty->used = 1;
    for(int j = 0; j < XATTR_MAX_ATTRS; j++) {
      empty->attrs[j].used = 0;
    }
    return empty;
  }

  return 0;
}

// 设置扩展属性
int
xattr_set(struct inode *ip, char *name, char *value, int size)
{
  struct xattr_inode *xi;
  struct xattr_entry *xe;
  int namelen = strlen(name);

  if(namelen == 0 || namelen >= XATTR_NAME_MAX)
    return -1;
  if(size < 0 || size > XATTR_VALUE_MAX)
    return -1;

  acquire(&xattr_table.lock);

  xi = xattr_find_or_create(ip->dev, ip->inum, 1);
  if(!xi) {
    release(&xattr_table.lock);
    return -1;  // 表满
  }

  // 查找是否已存在该属性
  for(int i = 0; i < XATTR_MAX_ATTRS; i++) {
    xe = &xi->attrs[i];
    if(xe->used && strncmp(xe->name, name, XATTR_NAME_MAX) == 0) {
      // 更新现有属性
      memmove(xe->value, value, size);
      xe->value_len = size;
      release(&xattr_table.lock);
      return 0;
    }
  }

  // 查找空槽位创建新属性
  for(int i = 0; i < XATTR_MAX_ATTRS; i++) {
    xe = &xi->attrs[i];
    if(!xe->used) {
      strncpy(xe->name, name, XATTR_NAME_MAX);
      memmove(xe->value, value, size);
      xe->value_len = size;
      xe->used = 1;
      release(&xattr_table.lock);
      return 0;
    }
  }

  release(&xattr_table.lock);
  return -1;  // 属性数量已满
}

// 获取扩展属性
int
xattr_get(struct inode *ip, char *name, char *value, int size)
{
  struct xattr_inode *xi;
  struct xattr_entry *xe;

  acquire(&xattr_table.lock);

  xi = xattr_find_or_create(ip->dev, ip->inum, 0);
  if(!xi) {
    release(&xattr_table.lock);
    return -1;  // 无属性
  }

  for(int i = 0; i < XATTR_MAX_ATTRS; i++) {
    xe = &xi->attrs[i];
    if(xe->used && strncmp(xe->name, name, XATTR_NAME_MAX) == 0) {
      if(size == 0) {
        // 仅返回长度
        int len = xe->value_len;
        release(&xattr_table.lock);
        return len;
      }
      if(size < xe->value_len) {
        release(&xattr_table.lock);
        return -1;  // 缓冲区太小
      }
      memmove(value, xe->value, xe->value_len);
      int len = xe->value_len;
      release(&xattr_table.lock);
      return len;
    }
  }

  release(&xattr_table.lock);
  return -1;  // 属性不存在
}

// 清除 inode 的所有扩展属性（在 inode 被删除时调用）
void
xattr_clear(struct inode *ip)
{
  struct xattr_inode *xi;

  acquire(&xattr_table.lock);

  xi = xattr_find_or_create(ip->dev, ip->inum, 0);
  if(xi) {
    xi->used = 0;  // 释放整个条目
  }

  release(&xattr_table.lock);
}
// 列出所有扩展属性名
int
xattr_list(struct inode *ip, char *list, int size)
{
  struct xattr_inode *xi;
  struct xattr_entry *xe;
  int total = 0;

  acquire(&xattr_table.lock);

  xi = xattr_find_or_create(ip->dev, ip->inum, 0);
  if(!xi) {
    release(&xattr_table.lock);
    return 0;  // 无属性
  }

  // 计算总长度
  for(int i = 0; i < XATTR_MAX_ATTRS; i++) {
    xe = &xi->attrs[i];
    if(xe->used) {
      total += strlen(xe->name) + 1;  // 包含 null 终止符
    }
  }

  if(size == 0) {
    release(&xattr_table.lock);
    return total;
  }

  if(size < total) {
    release(&xattr_table.lock);
    return -1;  // 缓冲区太小
  }

  // 复制属性名
  int offset = 0;
  for(int i = 0; i < XATTR_MAX_ATTRS; i++) {
    xe = &xi->attrs[i];
    if(xe->used) {
      int len = strlen(xe->name) + 1;
      memmove(list + offset, xe->name, len);
      offset += len;
    }
  }

  release(&xattr_table.lock);
  return total;
}

// 删除扩展属性
int
xattr_remove(struct inode *ip, char *name)
{
  struct xattr_inode *xi;
  struct xattr_entry *xe;

  acquire(&xattr_table.lock);

  xi = xattr_find_or_create(ip->dev, ip->inum, 0);
  if(!xi) {
    release(&xattr_table.lock);
    return -1;  // 无属性
  }

  for(int i = 0; i < XATTR_MAX_ATTRS; i++) {
    xe = &xi->attrs[i];
    if(xe->used && strncmp(xe->name, name, XATTR_NAME_MAX) == 0) {
      xe->used = 0;
      // 检查是否所有属性都已删除
      int any_used = 0;
      for(int j = 0; j < XATTR_MAX_ATTRS; j++) {
        if(xi->attrs[j].used) {
          any_used = 1;
          break;
        }
      }
      if(!any_used) {
        xi->used = 0;  // 释放 inode 条目
      }
      release(&xattr_table.lock);
      return 0;
    }
  }

  release(&xattr_table.lock);
  return -1;  // 属性不存在
}
