#define _POSIX_C_SOURCE 200809L
#ifndef EXT2_H
#define EXT2_H

#include <stdint.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#  define HOST_BIG_ENDIAN 1
#else
#  define HOST_BIG_ENDIAN 0
#endif

static inline uint16_t le16(uint16_t v) {
#if HOST_BIG_ENDIAN
    return (uint16_t)((v >> 8) | (v << 8));
#else
    return v;
#endif
}

static inline uint32_t le32(uint32_t v) {
#if HOST_BIG_ENDIAN
    return ((v & 0x000000FFu) << 24) |
           ((v & 0x0000FF00u) <<  8) |
           ((v & 0x00FF0000u) >>  8) |
           ((v & 0xFF000000u) >> 24);
#else
    return v;
#endif
}

static inline uint64_t le64(uint64_t v) {
#if HOST_BIG_ENDIAN
    uint32_t lo = (uint32_t)(v & 0xFFFFFFFFu);
    uint32_t hi = (uint32_t)(v >> 32);
    return ((uint64_t)le32(lo) << 32) | (uint64_t)le32(hi);
#else
    return v;
#endif
}

#define EXT2_SUPER_MAGIC        0xEF53
#define EXT2_SUPERBLOCK_OFFSET  1024

typedef struct {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count;
    uint32_t s_r_blocks_count;
    uint32_t s_free_blocks_count;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;
    uint32_t s_log_frag_size;
    uint32_t s_blocks_per_group;
    uint32_t s_frags_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;
    uint32_t s_first_ino;
    uint16_t s_inode_size;
    uint16_t s_block_group_nr;
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
    uint8_t  s_uuid[16];
    char     s_volume_name[16];
    char     s_last_mounted[64];
    uint32_t s_algo_bitmap;
    uint8_t  _pad[820];
} __attribute__((packed)) ext2_superblock_t;

typedef struct {
    uint32_t bg_block_bitmap;
    uint32_t bg_inode_bitmap;
    uint32_t bg_inode_table;
    uint16_t bg_free_blocks_count;
    uint16_t bg_free_inodes_count;
    uint16_t bg_used_dirs_count;
    uint16_t bg_pad;
    uint8_t  bg_reserved[12];
} __attribute__((packed)) ext2_bgd_t;

#define EXT2_NDIR_BLOCKS 12
#define EXT2_IND_BLOCK   12
#define EXT2_DIND_BLOCK  13
#define EXT2_TIND_BLOCK  14
#define EXT2_N_BLOCKS    15

typedef struct {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint32_t i_block[EXT2_N_BLOCKS];
    uint32_t i_generation;
    uint32_t i_file_acl;
    uint32_t i_dir_acl;
    uint32_t i_faddr;
    uint8_t  i_osd2[12];
} __attribute__((packed)) ext2_inode_t;

typedef struct {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[255];
} __attribute__((packed)) ext2_dirent_t;

#define EXT2_FT_UNKNOWN  0
#define EXT2_FT_REG_FILE 1
#define EXT2_FT_DIR      2
#define EXT2_FT_CHRDEV   3
#define EXT2_FT_BLKDEV   4
#define EXT2_FT_FIFO     5
#define EXT2_FT_SOCK     6
#define EXT2_FT_SYMLINK  7

#define EXT2_S_IFSOCK 0xC000
#define EXT2_S_IFLNK  0xA000
#define EXT2_S_IFREG  0x8000
#define EXT2_S_IFBLK  0x6000
#define EXT2_S_IFDIR  0x4000
#define EXT2_S_IFCHR  0x2000
#define EXT2_S_IFIFO  0x1000

typedef struct {
    int            fd;
    ext2_superblock_t sb;
    uint32_t       block_size;
    uint32_t       inodes_per_group;
    uint16_t       inode_size;
} ext2_fs_t;

static inline int ext2_open(const char *path, ext2_fs_t *fs) {
    fs->fd = open(path, O_RDONLY);
    if (fs->fd < 0) { perror(path); return -1; }
    if (pread(fs->fd, &fs->sb, sizeof(fs->sb), EXT2_SUPERBLOCK_OFFSET)
            != sizeof(fs->sb)) {
        fprintf(stderr, "Cannot read superblock\n"); close(fs->fd); return -1;
    }
    if (le16(fs->sb.s_magic) != EXT2_SUPER_MAGIC) {
        fprintf(stderr, "Not an ext2 filesystem (magic 0x%04x)\n",
                le16(fs->sb.s_magic));
        close(fs->fd); return -1;
    }
    fs->block_size = 1024u << le32(fs->sb.s_log_block_size);
    fs->inodes_per_group = le32(fs->sb.s_inodes_per_group);
    fs->inode_size = (le32(fs->sb.s_rev_level) >= 1)
                     ? le16(fs->sb.s_inode_size) : 128;
    return 0;
}

static inline int ext2_read_block(ext2_fs_t *fs, uint32_t blk, void *buf) {
    if (blk == 0) { memset(buf, 0, fs->block_size); return 0; }
    off_t off = (off_t)blk * fs->block_size;
    ssize_t r = pread(fs->fd, buf, fs->block_size, off);
    if (r != (ssize_t)fs->block_size) {
        fprintf(stderr, "pread block %u: %s\n", blk, strerror(errno));
        return -1;
    }
    return 0;
}

