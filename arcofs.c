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
#define ARCOFS_BLOCK_SIZE 1024
#define ARCOFS_MAGIC   0x27266673 // 0x6673 is the ascii of 'fs'

/*
 * #1
 * arcofs文件系统结构体 
 */
 // VFS的super block
 //   s_fs_info(指向一个sbi对象)
 //     s_as(指向arcofs的super block结构)
struct arcofs_super_block {
    unsigned int s_magic;
    int s_inodes_count;
    int s_free_inodes_count;
    int s_blocks_count;
    int s_free_blocks_count;
    char pad[1004];
};

struct arcofs_inode {
    /*00*/ int i_mode;
    /*04*/ int i_size;
    /*08*/ int i_block[8];
    /*40*/ char filename[12];
    /*52*/ char pad[12];
};

struct arcofs_bytemap {
    unsigned char idx[1024];
};

struct arcofs_sb_info {
    int version;
    struct arcofs_super_block *s_as;
};

/*
 * #2
 * 函数声明 
 */
int arcofs_writepage(struct page *page, struct writeback_control *wbc);
static int arcofs_readpage(struct file *file, struct page *page);
static sector_t arcofs_bmap(struct address_space *mapping, sector_t block);
int arcofs_get_block(struct inode * inode, sector_t block, struct buffer_head *bh, int create);


void arcofs_set_inode(struct inode *inode, dev_t rdev);
struct inode *arcofs_new_inode(const struct inode *dir, umode_t mode, const char *name);
static int arcofs_mknod(struct inode * dir, struct dentry *dentry, umode_t mode, dev_t rdev);
static int arcofs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl);
static struct dentry *arcofs_lookup(struct inode * dir, struct dentry *dentry, unsigned int flags);
static int arcofs_unlink(struct inode * dir, struct dentry *dentry);
//static int arcofs_getattr(struct vfsmount *mnt, struct dentry *dentry, struct kstat *stat); // ubuntu16内核不一致，暂不实现
static int arcofs_readdir(struct file *file, struct dir_context *ctx);

static ssize_t arcofs_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t arcofs_write(struct file *, const char __user *, size_t, loff_t *);

static int arcofs_statfs(struct dentry *dentry, struct kstatfs *buf);

struct inode *arcofs_iget(struct super_block *sb, unsigned long ino);


/*
 * #3
 * 操作结构声明
 */
// 地址空间操作结构
static const struct address_space_operations arcofs_aops = {
	.readpage = arcofs_readpage,
	.writepage = arcofs_writepage,
	// .write_begin = arcofs_write_begin,
	// .write_end = generic_write_end,
	.bmap = arcofs_bmap,
};

// dir操作结构
const struct inode_operations arcofs_dir_inode_operations = {
	.create		= arcofs_create,
	.lookup		= arcofs_lookup,
	// .link		= arcofs_link,
    .unlink		= arcofs_unlink,
	// .symlink	= arcofs_symlink,
	// .mkdir		= arcofs_mkdir,
    // .rmdir		= arcofs_rmdir,
	.mknod		= arcofs_mknod,
	// .rename		= arcofs_rename,
//    .getattr	= arcofs_getattr,  // 这个指针类型不匹配, 看下是不是4.15内核改了(果然!ubuntu16内核里头文件不一样,我改了内核头文件(呵呵,改完挂了
	// .tmpfile	= arcofs_tmpfile,
};
const struct file_operations arcofs_dir_operations = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
    .iterate	= arcofs_readdir,
	.fsync		= generic_file_fsync,
};

// file操作结构
 const struct inode_operations arcofs_file_inode_operations = {
// 	.setattr	= arcofs_setattr,
// 	.getattr	= arcofs_getattr,
 };
 const struct file_operations arcofs_file_operations = {
 	.llseek		= generic_file_llseek,
//    .read       = arcofs_read,
    .write      = arcofs_write,
 	.read_iter	= generic_file_read_iter,
// 	.write_iter	= generic_file_write_iter, // 这两个太高级了, 没玩明白, 先注释掉
 	.mmap		= generic_file_mmap,
    .open		= dquot_file_open,
 	.fsync		= generic_file_fsync,
 	.splice_read	= generic_file_splice_read,
 };

// 超级块操作结构
static const struct super_operations arcofs_sops = {
	// .alloc_inode	= arcofs_alloc_inode,
	// .destroy_inode	= arcofs_destroy_inode,
	// .write_inode	= arcofs_write_inode,
	// .evict_inode	= arcofs_evict_inode,
	// .put_super	= arcofs_put_super,
	.statfs		= arcofs_statfs,
	// .remount_fs	= arcofs_remount,
};



