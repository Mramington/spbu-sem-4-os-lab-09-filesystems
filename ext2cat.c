 #define _POSIX_C_SOURCE 200809L
 #include "ext2.h"
 #include <inttypes.h>
 
 int write_all(const uint8_t *buf, size_t n)
 {
     while (n > 0) {
         ssize_t w = write(STDOUT_FILENO, buf, n);
         if (w <= 0) { perror("write"); return -1; }
         buf += w;
         n   -= (size_t)w;
     }
     return 0;
 }
 
 ssize_t emit_block(ext2_fs_t *fs, uint32_t blk,
                            uint64_t remaining, uint8_t *buf)
 {
     size_t n = (remaining < fs->block_size)
                ? (size_t)remaining
                : (size_t)fs->block_size;
 
     if (blk == 0) {
         memset(buf, 0, n);
     } else {
         if (ext2_read_block(fs, blk, buf) != 0)
             return -1;
     }
 
     return write_all(buf, n) == 0 ? (ssize_t)n : -1;
 }
 
 int emit_hole(ext2_fs_t *fs, uint64_t count,
                      uint64_t *remaining, uint8_t *buf)
 {
     for (uint64_t i = 0; i < count && *remaining > 0; i++) {
         ssize_t w = emit_block(fs, 0, *remaining, buf);
         if (w < 0) return -1;
         *remaining -= (uint64_t)w;
     }
     return 0;
 }
 int emit_single_indirect(ext2_fs_t *fs, uint32_t ind,
                                  uint64_t *remaining, uint8_t *buf,
                                  uint8_t *ibuf)
 {
     uint32_t ptrs = fs->block_size / 4;
 
     if (ind == 0)
         return emit_hole(fs, ptrs, remaining, buf);
 
     if (ext2_read_block(fs, ind, ibuf) != 0)
         return -1;
 
     for (uint32_t i = 0; i < ptrs && *remaining > 0; i++) {
         uint32_t blk = le32(((uint32_t *)ibuf)[i]);
         ssize_t w = emit_block(fs, blk, *remaining, buf);
         if (w < 0) return -1;
         *remaining -= (uint64_t)w;
     }
     return 0;
 }
 
 int emit_double_indirect(ext2_fs_t *fs, uint32_t dind,
                                  uint64_t *remaining, uint8_t *buf,
                                  uint8_t *ibuf, uint8_t *dibuf)
 {
     uint32_t ptrs = fs->block_size / 4;
 
     if (dind == 0)
         return emit_hole(fs, (uint64_t)ptrs * ptrs, remaining, buf);
 
     if (ext2_read_block(fs, dind, dibuf) != 0)
         return -1;
 
     for (uint32_t i = 0; i < ptrs && *remaining > 0; i++) {
         uint32_t ind = le32(((uint32_t *)dibuf)[i]);
         if (emit_single_indirect(fs, ind, remaining, buf, ibuf) != 0)
             return -1;
     }
     return 0;
 }
 
 int emit_triple_indirect(ext2_fs_t *fs, uint32_t tind,
                                  uint64_t *remaining, uint8_t *buf,
                                  uint8_t *ibuf, uint8_t *dibuf, uint8_t *tibuf)
 {
     uint32_t ptrs = fs->block_size / 4;
 
     if (tind == 0)
         return emit_hole(fs, (uint64_t)ptrs * ptrs * ptrs, remaining, buf);
 
     if (ext2_read_block(fs, tind, tibuf) != 0)
         return -1;
 
     for (uint32_t i = 0; i < ptrs && *remaining > 0; i++) {
         uint32_t dind = le32(((uint32_t *)tibuf)[i]);
         if (emit_double_indirect(fs, dind, remaining, buf, ibuf, dibuf) != 0)
             return -1;
     }
     return 0;
 }
 
 int main(int argc, char *argv[])
 {
     if (argc != 3) {
         fprintf(stderr, "Использование: %s <образ|устройство> <номер_иноды>\n",
                 argv[0]);
         return 1;
     }
 
     ext2_fs_t fs;
     if (ext2_open(argv[1], &fs) != 0)
         return 1;
 
     uint32_t ino = (uint32_t)strtoul(argv[2], NULL, 10);
     ext2_inode_t inode;
     if (ext2_read_inode(&fs, ino, &inode) != 0) {
         close(fs.fd); return 1;
     }
 
     uint16_t mode = le16(inode.i_mode);
     uint16_t fmt  = mode & 0xF000;
 
     if (fmt == EXT2_S_IFLNK && le32(inode.i_blocks) == 0) {
         uint32_t sz = le32(inode.i_size);
         if (sz <= 60) {
             write_all((uint8_t *)inode.i_block, sz);
             close(fs.fd);
             return 0;
         }
     }
 
     if (fmt != EXT2_S_IFREG && fmt != EXT2_S_IFDIR && fmt != EXT2_S_IFLNK) {
         fprintf(stderr, "Inode %u: не файл, каталог или симлинк\n", ino);
         close(fs.fd); return 1;
     }
 
     uint64_t size = (uint64_t)le32(inode.i_size);
     if (fmt == EXT2_S_IFREG)
         size |= (uint64_t)le32(inode.i_dir_acl) << 32;
 
     if (size == 0) {
         close(fs.fd); return 0;
     }
 
     uint8_t *buf   = malloc(fs.block_size);
     uint8_t *ibuf  = malloc(fs.block_size);
     uint8_t *dibuf = malloc(fs.block_size);
     uint8_t *tibuf = malloc(fs.block_size);
     if (!buf || !ibuf || !dibuf || !tibuf) {
         perror("malloc");
         free(buf); free(ibuf); free(dibuf); free(tibuf);
         close(fs.fd); return 1;
     }
 
     uint64_t remaining = size;
     int rc = 0;
 
     for (uint32_t i = 0; i < EXT2_NDIR_BLOCKS && remaining > 0; i++) {
         ssize_t w = emit_block(&fs, le32(inode.i_block[i]), remaining, buf);
         if (w < 0) { rc = -1; goto done; }
         remaining -= (uint64_t)w;
     }
 
     if (remaining > 0)
         if (emit_single_indirect(&fs, le32(inode.i_block[EXT2_IND_BLOCK]),
                                   &remaining, buf, ibuf) != 0)
             { rc = -1; goto done; }
 
     if (remaining > 0)
         if (emit_double_indirect(&fs, le32(inode.i_block[EXT2_DIND_BLOCK]),
                                   &remaining, buf, ibuf, dibuf) != 0)
             { rc = -1; goto done; }
 
     if (remaining > 0)
         if (emit_triple_indirect(&fs, le32(inode.i_block[EXT2_TIND_BLOCK]),
                                   &remaining, buf, ibuf, dibuf, tibuf) != 0)
             { rc = -1; goto done; }
 
 done:
     free(buf); free(ibuf); free(dibuf); free(tibuf);
     close(fs.fd);
     return (rc < 0) ? 1 : 0;
 }