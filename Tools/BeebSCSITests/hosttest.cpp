// Drive Scsi.cpp through its registers as ADFS does
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "BeebWin.h"
#include "FileStore.h"
#include "Model.h"
#include "Scsi.h"
int IntStatus; char HardDrivePath[260] = "/tmp/hatest-card"; LEDType LEDs; BeebWin W; BeebWin *mainWin = &W;
FileStoreDriveType FileStoreDrive[4] = { FileStoreDriveType::Auto, FileStoreDriveType::None, FileStoreDriveType::Auto, FileStoreDriveType::E20 };
Model MachineType = Model::B;
static int fails, passes;
static void check(const char *t, bool ok, const char *why) { if (ok) { passes++; printf("PASS %s\n", t); } else { fails++; printf("FAIL %s: %s\n", t, why); } }
enum { CND = 0x80, INO = 0x40, REQ = 0x20, BSY = 0x02, MSG = 0x01 };
static uint8_t st() { return SCSIRead(1); }
// one command; returns status byte or -1 (no answer)
static int cmd(uint8_t id, const uint8_t *cdb, int n, const uint8_t *out, int outn, uint8_t *in, int *inn)
{
   SCSIWrite(0, id); SCSIWrite(2, id);
   if (!(st() & BSY)) return -1;
   int c = 0, o = 0, i = 0, status = -1;
   for (int guard = 0; guard < 100000; guard++) {
      uint8_t s = st();
      if (!(s & BSY)) break;
      if (!(s & REQ)) continue;
      if ((s & CND) && !(s & INO) && !(s & MSG)) SCSIWrite(0, cdb[c++]);
      else if ((s & CND) && (s & INO) && !(s & MSG)) status = SCSIRead(0);
      else if ((s & CND) && (s & INO) && (s & MSG)) { SCSIRead(0); }
      else if (!(s & CND) && (s & INO)) { uint8_t b = SCSIRead(0); if (in) in[i] = b; i++; }
      else if (!(s & CND) && !(s & INO)) SCSIWrite(0, o < outn ? out[o++] : 0);
   }
   if (inn) *inn = i;
   return status;
}
int main()
{
   system("rm -rf /tmp/hatest-card; mkdir -p /tmp/hatest-card");
   system("cp " DEFSCSI " /tmp/hatest-card/");
   FILE *f = fopen("/tmp/hatest-card/scsi0.dat", "wb");
   for (int b = 0; b < 33 * 4 * 10; b++) { unsigned char blk[256]; memset(blk, b & 0xff, 256); fwrite(blk, 1, 256, f); }
   fclose(f);
   SCSIDriveEnabled = true;
   SCSIReset();
   char why[200]; static uint8_t in[70000]; int n;
   // ADFS (&9A6C) and the FileStore MOS (&FA1E) check the adaptor is there
   // by writing the data register and reading it back
   SCSIWrite(0, 0x5A); SCSIWrite(3, 0); uint8_t r1 = SCSIRead(0);
   SCSIWrite(0, 0xA5); SCSIWrite(3, 0); uint8_t r2 = SCSIRead(0);
   snprintf(why, sizeof why, "read %02X %02X (want 5A A5)", r1, r2);
   check("data register reads back what the host wrote (adaptor present)", r1 == 0x5A && r2 == 0xA5, why);
   const uint8_t tur[6] = { 0, 0, 0, 0, 0, 0 };
   int s = cmd(1, tur, 6, 0, 0, in, &n);
   snprintf(why, sizeof why, "status %d", s); check("TEST UNIT READY on drive 0 before it starts: not ready (as Pi1MHz)", s == 2, why);
   check("BSY drops at the end of the command", !(st() & 0x02), "BSY still set");
   const uint8_t rd[6] = { 0x08, 0, 0, 5, 2, 0 };
   s = cmd(1, rd, 6, 0, 0, in, &n);
   snprintf(why, sizeof why, "status %d, %d bytes, %02X %02X", s, n, in[0], in[256]);
   check("READ6 2 blocks from LBA 5", s == 0 && n == 512 && in[0] == 5 && in[256] == 6 && in[511] == 6, why);
   static uint8_t wr[512]; memset(wr, 0xA5, 512);
   const uint8_t w6[6] = { 0x0A, 0, 0, 7, 2, 0 };
   s = cmd(1, w6, 6, wr, 512, in, &n);
   const uint8_t r7[6] = { 0x08, 0, 0, 7, 2, 0 };
   int s2 = cmd(1, r7, 6, 0, 0, in, &n);
   snprintf(why, sizeof why, "write %d read %d, %02X", s, s2, in[300]);
   check("WRITE6 then READ6 round-trip", s == 0 && s2 == 0 && in[0] == 0xA5 && in[511] == 0xA5, why);
   const uint8_t inq[6] = { 0x12, 0, 0, 0, 36, 0 };
   s = cmd(1, inq, 6, 0, 0, in, &n);
   snprintf(why, sizeof why, "status %d, %d bytes", s, n);
   check("INQUIRY from defscsi.cfg (BEEBSCSI GENERIC HD)", s == 0 && n == 36 && memcmp(in + 8, "BEEBSCSI", 8) == 0, why);
   const uint8_t ms[6] = { 0x1A, 0, 0x26, 0, 255, 0 };
   s = cmd(1, ms, 6, 0, 0, in, &n);
   snprintf(why, sizeof why, "status %d, %d bytes", s, n);
   check("MODE SENSE page 38 has 'corn'", s == 0 && n >= 18 && memcmp(in + 14, "corn", 4) == 0, why);
   const uint8_t tur1[6] = { 0, 1 << 5, 0, 0, 0, 0 };
   s = cmd(1, tur1, 6, 0, 0, in, &n);
   const uint8_t rs[6] = { 0x03, 1 << 5, 0, 0, 4, 0 };
   s2 = cmd(1, rs, 6, 0, 0, in, &n);
   snprintf(why, sizeof why, "status %d, sense status %d byte %02X", s, s2, in[0]);
   check("drive 1 (no image): CHECK CONDITION, sense 02", s == 2 && s2 == 0 && in[0] == 0x02, why);
   s = cmd(1, tur, 6, 0, 0, in, &n);
   snprintf(why, sizeof why, "status %d", s); check("TEST UNIT READY on drive 0 after READ6 started it", s == 0, why);
   // a selection in the middle of a command abandons it
   SCSIWrite(0, 1); SCSIWrite(2, 1); SCSIWrite(0, 0x08); SCSIWrite(0, 0);
   s = cmd(1, tur, 6, 0, 0, in, &n);
   snprintf(why, sizeof why, "status %d", s); check("a new selection mid-command starts again", s == 0, why);
   // the drive LED is lit only during a command
   check("LED off at bus free", !LEDs.HDisc[0], "LED on");
   // FileStore: targets by ID bit; None does not answer; Auto needs the image
   MachineType = Model::FileStoreE01; SCSIReset();
   const uint8_t start[6] = { 0x1B, 0, 0, 0, 1, 0 };
   s = cmd(1, start, 6, 0, 0, in, &n); snprintf(why, sizeof why, "status %d", s);
   check("FileStore drive 0 (Auto, image) answers START UNIT", s == 0, why);
   s = cmd(2, tur, 6, 0, 0, in, &n); snprintf(why, sizeof why, "status %d", s);
   check("FileStore drive 1 (None) does not answer", s == -1, why);
   s = cmd(4, tur, 6, 0, 0, in, &n); snprintf(why, sizeof why, "status %d", s);
   check("FileStore drive 2 (Auto, no image) does not answer", s == -1, why);
   s = cmd(8, tur, 6, 0, 0, in, &n); snprintf(why, sizeof why, "status %d", s);
   check("FileStore drive 3 (E20, no image) answers, not ready", s == 2, why);
   s = cmd(1, tur, 6, 0, 0, in, &n); snprintf(why, sizeof why, "status %d", s);
   check("FileStore drive 0 still answers after an unanswered selection", s == 0, why);
   // IRQ follows REQ while enabled
   SCSIWrite(3, 1); SCSIWrite(0, 1); SCSIWrite(2, 1);
   check("IRQ with REQ while enabled", (IntStatus & 0x20) && (st() & 0x10), "no IRQ");
   SCSIWrite(3, 0);
   check("IRQ cleared when disabled", !(IntStatus & 0x20), "IRQ still set");
   SCSIReset();
   printf("%d passed, %d failed\n", passes, fails);
   return fails != 0;
}
