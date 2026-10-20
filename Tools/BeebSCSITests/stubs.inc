#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "BeebSCSI/filesystem.h"
#include "BeebSCSI/hostadapter.h"
#include "BeebSCSI/scsi.h"

/* BeebEm harness: Pi1MHz's test_beebscsi.c run against BeebEm's port.
   The card is a scratch folder; Pi1MHz's /BeebSCSI<n> folders are
   subfolders of it, chosen with filesystemSetLunDirectory as there, and
   /Pi1MHz/defscsi.cfg is the user data folder's defscsi.cfg. */

static char root[256];
static unsigned lunDirectory;

static void map_path(const char *path, char *out, size_t size)
{
   if (strncmp(path, "/Pi1MHz/", 8) == 0)
      snprintf(out, size, "%s/%s", root, path + 8);
   else
      snprintf(out, size, "%s%s", root, path);
}

void filesystemSetLunDirectory(uint8_t host, uint8_t dir)
{
   (void)host;
   char folder[300];
   snprintf(folder, sizeof folder, "%s/BeebSCSI%u", root, dir);
   mkdir(folder, 0777);
   if (dir != lunDirectory) {
      lunDirectory = dir;
      filesystemSetPaths(folder, root);
      filesystemReset();           /* other drives: as a jukebox */
   }
}
uint8_t filesystemGetLunDirectory(void) { return (uint8_t)lunDirectory; }
bool filesystemMounted(void) { return true; }

/* FatFs, as far as the tests use it, on the scratch folder */
typedef unsigned int UINT;
typedef int FRESULT;
#define FR_OK 0
#define FR_NO_FILE 4
#define FA_READ 0x01
#define FA_WRITE 0x02
#define FA_CREATE_ALWAYS 0x08
#define AM_RDO 0x01
typedef struct { FILE *fp; } FIL;
typedef struct { unsigned long fsize; } FILINFO;

FRESULT __real_f_open(FIL *f, const char *path, int mode)
{
   char p[300];
   map_path(path, p, sizeof p);
   f->fp = fopen(p, (mode & FA_CREATE_ALWAYS) ? "wb" : (mode & FA_WRITE) ? "rb+" : "rb");
   return f->fp ? FR_OK : FR_NO_FILE;
}
FRESULT __real_f_close(FIL *f) { if (f->fp) fclose(f->fp); f->fp = NULL; return FR_OK; }
FRESULT f_write(FIL *f, const void *d, UINT n, UINT *done) { *done = (UINT)fwrite(d, 1, n, f->fp); return FR_OK; }
FRESULT f_read(FIL *f, void *d, UINT n, UINT *done) { *done = (UINT)fread(d, 1, n, f->fp); return FR_OK; }
FRESULT f_stat(const char *path, FILINFO *fi)
{
   char p[300]; struct stat st;
   map_path(path, p, sizeof p);
   if (stat(p, &st) != 0) return FR_NO_FILE;
   fi->fsize = (unsigned long)st.st_size;
   return FR_OK;
}
FRESULT f_unlink(const char *path) { char p[300]; map_path(path, p, sizeof p); return remove(p) == 0 ? FR_OK : FR_NO_FILE; }
FRESULT f_mkdir(const char *path) { char p[300]; map_path(path, p, sizeof p); mkdir(p, 0777); return FR_OK; }
static bool leaked_on(const char *) { return false; }

/* A scripted host for the bus tests (run_cmd): it selects, then supplies
   the CDB and any data-out bytes from bus_in[]; data-in bytes land in
   bus_out[] and the status byte in bus_status. */
static bool     bus_sel;
static uint8_t  bus_host = 1;
static uint8_t  bus_in[32768];
static unsigned bus_in_len, bus_in_pos;
static uint8_t  bus_out[32768];
static unsigned bus_out_len;
static int      bus_status = -1;
static bool     ph_msg, ph_cd, ph_io;
static bool     bus_reset;
static int      reset_after_in = -1;
static unsigned dma_short;

static uint8_t bus_in_next(void)
{
   uint8_t b = bus_in_pos < bus_in_len ? bus_in[bus_in_pos] : 0;
   bus_in_pos++;
   if (reset_after_in >= 0 && bus_in_pos >= (unsigned)reset_after_in)
      bus_reset = true;
   return b;
}

uint8_t hostadapterReadDatabus(void) { return bus_host; }
uint8_t hostadapterReadByte(void) { return bus_in_next(); }
void hostadapterWriteByte(uint8_t v)
{
   if (ph_cd && ph_io && !ph_msg) bus_status = v;
   else if (!ph_cd && ph_io && bus_out_len < sizeof bus_out) bus_out[bus_out_len++] = v;
}
uint32_t hostadapterPerformReadDMALen(const uint8_t *b, uint32_t len)
{
   for (unsigned i = 0; i < len && bus_out_len < sizeof bus_out; i++)
      bus_out[bus_out_len++] = b[i];
   return len;
}
uint32_t hostadapterPerformWriteDMALen(uint8_t *b, uint32_t len)
{
   unsigned n = dma_short ? dma_short : len;
   dma_short = 0;
   for (unsigned i = 0; i < n; i++)
      b[i] = bus_in_next();
   return n;
}
uint32_t hostadapterPerformReadDMA(const uint8_t *b) { return hostadapterPerformReadDMALen(b, 256); }
uint32_t hostadapterPerformWriteDMA(uint8_t *b) { return hostadapterPerformWriteDMALen(b, 256); }
bool hostadapterConnectedToExternalBus(void) { return true; }
void hostadapterWriteResetFlag(bool f) { bus_reset = f; }
bool hostadapterReadResetFlag(void) { return bus_reset; }
void hostadapterWriteDataPhaseFlags(bool m, bool c, bool i) { ph_msg = m; ph_cd = c; ph_io = i; }
void hostadapterWriteBusyFlag(bool f) { if (f) bus_sel = false; }
void hostadapterWriteRequestFlag(bool f) { (void)f; }
bool hostadapterReadSelectFlag(void) { return bus_sel; }
bool hostadapterTargetAttached(uint8_t) { return true; }
void statusledActivity(uint8_t) {}

