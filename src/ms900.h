/* ms900.h - Dell Pro Premium Mouse MS900 DPI protocol (plain C API).
 *
 * Decoded from a USB capture of Dell Display and Peripheral Manager:
 *   HID SET_REPORT, Feature, report ID 0x08, 32 bytes, receiver interface 2
 *     08 13 01 0d 02 <dpi lo> <dpi hi> 00 ... 00
 *   Ack on the input endpoint:
 *     01 0b 01 02 0d <nonce> 01        (last byte 01 = accepted)
 */
#ifndef MS900_H
#define MS900_H

#ifdef __cplusplus
extern "C" {
#endif

#define MS900_DPI_MIN   800
#define MS900_DPI_MAX   8000
#define MS900_DPI_STEP  200
#define MS900_MAX_DEVS  8

typedef struct {
    char path[64];      /* /dev/hidrawN */
    char kind[32];      /* "USB receiver" / "Bluetooth" */
} ms900_dev;

/* Fill devs[], return count found. */
int  ms900_find(ms900_dev *devs, int max);

/* Build the 32-byte feature report for a DPI value. */
void ms900_build_report(unsigned char out[32], int dpi);

/* Send the DPI command. Returns an open fd (O_NONBLOCK) to poll for the ack,
 * or -errno on failure. Caller must close() it. */
int  ms900_send(const char *path, int dpi);

/* Non-blocking ack check: 1 = accepted, 0 = rejected, -1 = nothing yet. */
int  ms900_poll_ack(int fd);

int  ms900_valid_dpi(int dpi);
int  ms900_load_last(void);          /* 0 if none */
void ms900_save_last(int dpi);

#ifdef __cplusplus
}
#endif
#endif