static inline int ext2_read_inode(ext2_fs_t *fs, uint32_t ino,
                                   ext2_inode_t *inode) {
    if (ino == 0) { fprintf(stderr, "inode 0 is invalid\n"); return -1; }
    uint32_t group = (ino - 1) / fs->inodes_per_group;
    uint32_t index = (ino - 1) % fs->inodes_per_group;

    uint32_t bgd_block = le32(fs->sb.s_first_data_block) + 1;
    off_t bgd_off = (off_t)bgd_block * fs->block_size
                    + (off_t)group * sizeof(ext2_bgd_t);
    ext2_bgd_t bgd;
    if (pread(fs->fd, &bgd, sizeof(bgd), bgd_off) != sizeof(bgd)) {
        perror("pread bgd"); return -1;
    }

    uint32_t inode_table_blk = le32(bgd.bg_inode_table);
    off_t inode_off = (off_t)inode_table_blk * fs->block_size
                      + (off_t)index * fs->inode_size;
    if (pread(fs->fd, inode, sizeof(ext2_inode_t), inode_off)
            != sizeof(ext2_inode_t)) {
        perror("pread inode"); return -1;
    }
    return 0;
}

#endif /* EXT2_H */

static inline uint64_t ext2_inode_size(const ext2_inode_t *inode)
{
    uint64_t lo = le32(inode->i_size);
    /* i_dir_acl is the high 32 bits of size only for regular files */
    if ((le16(inode->i_mode) & 0xF000) == EXT2_S_IFREG)
        return lo | ((uint64_t)le32(inode->i_dir_acl) << 32);
    return lo;
}

static inline uint32_t ext2_inode_get_block(
    ext2_fs_t *fs,
    ext2_inode_t *inode,
    uint32_t logical
) {
    uint32_t ptrs = fs->block_size / 4;

    if (logical < EXT2_NDIR_BLOCKS)
        return le32(inode->i_block[logical]);
    logical -= EXT2_NDIR_BLOCKS;

    uint8_t *buf = (uint8_t *)malloc(fs->block_size);
    if (!buf) { perror("malloc"); return (uint32_t)-1; }
    uint32_t result = (uint32_t)-1;

    // single
    if (logical < ptrs) {
        uint32_t ind = le32(inode->i_block[EXT2_IND_BLOCK]);
        if (ind == 0) { result = 0; goto done; }
        if (pread(fs->fd, buf, fs->block_size,
                  (off_t)ind * fs->block_size) != (ssize_t)fs->block_size)
            goto done;
        uint32_t v; memcpy(&v, buf + logical * 4, 4);
        result = le32(v);
        goto done;
    }
    logical -= ptrs;

    // double
    if (logical < ptrs * ptrs) {
        uint32_t dind = le32(inode->i_block[EXT2_DIND_BLOCK]);
        if (dind == 0) { result = 0; goto done; }
        if (pread(fs->fd, buf, fs->block_size,
                  (off_t)dind * fs->block_size) != (ssize_t)fs->block_size)
            goto done;
        uint32_t v1; memcpy(&v1, buf + (logical / ptrs) * 4, 4);
        uint32_t l1 = le32(v1);
        if (l1 == 0) { result = 0; goto done; }
        if (pread(fs->fd, buf, fs->block_size,
                  (off_t)l1 * fs->block_size) != (ssize_t)fs->block_size)
            goto done;
        uint32_t v2; memcpy(&v2, buf + (logical % ptrs) * 4, 4);
        result = le32(v2);
        goto done;
    }
    logical -= ptrs * ptrs;

    // triple
    {
        uint32_t tind = le32(inode->i_block[EXT2_TIND_BLOCK]);
        if (tind == 0) { result = 0; goto done; }
        if (pread(fs->fd, buf, fs->block_size,
                  (off_t)tind * fs->block_size) != (ssize_t)fs->block_size)
            goto done;
        uint32_t v1; memcpy(&v1, buf + (logical / (ptrs * ptrs)) * 4, 4);
        uint32_t l1 = le32(v1);
        if (l1 == 0) { result = 0; goto done; }
        if (pread(fs->fd, buf, fs->block_size,
                  (off_t)l1 * fs->block_size) != (ssize_t)fs->block_size)
            goto done;
        uint32_t rem = logical % (ptrs * ptrs);
        uint32_t v2; memcpy(&v2, buf + (rem / ptrs) * 4, 4);
        uint32_t l2 = le32(v2);
        if (l2 == 0) { result = 0; goto done; }
        if (pread(fs->fd, buf, fs->block_size,
                  (off_t)l2 * fs->block_size) != (ssize_t)fs->block_size)
            goto done;
        uint32_t v3; memcpy(&v3, buf + (rem % ptrs) * 4, 4);
        result = le32(v3);
    }

done:
    free(buf);
    return result;
}

static inline void ext2_close(ext2_fs_t *fs)
{
    if (fs && fs->fd >= 0) { close(fs->fd); fs->fd = -1; }
}