/*
 * #4
 * 操作结构回调函数实现
 */
// ##4.1 aops方法实现
int arcofs_writepage(struct page *page, struct writeback_control *wbc)
{
	return block_write_full_page(page, arcofs_get_block, wbc);
}

static int arcofs_readpage(struct file *file, struct page *page)
{
	return block_read_full_page(page, arcofs_get_block);
}

static sector_t arcofs_bmap(struct address_space *mapping, sector_t block)
{
	return generic_block_bmap(mapping,block, arcofs_get_block);
}

int arcofs_get_block(struct inode * inode, sector_t block, struct buffer_head *bh, int create)
{
    printk("arco-fs: try get block %ld\n", block);
    map_bh(bh, inode->i_sb, block);
    return 0;
}


// ##4.2 dir方法实现
void arcofs_set_inode(struct inode *inode, dev_t rdev)
{
    // 判断inode的i_mode，挂载不同的操作结构
	if (S_ISREG(inode->i_mode)) {
        printk("arco-fs: inode type file\n");
        inode->i_op = &arcofs_file_inode_operations;
        inode->i_fop = &arcofs_file_operations;
		inode->i_mapping->a_ops = &arcofs_aops;
	}
    else if (S_ISDIR(inode->i_mode)) {
        printk("arco-fs: inode type dir\n");
		inode->i_op = &arcofs_dir_inode_operations;
		inode->i_fop = &arcofs_dir_operations;
		inode->i_mapping->a_ops = &arcofs_aops;
	}
    else {
        printk("arco-fs: unsupport inode type 0x%x\n", inode->i_mode);
        init_special_inode(inode, inode->i_mode, rdev);
    }
}

struct inode *arcofs_new_inode(const struct inode *dir, umode_t mode, const char *name)
{
    int i;
	struct super_block *sb = dir->i_sb;
	struct arcofs_sb_info *sbi = sb->s_fs_info;
	struct inode *inode = new_inode(sb);

    struct buffer_head* inode_bytemap_block;
    inode_bytemap_block = sb_bread(sb, 3); // block number of inode bytemap
    unsigned char *inode_bytemap_arr = (unsigned char*)inode_bytemap_block->b_data;

    // 在inode bytemap中 找到一个空闲的inode
    for (i = 0; i < sbi->s_as->s_inodes_count; i++) {
        printk("arco-fs: inode[%d] state=%d\n", i, inode_bytemap_arr[i]);
        if (inode_bytemap_arr[i] == 1) {
            printk("arco-fs: find inode[%d] free\n", i);
            inode_bytemap_arr[i] = 2;
            inode->i_ino = i + 1; // ino号从1而不是从0开始
            break;
        }
    }
    inode->i_blocks = 0;
    inode->i_mode = S_IFREG; // create出来的一律是file, mkdir出来的才是dir

    // 在磁盘中创建一个arcofs inode
    struct buffer_head* inode_table_block;
    inode_table_block = sb_bread(sb, 4); // block number of inode bytemap
    struct arcofs_inode *inode_table_arr = (struct arcofs_inode*)inode_table_block->b_data;
    inode_table_arr[i].i_mode = S_IFREG;
    strcpy(inode_table_arr[i].filename, name); // 用
    // 没有加入dentry的动作

	// insert_inode_hash(inode);
    mark_inode_dirty(inode);
    mark_buffer_dirty(inode_bytemap_block);
    mark_buffer_dirty(inode_table_block);

    return inode;
}

static int arcofs_add_nondir(struct dentry *dentry, struct inode *inode)
{
    // ext2这里还做了其他操作，但我没看懂
    d_instantiate(dentry, inode);
    return 0;
}

// umode_t是unsigned short类型
static int arcofs_mknod(struct inode * dir, struct dentry *dentry, umode_t mode, dev_t rdev)
{
	int error = 0;
	struct inode *inode;

	if (!old_valid_dev(rdev))
		return -EINVAL;

    printk("arco-fs: try touch file name (%s)\n", dentry->d_name.name);

	inode = arcofs_new_inode(dir, mode, dentry->d_name.name);

	if (inode) {
		arcofs_set_inode(inode, rdev);
		mark_inode_dirty(inode);
        error = arcofs_add_nondir(dentry, inode);
	}
	return error;
}

static int arcofs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
	return arcofs_mknod(dir, dentry, mode, 0);
}


ino_t arcofs_inode_by_name(struct dentry *dentry)
{
    return 0;
}

