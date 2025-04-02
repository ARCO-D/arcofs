[TOC]

# 前言
ext2文件系统分析

# 系统函数
map_bh
```c
static inline void
map_bh(struct buffer_head *bh, struct super_block *sb, sector_t block)
{
	set_buffer_mapped(bh);
	bh->b_bdev = sb->s_bdev;
	bh->b_blocknr = block;
	bh->b_size = sb->s_blocksize;
}

// set_buffer_mapped是宏生成的函数
BUFFER_FNS(Mapped, mapped)
static __always_inline void set_buffer_mapped(struct buffer_head *bh)
{
	if (!test_bit(BH_Mapped, &(bh)->b_state))
		set_bit(BH_Mapped, &(bh)->b_state);
}
```
set_buffer_mapped : 写了一个标致bit位
bh->b_bdev        : 设置了这个bh的bdev (对于arcofs来说，应该是loop设备
bh->b_blocknr     : 设置block号(仅一个block
bh->b_size        : 设置块大小





# ext2 get_block相关函数
## ext2_get_block
```c
int ext2_get_block(struct inode *inode, sector_t iblock, struct buffer_head *bh_result, int create)
	ret = ext2_get_blocks(inode, iblock, max_blocks, &bno, &new, &boundary, create);
	map_bh(bh_result, inode->i_sb, bno);
	bh_result->b_size = (ret << inode->i_blkbits);
```
传入i_block(文件块号)，调用ext2_get_blocks获取bno(文件系统块号)
调用map_bh设置bh_result的属性
但然后又用ret重设了bh_result的b_size，为什么？
1) 如果ext2_get_blocks异常的话，ret返回的是错误码
2) 如果正常的话，ret的值是从ext2_blks_to_allocate返回的count



## ext2_block_to_path
```c
static int ext2_block_to_path(struct inode *inode, long i_block, int offsets[4], int *boundary)
```
```
i_block: 文件块号，是对于一个文件而言的，比如说第0~4095字节是文件的第一个块，第4096~8191字节就是第二个文件块
bno    : 文件系统块号，是对于文件系统来讲，位于设备上的第x个块
offsets[4]里面装的是0-3级间接块指针的值
ptrs_bits是用于运算的快捷值: 2^ptrs_bits = block_size / sizeof(ino), 当块大小为4096字节、块号占4字节的情景时，2^ptrs_bits = 1024, => ptrs_bits = 10
i_block & (ptrs - 1)是求余的快捷运算，等价于i_block % ptrs

1) 块号在直接块范围内: offsets[0] = i_block, 直接根据这个直接块里的值x，去读文件系统的第x个块
2) 块号在1级间接块范围内: offsets[0] = EXT2_IND_BLOCK, offsets[1] = i_block
	比方说，文件系统想读文件的第200个块了，怎么在文件系统里查找这个块？
	文件块号是200，ind01 = inode.i_block[EXT2_IND_BLOCK], ind01是1级间接块号，然后在文件系统里找到ind01这个块， dev[ind01]这个块里放满了块号，第i_block个单元，指向最终存放文件内容的 文件系统块号
2) 块号在2级间接块范围内: offsets[0] = EXT2_DIND_BLOCK, offsets[1] = i_block / 1024, offsets[2] = i_block % 1024
	比方说，文件系统想读文件的第20000个块了，怎么在文件系统里查找这个块？
	ind12 = inode.i_block[EXT2_DIND_BLOCK], ind12是2级间接块的1级间接块号， 在文件系统里找到dev[ind12]这个块，dev[ind12]里的每个bno，都指向了一个2级间接块的2级间接块ind22，每个ind22里有1024个块号
	所以ind12里的每个单元，都能映射1024个文件系统块号，所以在计算偏移时这里要/1024
	ind12 = 20000/1024 = 19，所以文件块号20000落在2级间接块第1级间接块的第19个单元，dev[ind12][19]指向的就是2级间接块的第2级间接块ind22
	ind22中的第20000 % 1024个单元 的值就是最终的文件系统块号
```

## ext2_get_branch
```c
typedef struct {
	__le32	*p;
	__le32	key;
	struct buffer_head *bh;
} Indirect;
static Indirect *ext2_get_branch(struct inode *inode, int depth, int *offsets, Indirect chain[4], int *err)
```


## ext2_find_goal
```c
static inline ext2_fsblk_t ext2_find_goal(struct inode *inode, long block, Indirect *partial)
```



## ext2_blks_to_allocate
```c
static int ext2_blks_to_allocate(Indirect * branch, int k, unsigned long blks, int blocks_to_boundary)
```



## ext2_alloc_branch
```c
static int ext2_alloc_branch(struct inode *inode, int indirect_blks, int *blks, ext2_fsblk_t goal, int *offsets, Indirect *branch)
```

















