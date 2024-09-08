#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<sys/stat.h>
#include<sys/mman.h>
#include<fcntl.h>
#include<errno.h>

#define ARCOFS_BLOCK_SIZE 1024
#define ARCOFS_MAGIC   0x27266673 // 0x6673 is the ascii of 'fs'

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

int main(int argc, char* argv[])
{
    char filename[256];

    /* 合法校验 */
    if (argc != 2) {
        printf("mkarcofs: arg num error\n");
        return -1;
    }
    strcpy(filename, argv[1]);
    struct stat st;
    if (stat(filename, &st) != 0) {
        printf("mkarcofs: file %s is not exist\n", filename);
        return -1;
    }
    // 判断是否为文件
    if (!(st.st_mode & S_IFREG)) {
        printf("mkarcofs: %s is not file", filename);
        return -1;
    }
    // 计算文件大小是否能进行格式化(最少16kb)
    printf("mkarcofs: file size=%ldbyte\n", st.st_size);
    if (st.st_size * 8 < 16 * 1024) {
        printf("mkarcofs: file size too small, can't make arcofs\n");
        return -1;
    }


    /* 使用mmap映射文件到内存 */
    int fd, maplen = st.st_size, block_num;
    void* start = NULL;
    fd = open(filename, O_RDWR);
    if (fd <= 0) {
        printf("mkarcofs: open %s failed\n", filename);
        return -1;
    }
    start = (char*)mmap(NULL, maplen, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (start <= 0) {
        printf("mkarcofs: mmap failed errno:%s\n", strerror(errno));
    }
    // 跳过reserved块
    start += ARCOFS_BLOCK_SIZE;

    /* 计算可分配的block数量 */
    block_num = st.st_size / 1024 - 1;
    printf("mkarcofs: block_num=%d\n", block_num);

    /* 格式化super_block */
    struct arcofs_super_block *sb = malloc(sizeof(struct arcofs_super_block));
    sb->s_magic = ARCOFS_MAGIC;
    sb->s_inodes_count = (ARCOFS_BLOCK_SIZE / sizeof(struct arcofs_inode));
    sb->s_free_inodes_count = sb->s_inodes_count - 2; // .和..
    sb->s_blocks_count = block_num - 4;
    sb->s_free_blocks_count = block_num - 4;
    memset(sb->pad, 0, sizeof(sb->pad));
    printf("start addr:%p\n", start);
    printf("sb addr:%p\n", sb);
    printf("sizeof sb=%ld\n", sizeof(struct arcofs_super_block));
    memcpy(start, sb, sizeof(struct arcofs_super_block));
    start += ARCOFS_BLOCK_SIZE;

    /* 格式化block bitmap */
    memset(start, 1, ARCOFS_BLOCK_SIZE); // memset按uchar填充
    struct arcofs_bytemap *blockmap = (struct arcofs_bytemap*)start;
    blockmap->idx[0] = 2;
    blockmap->idx[1] = 2;
    start += ARCOFS_BLOCK_SIZE;

    /* 格式化inode bitmap */
    memset(start, 1, ARCOFS_BLOCK_SIZE);
    struct arcofs_bytemap *inodemap = (struct arcofs_bytemap*)start;
    inodemap->idx[0] = 2;
    inodemap->idx[1] = 2;
    start += ARCOFS_BLOCK_SIZE;

    /* 格式化inode table */
    // 创建.目录inode
    memset(start, 0, ARCOFS_BLOCK_SIZE);
    struct arcofs_inode *node_dot = malloc(sizeof(struct arcofs_inode));
    node_dot->i_mode = S_IFDIR;
    strcpy(node_dot->filename, ".");
    memset(node_dot->i_block, 0, sizeof(node_dot->i_block));
    memset(node_dot->pad, 0, sizeof(node_dot->pad));
    memcpy(start, node_dot, sizeof(struct arcofs_inode));
    start += sizeof(struct arcofs_inode);
    // 创建..目录inode
    memset(start, 0, ARCOFS_BLOCK_SIZE);
    struct arcofs_inode *node_dotdot = malloc(sizeof(struct arcofs_inode));
    node_dotdot->i_mode = S_IFDIR;
    strcpy(node_dotdot->filename, "..");
    memset(node_dotdot->i_block, 0, sizeof(node_dotdot->i_block));
    memset(node_dotdot->pad, 0, sizeof(node_dotdot->pad));
    memcpy(start, node_dotdot, sizeof(struct arcofs_inode));
    start += sizeof(struct arcofs_inode);

    munmap(start, maplen);
    return 0;
}