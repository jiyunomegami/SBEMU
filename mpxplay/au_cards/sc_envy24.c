// Envy24(ice1724) driver for SBEMU
// based on the Linux driver(ice1712)

#include "au_linux.h"

#ifdef AU_CARDS_LINK_ENVY24

#define ENVY24_DEBUG 2

#if ENVY24_DEBUG
#define envy24dbg(...) do { DBG_Logi("Envy24: "); DBG_Logi(__VA_ARGS__); } while (0)
#else
#define envy24dbg(...)
#endif

#include "dmairq.h"
#include "pcibios.h"
#include "dpmi/dbgutil.h"

static pci_device_s envy24_devices[] = {
  {"Envy24", 0x1412, 0x1724, 0},
  {NULL,0,0,0}
};

struct envy24_card_s {
  struct au_linux_card card;
};

extern struct snd_pcm_ops snd_vt1724_playback_pro_ops;
static struct snd_pcm_ops *envy24_ops = &snd_vt1724_playback_pro_ops;
extern int snd_vt1724_probe (struct snd_card *card, struct pci_dev *pci, int probe_only);
extern irqreturn_t snd_vt1724_interrupt(int irq, void *dev_id);

static void ENVY24_close (struct mpxplay_audioout_info_s *aui)
{
  struct envy24_card_s *card = aui->card_private_data;
  if (card) {
    au_linux_close_card(&card->card);
    pds_free(card);
    aui->card_private_data = NULL;
  }
}

static void ENVY24_card_info (struct mpxplay_audioout_info_s *aui)
{
  struct envy24_card_s *card = aui->card_private_data;
  char sout[100];
  sprintf(sout, "ENVY24 : %s (%4.4X) IRQ %u", card->card.pci_dev->device_name, card->card.pci_dev->device_id, card->card.irq);
  pds_textdisplay_printf(sout);
}

static int ENVY24_adetect (struct mpxplay_audioout_info_s *aui)
{
  struct envy24_card_s *card;
  int err;

  envy24dbg("adetect\n");

  card = (struct envy24_card_s *)pds_zalloc(sizeof(struct envy24_card_s));
  if (!card)
    return 0;
  if (au_linux_find_card(aui, &card->card, envy24_devices) < 0)
    goto err_adetect;

  envy24dbg("pci subsystem %4.4X:%4.4X\n", card->card.linux_pci_dev->subsystem_vendor, card->card.linux_pci_dev->subsystem_device);
  int probe_only = aui->card_controlbits & AUINFOS_CARDCNTRLBIT_TESTCARD;
  err = snd_vt1724_probe(card->card.linux_snd_card, card->card.linux_pci_dev, probe_only);
  if (err) goto err_adetect;

  envy24dbg("ENVY24 : %s (%4.4X) IRQ %u\n", card->card.pci_dev->device_name, card->card.pci_dev->device_id, card->card.irq);

  return 1;

err_adetect:
  ENVY24_close(aui);
  return 0;
}

static void ENVY24_setrate (struct mpxplay_audioout_info_s *aui)
{
  struct envy24_card_s *card = aui->card_private_data;
  int err;

  envy24dbg("setrate %u\n", aui->freq_card);
  if (aui->freq_card < 8000) {
    aui->freq_card = 8000;
  } else if (aui->freq_card > 192000) {
    aui->freq_card = 192000;
  }
  envy24dbg("-> %u\n", aui->freq_card);
  aui->card_dmasize = 512;
  aui->card_dma_buffer_size = 4096;
  aui->dma_addr_bits = 32;
  aui->buffer_size_shift = 1;
  err = au_linux_make_snd_pcm_substream(aui, &card->card, envy24_ops);
  if (err) goto err_setrate;
  envy24_ops->prepare(card->card.pcm_substream);
  return;

 err_setrate:
  envy24dbg("setrate error\n");
}

static void ENVY24_start (struct mpxplay_audioout_info_s *aui)
{
  envy24dbg("start\n");
  struct envy24_card_s *card = aui->card_private_data;
  envy24_ops->trigger(card->card.pcm_substream, SNDRV_PCM_TRIGGER_START);
}

static void ENVY24_stop (struct mpxplay_audioout_info_s *aui)
{
  envy24dbg("stop\n");
  struct envy24_card_s *card = aui->card_private_data;
  envy24_ops->trigger(card->card.pcm_substream, SNDRV_PCM_TRIGGER_STOP);
}

unsigned int envy24_int_cnt = 0;

static long ENVY24_getbufpos (struct mpxplay_audioout_info_s *aui)
{
  struct envy24_card_s *card = aui->card_private_data;
  unsigned long bufpos = envy24_ops->pointer(card->card.pcm_substream);
  bufpos <<= 1;
#if ENVY24_DEBUG > 1
  if ((envy24_int_cnt % 450) == 0)
    envy24dbg("getbufpos %u / %u\n", bufpos, aui->card_dmasize);
  //if (bufpos == aui->card_dmasize)
  //  envy24dbg("getbufpos %u == dmasize\n", bufpos);
#endif
  if (bufpos < aui->card_dmasize)
    aui->card_dma_lastgoodpos = bufpos;
  return aui->card_dma_lastgoodpos;
}

static aucards_onemixerchan_s ENVY24_master_vol={AU_MIXCHANFUNCS_PACK(AU_MIXCHAN_MASTER,AU_MIXCHANFUNC_VOLUME),2,{{0,255,8,0},{0,255,0,0}}};
static aucards_allmixerchan_s ENVY24_mixerset[] = {
 &ENVY24_master_vol,
 NULL
};

static void ENVY24_writeMIXER (struct mpxplay_audioout_info_s *aui, unsigned long reg, unsigned long val)
{
  struct envy24_card_s *card = aui->card_private_data;
  envy24dbg("write mixer val: %X\n", val);
}

static unsigned long ENVY24_readMIXER (struct mpxplay_audioout_info_s *aui, unsigned long reg)
{
  struct envy24_card_s *card = aui->card_private_data;
  return 0xffff;
}

static int ENVY24_IRQRoutine (struct mpxplay_audioout_info_s *aui)
{
  struct envy24_card_s *card = aui->card_private_data;
  int handled = snd_vt1724_interrupt(card->card.irq, card->card.linux_snd_card->private_data);
#if ENVY24_DEBUG
  if (handled) {
    if ((envy24_int_cnt % 500) == 0) DBG_Logi("envy24irq %u\n", envy24_int_cnt);
    envy24_int_cnt++;
  }
#endif
  return handled;
}

one_sndcard_info ENVY24_sndcard_info = {
  "Envy24",
  SNDCARD_LOWLEVELHAND|SNDCARD_INT08_ALLOWED,

  NULL,
  NULL,
  &ENVY24_adetect,
  &ENVY24_card_info,
  &ENVY24_start,
  &ENVY24_stop,
  &ENVY24_close,
  &ENVY24_setrate,

  &MDma_writedata,
  &ENVY24_getbufpos,
  &MDma_clearbuf,
  &MDma_interrupt_monitor,
  &ENVY24_IRQRoutine,

  &ENVY24_writeMIXER,
  &ENVY24_readMIXER,
  &ENVY24_mixerset[0],

  NULL,
  NULL,
  NULL,
  NULL,
};

#endif // AUCARDS_LINK_ENVY24
