// EMU10K1X driver for SBEMU
// based on the Linux driver

#include "au_cards.h"

#ifdef AU_CARDS_LINK_EMU10K1X

#define EMU10K1X_DEBUG 0

#if EMU10K1X_DEBUG
#define emu10k1xdbg(...) do { DBG_Logi("EMU10K1X: "); DBG_Logi(__VA_ARGS__); } while (0)
#else
#define emu10k1xdbg(...)
#endif

#include "dmairq.h"
#include "pcibios.h"
#include "dpmi/dbgutil.h"
#include "sound/core.h"
#include "sound/pcm.h"
#include "sound/ac97_codec.h"

struct emu10k1x_card_s {
  struct snd_card *linux_snd_card;
  struct pci_dev *linux_pci_dev;
  struct snd_pcm_substream *pcm_substream;
  struct pci_config_s *pci_dev;
  unsigned int irq;
};

extern unsigned char emu10k1x_mpu401_read (void *card, unsigned int idx);
extern void emu10k1x_mpu401_write (void *card, unsigned int idx, unsigned char data);

extern int snd_emu10k1x_playback_open (struct snd_pcm_substream *substream);
extern int snd_emu10k1x_pcm_hw_params(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params *hw_params);
extern int snd_emu10k1x_pcm_prepare (struct snd_pcm_substream *substream);
extern int snd_emu10k1x_pcm_trigger (struct snd_pcm_substream *substream, int cmd);
extern struct snd_pcm_ops snd_emu10k1x_playback_ops;
extern int snd_emu10k1x_probe (struct snd_card *card, struct pci_dev *pci);

static int
make_snd_pcm_substream (struct mpxplay_audioout_info_s *aui, struct emu10k1x_card_s *card, struct snd_pcm_substream **substreamp)
{
  struct snd_pcm_substream *substream;
  struct snd_pcm_runtime *runtime;
  int err;

  substream = kzalloc(sizeof(*substream), GFP_KERNEL);
  if (!substream) {
    goto err;
  }
  substream->ops = &snd_emu10k1x_playback_ops;
  substream->pcm = kzalloc(sizeof(struct snd_pcm), GFP_KERNEL);
  substream->pcm->card = card->linux_snd_card;
  substream->pcm->device = 0;
  runtime = kzalloc(sizeof(struct snd_pcm_runtime), GFP_KERNEL);
  if (!runtime) {
    goto err;
  }
  substream->runtime = runtime;
  substream->private_data = card->linux_snd_card->private_data;
  runtime->dma_buffer_p = kzalloc(sizeof(struct snd_dma_buffer), GFP_KERNEL);
  if (!runtime->dma_buffer_p) {
    goto err;
  }
#define PCMBUFFERPAGESIZE 512
//#define PCMBUFFERPAGESIZE 4096
  aui->chan_card = 2;
  aui->bits_card = 16;
  aui->card_wave_id = MPXPLAY_WAVEID_PCM_SLE;
  size_t dmabuffsize = 4096;
  //size_t dmabuffsize = aui->card_dmasize;
  dmabuffsize = MDma_get_max_pcmoutbufsize(aui, 0, PCMBUFFERPAGESIZE, 2, 0);
  emu10k1xdbg("max dmabuffsize: %u\n", dmabuffsize);
  err = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, 
                            &card->linux_pci_dev->dev,
                            dmabuffsize,
                            runtime->dma_buffer_p);
  if (err) {
    goto err;
  }
  aui->card_DMABUFF = runtime->dma_buffer_p->area;
  dmabuffsize = MDma_init_pcmoutbuf(aui, dmabuffsize, PCMBUFFERPAGESIZE, 0);
  emu10k1xdbg("dmabuffsize: %u   buff: %8.8X\n", dmabuffsize, aui->card_DMABUFF);
  snd_pcm_set_runtime_buffer(substream, runtime->dma_buffer_p);
  runtime->buffer_size = dmabuffsize;
  runtime->channels = 2;
  runtime->frame_bits = 16;
  runtime->sample_bits = 16;
  runtime->rate = aui->freq_card;
  runtime->format = SNDRV_PCM_FORMAT_S16_LE;
  *substreamp = substream;
  //emu10k1xdbg("dmabuff: %8.8X\n", aui->card_DMABUFF);
  err = snd_emu10k1x_playback_open(substream);
  if (err) {
    goto err;
  }
  err = snd_emu10k1x_pcm_hw_params(substream, NULL);
  if (err) {
    goto err;
  }
  //emu10k1xdbg("runtime: %8.8X\n", runtime);
  int periods = max(1, dmabuffsize / PCMBUFFERPAGESIZE);
  runtime->periods = periods;
  runtime->period_size = (dmabuffsize / periods) >> 1;
  emu10k1xdbg("periods: %u  size: %u\n", runtime->periods, runtime->period_size);
  aui->card_dmasize = aui->card_dma_buffer_size = dmabuffsize;
  aui->card_samples_per_int = runtime->period_size >> 2;

  return 0;

 err:
  if (runtime->dma_buffer_p) snd_dma_free_pages(runtime->dma_buffer_p);
  if (runtime) kfree(runtime);
  if (substream) kfree(substream);
  return -1;
}

