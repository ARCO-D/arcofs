#include <linux/module.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/parser.h>
#include <linux/random.h>
#include <linux/buffer_head.h>
#include <linux/exportfs.h>
#include <linux/vfs.h>
#include <linux/seq_file.h>
#include <linux/mount.h>
#include <linux/log2.h>
#include <linux/quotaops.h>
#include <linux/uaccess.h>
#include <linux/delay.h>

#define ARCOFS_VERSION "0.1"
#define ARCOFS_MAGIC   0x27266673 // 0x6673 is the ascii of 'fs'

#define ARCOFS_BLOCK_SIZE 1024

/*
 * description:
 * as ext2, reserved first block for boot partition
 * block 1: super block
 * block 2: block bitmap
 * block 3: inode bitmap
 * block 4: inode table
 * block 5+ data area
*/

struct arcofs_super_block {
    int s_inodes_count;
    int s_free_inodes_count;
    int s_blocks_count;
    int s_free_blocks_count;
    char pad[1008];
};

struct arcofs_inode {
    /*00*/ int i_mode;
    /*04*/ int i_size;
    /*08*/ int i_block[8];
    /*40*/ char filename[12];
    /*52*/ char pad[12];
};

struct arcofs_sb_info {
    int version;
    struct arcofs_super_block *s_as;
};



struct arcofs_inode* arcofs_raw_inode(struct super_block *sb, int ino, struct buffer_head **bh)
{
    int block;
    struct arcofs_inode *ai;
    
    ino -= 1;
    block = 4; // inode表所在的块号

    *bh = sb_bread(sb, block);
    if (!*bh) {
        printk("arco-fs: Unable to read inode block\n");
        return NULL;
    }

    ai = (void*)(*bh)->b_data; // 此inode所在块的起始地址
    return ai + (ino * sizeof(struct arcofs_inode)); // 加块内偏移地址
    
}

struct inode *arcofs_iget(struct super_block *sb, unsigned long ino)
{
    struct inode *inode;
    struct buffer_head *bh;
    struct arcofs_inode *raw_inode;

    inode = iget_locked(sb, ino);
    if (!inode) {
        printk("arco-fs: iget_locked failed\n");
        return ERR_PTR(-ENOMEM);
    }
    // 获取原始arcofs inode
    raw_inode = arcofs_raw_inode(inode->i_sb, inode->i_ino, &bh);
    // 拼装VFS inode
    inode->i_size = raw_inode->i_size; // i_size是文件大小

    return inode;
}


static int arcofs_fill_super(struct super_block *s, void *data, int silent)
{
    int err = -1;
    struct arcofs_sb_info *sbi;
    struct inode *root_inode;
    struct buffer_head *bh;
    struct arcofs_super_block *as;

    sbi = kzalloc(sizeof(struct arcofs_sb_info), GFP_KERNEL);
    if (!sbi)
        return -ENOMEM;
    s->s_fs_info = sbi;

    if (!sb_set_blocksize(s, ARCOFS_BLOCK_SIZE))
        goto out_bad_hblock;

    if (!(bh = sb_bread(s, 1)))
        goto out_bad_sb;

    // 把上一步读取出的块作为arcofs super_block
    as = (struct arcofs_super_block*) bh->b_data;
    sbi->s_as = as;

    // 设置bit map

    // 判断block是否足够

    // 获取根目录的inode(inode号从1开始)
    root_inode = arcofs_iget(s, 1);
	if (IS_ERR(root_inode)) {
		err = PTR_ERR(root_inode);
		goto out_no_root;
	}

    // dmake root
    s->s_root = d_make_root(root_inode);
    if (!s->s_root)
        goto out_no_root;

    printk("arco-fs: fill super seems ok\n");

    return 0;
    
    out_bad_hblock:
    printk("arco-fs: blocksize too small for device\n");

    out_bad_sb:
    printk("arco-fs: unable to read superblock\n");

    out_no_root:
    printk("arco-fs: no root error\n");

    return err;
}


static struct dentry *arcofs_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return mount_bdev(fs_type, flags, dev_name, data, arcofs_fill_super);
}


static struct file_system_type arcofs_fs_type = {
    .owner    = THIS_MODULE,
    .name     = "arcofs",
	.mount    = arcofs_mount,
    .kill_sb  = kill_block_super,
    .fs_flags = FS_REQUIRES_DEV,
};
MODULE_ALIAS_FS("arcofs");

static int __init init_arcofs_fs(void)
{
    int ret;
    printk("arco-fs: version:%s\n", ARCOFS_VERSION);
    ret = register_filesystem(&arcofs_fs_type);
    return ret;
}

static void __exit exit_arcofs_fs(void)
{
    unregister_filesystem(&arcofs_fs_type);
}


MODULE_AUTHOR("ARCO");
MODULE_DESCRIPTION("ARCO Filesystem");
MODULE_LICENSE("GPL");
module_init(init_arcofs_fs);
module_exit(exit_arcofs_fs);


