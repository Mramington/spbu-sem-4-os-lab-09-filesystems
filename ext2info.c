#define _POSIX_C_SOURCE 200809L
#include "ext2.h"
#include <time.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

const char *filetype_str(uint16_t mode)
{
    switch (mode & 0xF000) {
    case EXT2_S_IFREG: return "regular file";
    case EXT2_S_IFDIR: return "directory";
    case EXT2_S_IFLNK: return "symbolic link";
    case EXT2_S_IFBLK: return "block device";
    case EXT2_S_IFCHR: return "character device";
    case EXT2_S_IFIFO: return "fifo";
    case EXT2_S_IFSOCK: return "socket";
    default: return "unknown";
    }
}

void print_perms(uint16_t mode)
{
    char s[11];
    uint16_t fmt = mode & 0xF000;
    s[0] = (fmt == EXT2_S_IFDIR)  ? 'd' :
            (fmt == EXT2_S_IFLNK)  ? 'l' :
            (fmt == EXT2_S_IFBLK)  ? 'b' :
            (fmt == EXT2_S_IFCHR)  ? 'c' :
            (fmt == EXT2_S_IFIFO)  ? 'p' :
            (fmt == EXT2_S_IFSOCK) ? 's' : '-';
    s[1] = (mode & 0400) ? 'r' : '-';
    s[2] = (mode & 0200) ? 'w' : '-';
    s[3] = (mode & 0100) ? ((mode & 04000) ? 's' : 'x')
                           : ((mode & 04000) ? 'S' : '-');
    s[4] = (mode & 0040) ? 'r' : '-';
    s[5] = (mode & 0020) ? 'w' : '-';
    s[6] = (mode & 0010) ? ((mode & 02000) ? 's' : 'x')
                           : ((mode & 02000) ? 'S' : '-');
    s[7] = (mode & 0004) ? 'r' : '-';
    s[8] = (mode & 0002) ? 'w' : '-';
    s[9] = (mode & 0001) ? ((mode & 01000) ? 't' : 'x')
                           : ((mode & 01000) ? 'T' : '-');
    s[10] = '\0';
    printf("  %-12s %s  (octal %04o)\n", "Mode:", s, mode & 07777);
}

void print_time(const char *label, uint32_t t)
{
    time_t tt = (time_t)t;
    char buf[64];
    struct tm *tm = gmtime(&tt);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S UTC", tm);
    printf("  %-12s %s\n", label, buf);
}

#define MAX_SHOW 64u

void print_block_map(ext2_fs_t *fs, ext2_inode_t *inode, uint64_t size)
{
    uint32_t block_size = fs->block_size;
    uint64_t num_logical = (size + block_size - 1) / block_size;

    printf("\n--- Logical block map");
    if (num_logical == 0) {
        printf(" (empty file) ---\n");
        return;
    }
    printf(" (%" PRIu64 " logical blocks", num_logical);
    if (num_logical > MAX_SHOW)
        printf(", showing first %u", MAX_SHOW);
    printf(") ---\n");

    uint64_t show = (num_logical < MAX_SHOW) ? num_logical : MAX_SHOW;
    uint64_t holes = 0;

    for (uint64_t i = 0; i < show; i++) {
        uint32_t phys = ext2_inode_get_block(fs, inode, (uint32_t)i);
        if (phys == (uint32_t)-1) {
            fprintf(stderr, "  Error resolving logical block %" PRIu64 "\n", i);
            break;
        }
        if (phys == 0) {
            printf("  logical[%6" PRIu64 "] -> hole (sparse)\n", i);
            holes++;
        } else {
            printf("  logical[%6" PRIu64 "] -> physical block %u\n", i, phys);
        }
    }
    if (num_logical > MAX_SHOW)
        printf("  ... (%" PRIu64 " more logical blocks not shown)\n",
               num_logical - MAX_SHOW);
    if (holes > 0)
        printf("  (%llu hole(s) in shown range)\n", (unsigned long long)holes);
}

int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <image-or-device> <inode-number>\n", argv[0]);
        return 1;
    }

    char *endp;
    unsigned long ino_arg = strtoul(argv[2], &endp, 10);
    if (*endp != '\0' || ino_arg == 0) {
        fprintf(stderr, "Invalid inode number: %s\n", argv[2]);
        return 1;
    }

    ext2_fs_t fs;
    if (ext2_open(argv[1], &fs) != 0) return 1;

    ext2_inode_t inode;
    if (ext2_read_inode(&fs, (uint32_t)ino_arg, &inode) != 0) {
        ext2_close(&fs);
        return 1;
    }

    uint16_t mode  = le16(inode.i_mode);
    uint64_t size  = ext2_inode_size(&inode);
    uint32_t uid   = le16(inode.i_uid);
    uint32_t gid   = le16(inode.i_gid);
    uint32_t links = le16(inode.i_links_count);
    uint32_t blks  = le32(inode.i_blocks);
    uint32_t flags = le32(inode.i_flags);

    printf("=== Inode %lu on %s ===\n\n", ino_arg, argv[1]);
    printf("  %-12s %s\n", "Type:", filetype_str(mode));
    print_perms(mode);
    printf("  %-12s %u\n",          "UID:",    uid);
    printf("  %-12s %u\n",          "GID:",    gid);
    printf("  %-12s %" PRIu64 " bytes\n", "Size:", size);
    printf("  %-12s %u  (= %" PRIu64 " bytes in 512-unit blocks)\n",
           "i_blocks:", blks, (uint64_t)blks * 512);
    printf("  %-12s %u\n",          "Links:",  links);
    printf("  %-12s 0x%08x\n",      "Flags:",  flags);
    printf("  %-12s %u\n",          "Generation:", le32(inode.i_generation));
    print_time("atime:", le32(inode.i_atime));
    print_time("ctime:", le32(inode.i_ctime));
    print_time("mtime:", le32(inode.i_mtime));
    if (le32(inode.i_dtime))
        print_time("dtime:", le32(inode.i_dtime));

    printf("\n--- Block pointer fields (i_block[]) ---\n");
    printf("  Direct blocks (i_block[0..11]):\n");
    for (int i = 0; i < EXT2_NDIR_BLOCKS; i++) {
        uint32_t b = le32(inode.i_block[i]);
        if (b)
            printf("    [%2d] physical block %u\n", i, b);
        else
            printf("    [%2d] 0 (hole)\n", i);
    }
    printf("  Indirect block pointer  (i_block[12]): %u%s\n",
           le32(inode.i_block[EXT2_IND_BLOCK]),
           le32(inode.i_block[EXT2_IND_BLOCK]) ? "" : "  (not used)");
    printf("  Dbl-indirect pointer    (i_block[13]): %u%s\n",
           le32(inode.i_block[EXT2_DIND_BLOCK]),
           le32(inode.i_block[EXT2_DIND_BLOCK]) ? "" : "  (not used)");
    printf("  Tri-indirect pointer    (i_block[14]): %u%s\n",
           le32(inode.i_block[EXT2_TIND_BLOCK]),
           le32(inode.i_block[EXT2_TIND_BLOCK]) ? "" : "  (not used)");

    print_block_map(&fs, &inode, size);

    ext2_close(&fs);
    return 0;
}