//-------------------------------------------------------------------------
static pci_device_s emu10k1x_devices[] = {
  {"EMU10K1X", 0x1102, 0x0006, 0},
  {NULL,0,0,0}
};

extern void snd_emu10k1x_free(struct snd_card *card);

static void EMU10K1X_close (struct mpxplay_audioout_info_s *aui)
{
  struct emu10k1x_card_s *card = aui->card_private_data;
  if (card) {
    if (card->linux_snd_card)
      snd_emu10k1x_free(card->linux_snd_card);
    if (card->pci_dev)
      pds_free(card->pci_dev);
    pds_free(card);
    aui->card_private_data = NULL;
  }
}

static void EMU10K1X_card_info (struct mpxplay_audioout_info_s *aui)
{
  struct emu10k1x_card_s *card = aui->card_private_data;
  char sout[100];
  sprintf(sout, "EMU10K1X : Creative %s (%4.4X) IRQ %u", card->pci_dev->device_name, card->pci_dev->device_id, card->irq);
  pds_textdisplay_printf(sout);
}

static int EMU10K1X_adetect (struct mpxplay_audioout_info_s *aui)
{
  struct emu10k1x_card_s *card;
  uint32_t iobase;
  int err;

  emu10k1xdbg("adetect\n");

  card = (struct emu10k1x_card_s *)pds_zalloc(sizeof(struct emu10k1x_card_s));
  if (!card)
    return 0;
  aui->card_private_data = card;

  card->pci_dev = (struct pci_config_s *)pds_zalloc(sizeof(struct pci_config_s));
  if (!card->pci_dev)
    goto err_adetect;

  if (pcibios_search_devices(emu10k1x_devices, card->pci_dev) != PCI_SUCCESSFUL)
    goto err_adetect;

  iobase = pcibios_ReadConfig_Dword(card->pci_dev, PCIR_NAMBAR);
  iobase &= 0xfffffff8;
  if (!iobase)
    goto err_adetect;

  aui->card_irq = card->irq = pcibios_ReadConfig_Byte(card->pci_dev, PCIR_INTR_LN);
#ifdef SBEMU
  aui->card_pci_dev = card->pci_dev;
#endif

  card->linux_snd_card = pds_zalloc(sizeof(struct snd_card));
  card->linux_pci_dev = pds_zalloc(sizeof(struct pci_dev));
  card->linux_pci_dev->pcibios_dev = card->pci_dev;
  card->linux_pci_dev->irq = card->irq;
  card->linux_pci_dev->vendor = card->pci_dev->vendor_id;
  card->linux_pci_dev->device = card->pci_dev->device_id;
  card->linux_pci_dev->subsystem_vendor = pcibios_ReadConfig_Dword(card->pci_dev, PCIR_SSVID);
  card->linux_pci_dev->subsystem_device = pcibios_ReadConfig_Dword(card->pci_dev, PCIR_SSID);
  card->linux_pci_dev->revision = pcibios_ReadConfig_Byte(card->pci_dev, PCIR_RID);
  emu10k1xdbg("pci subsystem %4.4X:%4.4X\n", card->linux_pci_dev->subsystem_vendor, card->linux_pci_dev->subsystem_device);
  err = snd_emu10k1x_probe(card->linux_snd_card, card->linux_pci_dev);
  if (err) goto err_adetect;
  aui->freq_card = 48000;
  err = make_snd_pcm_substream(aui, card, &card->pcm_substream);
  if (err) goto err_adetect;
  aui->mpu401 = 1;
  emu10k1xdbg("EMU10K1X : Creative %s (%4.4X) IRQ %u\n", card->pci_dev->device_name, card->pci_dev->device_id, card->irq);

  return 1;

err_adetect:
  EMU10K1X_close(aui);
  return 0;
}

static void EMU10K1X_setrate (struct mpxplay_audioout_info_s *aui)
{
  struct emu10k1x_card_s *card = aui->card_private_data;
  emu10k1xdbg("setrate %u\n", aui->freq_card);
  aui->freq_card = 48000; // 48KHz only
  emu10k1xdbg("-> %u\n", aui->freq_card);
  snd_emu10k1x_pcm_prepare(card->pcm_substream);
}

static void EMU10K1X_start (struct mpxplay_audioout_info_s *aui)
{
  emu10k1xdbg("start\n");
  struct emu10k1x_card_s *card = aui->card_private_data;
  snd_emu10k1x_pcm_trigger(card->pcm_substream, SNDRV_PCM_TRIGGER_START);
}

static void EMU10K1X_stop (struct mpxplay_audioout_info_s *aui)
{
  emu10k1xdbg("stop\n");
  struct emu10k1x_card_s *card = aui->card_private_data;
  snd_emu10k1x_pcm_trigger(card->pcm_substream, SNDRV_PCM_TRIGGER_STOP);
}

