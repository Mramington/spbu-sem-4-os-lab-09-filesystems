#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#  define HOST_BIG_ENDIAN 1
#else
#  define HOST_BIG_ENDIAN 0
#endif

uint16_t le16(uint16_t v) {
#if HOST_BIG_ENDIAN
    return (uint16_t)((v >> 8) | (v << 8));
#else
    return v;
#endif
}

uint32_t le32(uint32_t v) {
#if HOST_BIG_ENDIAN
    return ((v & 0x000000FFu) << 24) |
           ((v & 0x0000FF00u) <<  8) |
           ((v & 0x00FF0000u) >>  8) |
           ((v & 0xFF000000u) >> 24);
#else
    return v;
#endif
}

const char *ftype_name(uint8_t ft) {
    switch (ft) {
    case 1: return "REG";
    case 2: return "DIR";
    case 3: return "CHR";
    case 4: return "BLK";
    case 5: return "FIFO";
    case 6: return "SOCK";
    case 7: return "LNK";
    default: return "???";
    }
}

int parse_dir(const uint8_t *data, size_t size) {
    size_t offset = 0;
    int count = 0;
    printf("%-10s  %-6s  %s\n", "INODE", "TYPE", "NAME");
    printf("----------  ------  ----\n");

    while (offset + 8 <= size) {
        uint32_t ino;
        uint16_t rec_len;
        uint8_t  name_len;
        uint8_t  file_type;

        memcpy(&ino, data + offset, 4);
        memcpy(&rec_len, data + offset + 4, 2);
        memcpy(&name_len, data + offset + 6, 1);
        memcpy(&file_type, data + offset + 7, 1);

        ino     = le32(ino);
        rec_len = le16(rec_len);

        if (rec_len < 8) {
            fprintf(stderr, "Corrupt dirent at offset %zu (rec_len=%u)\n",
                    offset, rec_len);
            return -1;
        }

        if (ino != 0) {
            char name[256];
            size_t nlen = name_len;
            if (nlen > 255) nlen = 255;
            memcpy(name, data + offset + 8, nlen);
            name[nlen] = '\0';
            printf("%-10u  %-6s  %s\n", ino, ftype_name(file_type), name);
            count++;
        }
        offset += rec_len;
    }
    printf("--- %d entries ---\n", count);
    return 0;
}

int main(int argc, char *argv[]) {
    int fd = STDIN_FILENO;
    if (argc >= 2) {
        fd = open(argv[1], O_RDONLY);
        if (fd < 0) { perror(argv[1]); return 1; }
    }

    size_t cap = 65536, len = 0;
    uint8_t *buf = malloc(cap);
    if (!buf) { perror("malloc"); return 1; }

    for (;;) {
        if (len == cap) {
            cap *= 2;
            uint8_t *nb = realloc(buf, cap);
            if (!nb) { perror("realloc"); free(buf); return 1; }
            buf = nb;
        }
        ssize_t r = read(fd, buf + len, cap - len);
        if (r < 0) { perror("read"); free(buf); return 1; }
        if (r == 0) break;
        len += (size_t)r;
    }
    if (fd != STDIN_FILENO) close(fd);

    int rc = parse_dir(buf, len);
    free(buf);
    return rc < 0 ? 1 : 0;
}