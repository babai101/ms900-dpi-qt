#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "ms900.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#define DELL_VID      0x413C
#define RECEIVER_PID  0x2119   /* Dell Secure Link Receiver */
#define REPORT_ID     0x08
#define REPORT_LEN    32

void ms900_build_report(unsigned char out[REPORT_LEN], int dpi)
{
    memset(out, 0, REPORT_LEN);
    out[0] = REPORT_ID;
    out[1] = 0x13;
    out[2] = 0x01;      /* receiver slot */
    out[3] = 0x0D;      /* command: DPI */
    out[4] = 0x02;      /* payload length */
    out[5] = (unsigned char)(dpi & 0xFF);
    out[6] = (unsigned char)((dpi >> 8) & 0xFF);
}

int ms900_valid_dpi(int dpi)
{
    return dpi >= MS900_DPI_MIN && dpi <= MS900_DPI_MAX && dpi % MS900_DPI_STEP == 0;
}

/* Walk a HID report descriptor looking for a Report ID item == rid. */
static int has_report_id(const unsigned char *d, size_t n, int rid)
{
    size_t i = 0;
    while (i < n) {
        unsigned char b = d[i];
        if (b == 0xFE) {                     /* long item */
            i += 3 + (i + 1 < n ? d[i + 1] : 0);
            continue;
        }
        static const int sizes[4] = {0, 1, 2, 4};
        int sz = sizes[b & 3];
        if ((b & 0xFC) == 0x84 && sz == 1 && i + 1 < n && d[i + 1] == rid)
            return 1;
        i += 1 + sz;
    }
    return 0;
}

static size_t read_file(const char *path, void *buf, size_t max)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    size_t n = fread(buf, 1, max, f);
    fclose(f);
    return n;
}

int ms900_find(ms900_dev *devs, int max)
{
    DIR *dir = opendir("/sys/class/hidraw");
    if (!dir) return 0;

    /* collect + sort names so hidraw2 comes before hidraw10 */
    int nums[256], cnt = 0;
    struct dirent *e;
    while ((e = readdir(dir)) && cnt < 256)
        if (strncmp(e->d_name, "hidraw", 6) == 0)
            nums[cnt++] = atoi(e->d_name + 6);
    closedir(dir);
    for (int a = 0; a < cnt; a++)
        for (int b = a + 1; b < cnt; b++)
            if (nums[b] < nums[a]) { int t = nums[a]; nums[a] = nums[b]; nums[b] = t; }

    int found = 0;
    for (int k = 0; k < cnt && found < max; k++) {
        char p[128], uevent[2048];
        unsigned char desc[4096];

        snprintf(p, sizeof p, "/sys/class/hidraw/hidraw%d/device/uevent", nums[k]);
        size_t un = read_file(p, uevent, sizeof uevent - 1);
        if (!un) continue;
        uevent[un] = 0;

        snprintf(p, sizeof p, "/sys/class/hidraw/hidraw%d/device/report_descriptor", nums[k]);
        size_t dn = read_file(p, desc, sizeof desc);
        if (!dn) continue;

        const char *id = strstr(uevent, "HID_ID=");
        unsigned bus, vid, pid;
        if (!id || sscanf(id, "HID_ID=%x:%x:%x", &bus, &vid, &pid) != 3) continue;
        if (vid != DELL_VID || !has_report_id(desc, dn, REPORT_ID)) continue;
        if (bus == 0x03 && pid != RECEIVER_PID) continue;

        snprintf(devs[found].path, sizeof devs[found].path, "/dev/hidraw%d", nums[k]);
        snprintf(devs[found].kind, sizeof devs[found].kind, "%s",
                 bus == 0x03 ? "USB receiver" : bus == 0x05 ? "Bluetooth" : "other bus");
        found++;
    }
    return found;
}

int ms900_send(const char *path, int dpi)
{
    unsigned char buf[REPORT_LEN];
    int fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return -errno;

    /* drain stale input so we only see the ack for this command */
    unsigned char junk[64];
    while (read(fd, junk, sizeof junk) > 0) {}

    ms900_build_report(buf, dpi);
    if (ioctl(fd, HIDIOCSFEATURE(REPORT_LEN), buf) < 0) {
        int err = errno;
        close(fd);
        return -err;
    }
    /* Dell's software reads the report back right after; do the same. */
    memset(buf, 0, sizeof buf);
    buf[0] = REPORT_ID;
    ioctl(fd, HIDIOCGFEATURE(REPORT_LEN), buf);
    return fd;
}

int ms900_poll_ack(int fd)
{
    unsigned char d[64];
    ssize_t n;
    while ((n = read(fd, d, sizeof d)) > 0) {
        if (n >= 7 && d[0] == 0x01 && d[1] == 0x0B && d[3] == 0x02 && d[4] == 0x0D)
            return d[6] == 0x01 ? 1 : 0;
    }
    return -1;
}

static void state_path(char *out, size_t n)
{
    const char *xdg = getenv("XDG_CACHE_HOME");
    if (xdg && *xdg) snprintf(out, n, "%s/ms900-dpi", xdg);
    else             snprintf(out, n, "%s/.cache/ms900-dpi", getenv("HOME") ? getenv("HOME") : ".");
}

int ms900_load_last(void)
{
    char p[512], s[16] = {0};
    state_path(p, sizeof p);
    if (!read_file(p, s, sizeof s - 1)) return 0;
    int v = atoi(s);
    return ms900_valid_dpi(v) ? v : 0;
}

void ms900_save_last(int dpi)
{
    char p[512];
    state_path(p, sizeof p);
    char *slash = strrchr(p, '/');
    if (slash) { *slash = 0; mkdir(p, 0755); *slash = '/'; }
    FILE *f = fopen(p, "w");
    if (f) { fprintf(f, "%d\n", dpi); fclose(f); }
}