unsigned int emu10k1x_int_cnt = 0;
extern snd_pcm_uframes_t snd_emu10k1x_pcm_pointer(struct snd_pcm_substream *substream);

static long EMU10K1X_getbufpos (struct mpxplay_audioout_info_s *aui)
{
  struct emu10k1x_card_s *card = aui->card_private_data;
  unsigned long bufpos = snd_emu10k1x_pcm_pointer(card->pcm_substream);
  bufpos <<= 1;
#if EMU10K1X_DEBUG > 1
  if ((emu10k1x_int_cnt % 950) == 0)
    emu10k1xdbg("getbufpos %u / %u\n", bufpos, aui->card_dmasize);
  //if (bufpos == aui->card_dmasize)
  //  emu10k1xdbg("getbufpos %u == dmasize\n", bufpos);
#endif
  if (bufpos < aui->card_dmasize)
    aui->card_dma_lastgoodpos = bufpos;
  return aui->card_dma_lastgoodpos;
}

static aucards_onemixerchan_s EMU10K1X_master_vol={AU_MIXCHANFUNCS_PACK(AU_MIXCHAN_MASTER,AU_MIXCHANFUNC_VOLUME),2,{{0,15,4,0},{0,15,0,0}}};
static aucards_allmixerchan_s EMU10K1X_mixerset[] = {
 &EMU10K1X_master_vol,
 NULL
};

extern u16 emu10k1x_ac97_read (struct snd_card *card, u8 reg);
extern void emu10k1x_ac97_write (struct snd_card *card, u8 reg, u16 val);

static void EMU10K1X_writeMIXER (struct mpxplay_audioout_info_s *aui, unsigned long reg, unsigned long val)
{
  struct emu10k1x_card_s *card = aui->card_private_data;
  emu10k1xdbg("write mixer val: %X\n", val);
  // warning: uses only one channel's volume
  val = ((val & 15) << 1) | 1;
  u16 lval = 31 - (val & 31);
  u16 ac97val = (lval << 8) | lval;
  if (val <= 1) ac97val |= 0x8000;
  emu10k1xdbg("write mixer ac97val: %4.4X\n", ac97val);
  emu10k1x_ac97_write(card->linux_snd_card, AC97_MASTER, ac97val);
}

static unsigned long EMU10K1X_readMIXER (struct mpxplay_audioout_info_s *aui, unsigned long reg)
{
  struct emu10k1x_card_s *card = aui->card_private_data;
  u16 ac97val = emu10k1x_ac97_read(card->linux_snd_card, AC97_MASTER);
  emu10k1xdbg("read ac97val %4.4X\n", ac97val);
  u16 lval = 31 - (ac97val & 31);
  lval >>= 1;
  u16 val = (lval << 4) | lval;
  if (ac97val & 0x8000)
    return 0;
  emu10k1xdbg("read mixer returning %X\n", val);
  return val;
}

extern irqreturn_t snd_emu10k1x_interrupt(int irq, void *dev_id);

static int EMU10K1X_IRQRoutine (struct mpxplay_audioout_info_s *aui)
{
  struct emu10k1x_card_s *card = aui->card_private_data;
  int handled = snd_emu10k1x_interrupt(card->irq, card->linux_snd_card->private_data);
#if EMU10K1X_DEBUG
  if (handled) {
    if ((emu10k1x_int_cnt % 500) == 0) DBG_Logi("emu10k1xirq %u\n", emu10k1x_int_cnt);
    emu10k1x_int_cnt++;
  }
#endif
  return handled;
}

static uint8_t EMU10K1X_mpu401_read (struct mpxplay_audioout_info_s *aui, unsigned int idx)
{
  struct emu10k1x_card_s *card = aui->card_private_data;
  return emu10k1x_mpu401_read(card, idx);
}

static void EMU10K1X_mpu401_write (struct mpxplay_audioout_info_s *aui, unsigned int idx, uint8_t data)
{
  struct emu10k1x_card_s *card = aui->card_private_data;
  emu10k1x_mpu401_write(card, idx, data);
}

one_sndcard_info EMU10K1X_sndcard_info = {
 "EMU10K1X",
 SNDCARD_LOWLEVELHAND|SNDCARD_INT08_ALLOWED,

 NULL,
 NULL,
 &EMU10K1X_adetect,
 &EMU10K1X_card_info,
 &EMU10K1X_start,
 &EMU10K1X_stop,
 &EMU10K1X_close,
 &EMU10K1X_setrate,

 &MDma_writedata,
 &EMU10K1X_getbufpos,
 &MDma_clearbuf,
 &MDma_interrupt_monitor,
 &EMU10K1X_IRQRoutine,

 &EMU10K1X_writeMIXER,
 &EMU10K1X_readMIXER,
 &EMU10K1X_mixerset[0],

 NULL,
 NULL,
 &EMU10K1X_mpu401_write,
 &EMU10K1X_mpu401_read,
};

#endif // AUCARDS_LINK_EMU10K1X
