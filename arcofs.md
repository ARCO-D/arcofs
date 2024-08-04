# minix文件系统分析
## 主要结构体
```c
// file 操作结构
const struct file_operations minix_file_operations = {
	.llseek		= generic_file_llseek,
	.read_iter	= generic_file_read_iter,
	.write_iter	= generic_file_write_iter,
	.mmap		= generic_file_mmap,
	.fsync		= generic_file_fsync,
	.splice_read	= generic_file_splice_read,
};

// file inode操作结构
const struct inode_operations minix_file_inode_operations = {
	.setattr	= minix_setattr,
	.getattr	= minix_getattr,
};

// dir操作结构
const struct file_operations minix_dir_operations = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.iterate	= minix_readdir,
	.fsync		= generic_file_fsync,
};

// dir inode操作结构
const struct inode_operations minix_dir_inode_operations = {
	.create		= minix_create,
	.lookup		= minix_lookup,
	.link		= minix_link,
	.unlink		= minix_unlink,
	.symlink	= minix_symlink,
	.mkdir		= minix_mkdir,
	.rmdir		= minix_rmdir,
	.mknod		= minix_mknod,
	.rename		= minix_rename,
	.getattr	= minix_getattr,
	.tmpfile	= minix_tmpfile,
};

// 地址空间操作结构
static const struct address_space_operations minix_aops = {
	.readpage = minix_readpage,
	.writepage = minix_writepage,
	.write_begin = minix_write_begin,
	.write_end = generic_write_end,
	.bmap = minix_bmap
};

// minix inode信息
struct minix_inode_info {
	union {
		__u16 i1_data[16];
		__u32 i2_data[16];
	} u;
	struct inode vfs_inode;
};

// minix 超级块信息
struct minix_sb_info {
	unsigned long s_ninodes;
	unsigned long s_nzones;
	unsigned long s_imap_blocks;
	unsigned long s_zmap_blocks;
	unsigned long s_firstdatazone;
	unsigned long s_log_zone_size;
	unsigned long s_max_size;
	int s_dirsize;
	int s_namelen;
	struct buffer_head ** s_imap;
	struct buffer_head ** s_zmap;
	struct buffer_head * s_sbh;
	struct minix_super_block * s_ms;
	unsigned short s_mount_state;
	unsigned short s_version;
};

// minix 超级块结构
struct minix_super_block {
	__u16 s_ninodes;
	__u16 s_nzones;
	__u16 s_imap_blocks;
	__u16 s_zmap_blocks;
	__u16 s_firstdatazone;
	__u16 s_log_zone_size;
	__u32 s_max_size;
	__u16 s_magic;
	__u16 s_state;
	__u32 s_zones;
};

// minix inode结构
struct minix_inode {
	__u16 i_mode;
	__u16 i_uid;
	__u32 i_size;
	__u32 i_time;
	__u8  i_gid;
	__u8  i_nlinks;
	__u16 i_zone[9];
};

// minix dir entry
struct minix_dir_entry {
	__u16 inode;
	char name[0];
};


```

## 从fill_super开始的函数调用
```c
minix_fill_super
    sb_set_blocksize // 设置块大小供下一步读取
    sb_bread // 读出超级块
    // 设置bitmap
    // 设备空间检查是否足以格式化
    minix_iget
    d_make_root

minix_iget
    alloc_inode // 在iget_locked内部
    V1_minix_iget
        raw_inode = minix_V1_raw_inode // 读取原始minix inode
        // 根据原始minix inode填充inode
        minix_set_inode

minix_V1_raw_inode
    block = 2 + sbi->s_imap_blocks + sbi->s_zmap_blocks + ino / MINIX_INODES_PER_BLOCK; // 获取此inode所在的块号
    *bh = sb_bread // 读取该块
	p = (void *)(*bh)->b_data; // p是该块起始地址的制造
	return p + ino % MINIX_INODES_PER_BLOCK; // 返回inode在该块中的偏移地址

minix_set_inode
    根据inode的i_mode类型区分文件、目录、链接
    挂载不同的operations
```

## 获取文件系统块
```c
// 下列函数实现均在minix域中
V1_minix_get_block
    get_block
        block_to_path
        get_branch
        alloc_branch
        splice_branch

block_to_path
/* 
 * 参数  : inode, block, offsets
 * 返回值: 间接块层级
 * 
 * offsets[0]直接块[1]间接块[2]双重[3]三重
 */


// 顺便提下ext2的
ext2_block_to_path
/* 
 * 参数  : inode, block, offsets, boundary
 * 返回值: 间接块层级
 * 
 * offsets[0]直接块[1]间接块[2]双重[3]三重
 * 当为间接、双重、三重时，offsets[0]会被置为12/13/14
 * 在ext2_get_branch离会进行while(depth--)来add_chain
 */

```


## read系统调用
```c
minix_file_operations.read_iter
    generic_file_read_iter
        do_deneric_file_read

do_generic_file_read
    mapping->a_ops->readpage // minix_aops的回调函数

minix_readpage
    blokc_read_full_page
```