static struct dentry *arcofs_lookup(struct inode * dir, struct dentry *dentry, unsigned int flags)
{
	struct inode * inode = NULL;
	ino_t ino;

	ino = arcofs_inode_by_name(dentry);
	if (ino) {
		inode = arcofs_iget(dir->i_sb, ino);
		if (IS_ERR(inode))
			return ERR_CAST(inode);
	}
	d_add(dentry, inode);
	return NULL;
}

static int arcofs_unlink(struct inode * dir, struct dentry *dentry)
{
    printk("arco-fs: execute unlink\n");
    return 0;
}

int rflag = 1;
static int arcofs_readdir(struct file *file, struct dir_context *ctx)
{
    int i;
    rflag = !rflag;
    printk("arco-fs: execute readdir rflag=%d\n", rflag);
    struct buffer_head* inode_table_block;
    struct inode* inode = file_inode(file);
    struct super_block* sb = inode->i_sb;
    struct arcofs_sb_info *sbi = sb->s_fs_info;

    // 已经读过是1, return(我暂时没明白这里为什么会读两次
    if (rflag) return 0;

    // 读出inode表里的filename，arcofs没有专门设置dentry区
    inode_table_block = sb_bread(sb, 4); // block number of inode bytemap
    struct arcofs_inode *inode_table_arr = (struct arcofs_inode*)inode_table_block->b_data;
    // 并不一定是连续分布的, 所以每个都要过一遍
    for (i = 0; i < sbi->s_as->s_inodes_count; i++) {
        if (inode_table_arr[i].i_mode != 0) {
            printk("arco-fs: inode[%d] filename:%s\n", i, inode_table_arr[i].filename);
            unsigned l = strnlen(inode_table_arr[i].filename, sizeof(((struct arcofs_inode*)NULL)->filename));
            // 下面的参数i+1就是文件的inode号
            if (!dir_emit(ctx, inode_table_arr[i].filename, l, i + 1, DT_UNKNOWN)) {
                return 0;
            }
        }
        ctx->pos += sizeof(((struct arcofs_inode*)NULL)->filename);
    }

    return 0;
}

static ssize_t arcofs_read(struct file *, char __user *, size_t, loff_t *)
{
    return 0;
}

static ssize_t arcofs_write(struct file *, const char __user *, size_t, loff_t *)
{

}



// ##4.3 file方法实现
// 暂无

// ##4.4 super block方法实现
static int arcofs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	struct arcofs_sb_info *sbi = sb->s_fs_info;
    
	buf->f_type = sb->s_magic;
	buf->f_bsize = sb->s_blocksize;
	buf->f_blocks = sbi->s_as->s_blocks_count;
	buf->f_bfree = sbi->s_as->s_free_blocks_count;
	buf->f_bavail = buf->f_bfree;
	buf->f_files = (sbi->s_as->s_inodes_count - sbi->s_as->s_free_inodes_count);
	buf->f_ffree = sbi->s_as->s_free_inodes_count;

	return 0;
}


/*
 * #5
 * 文件系统挂载函数实现
 * fill_super相关
 */
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
    inode->i_mode = raw_inode->i_mode; // i_mode是文件类型

    arcofs_set_inode(inode, 0);

    return inode;
}


static int arcofs_fill_super(struct super_block *s, void *data, int silent)
{
    int err = -1;
    struct arcofs_sb_info *sbi;
    struct inode *root_inode;
    struct buffer_head *bh, *bh2, *bh3;
    struct arcofs_super_block *as;

    sbi = kzalloc(sizeof(struct arcofs_sb_info), GFP_KERNEL);
    if (!sbi)
        return -ENOMEM;
    s->s_fs_info = sbi;

    // 设置sb->s_blocksize
    if (!sb_set_blocksize(s, ARCOFS_BLOCK_SIZE))
        goto out_bad_hblock;

    if (!(bh = sb_bread(s, 1)))
        goto out_bad_sb;
    
    if (!(bh2 = sb_bread(s, 2)))
        goto out_bad_map;
    
    if (!(bh3 = sb_bread(s, 3)))
        goto out_bad_map;

    // 把上一步读取出的块作为arcofs super_block
    as = (struct arcofs_super_block*) bh->b_data;
    sbi->s_as = as;

	s->s_magic = as->s_magic;

    // 判断block是否足够

    // 注册super block操作结构
    s->s_op = &arcofs_sops;

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
    
    out_bad_map:
    printk("arco-fs: unable to read block and inode map\n");

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


