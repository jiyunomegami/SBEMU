#include "au_cards.h"

#define hw_fm_outb(reg,data) outp(aui->fm_port+reg,data)
#define hw_fm_inb(reg) inp(aui->fm_port+reg)

uint8_t ioport_fm_read (struct mpxplay_audioout_info_s *aui, unsigned int idx)
{
  return hw_fm_inb(idx);
}

void ioport_fm_write (struct mpxplay_audioout_info_s *aui, unsigned int idx, uint8_t data)
{
  hw_fm_outb(idx, data);
}

#define hw_mpu_outb(reg,data) outp(aui->mpu401_port+reg,data)
#define hw_mpu_inb(reg) inp(aui->mpu401_port+reg)

uint8_t ioport_mpu401_read (struct mpxplay_audioout_info_s *aui, unsigned int idx)
{
  return hw_mpu_inb(idx);
}

void ioport_mpu401_write (struct mpxplay_audioout_info_s *aui, unsigned int idx, uint8_t data)
{
#if 0 // shouldn't DOS programs be doing this themselves if it was really necessary?
  if (idx == 0 && aui->mpu401_softread) {
    int timeout = 10000; // 100ms
    do {
      uint8_t st = hw_mpu_inb(1);
      if (!(st & 0x40)) break;
      // still full
      pds_delay_10us(1);
    } while (--timeout);
  }
#endif
  hw_mpu_outb(idx, data);
}

void ioport_mpu401_write_when_ready (struct mpxplay_audioout_info_s *aui, unsigned int idx, uint8_t data)
{
  if (idx == 0) {
    int timeout = 10000; // 100ms
    do {
      uint8_t st = hw_mpu_inb(1);
      if (!(st & 0x40)) break;
      // still full
      pds_delay_10us(1);
    } while (--timeout);
  }
  hw_mpu_outb(idx, data);
}
