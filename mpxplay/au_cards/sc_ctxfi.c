// CTXFI driver for SBEMU
// based on the Linux driver

#include "au_cards.h"

#ifdef AU_CARDS_LINK_CTXFI

#define CTXFI_DEBUG 0

#if CTXFI_DEBUG
#define ctxfidbg(...) do { DBG_Logi("CTXFI: "); DBG_Logi(__VA_ARGS__); } while (0)
#else
#define ctxfidbg(...)
#endif

#include "dmairq.h"
#include "pcibios.h"
#include "dpmi/dbgutil.h"
#include "sound/core.h"
#include "sound/pcm.h"
#include "../../drivers/ctxfi/ctatc.h"

struct ctxfi_card_s {
  struct snd_card *linux_snd_card;
  struct pci_dev *linux_pci_dev;
  struct ct_atc *atc;
  struct snd_pcm_substream *pcm_substream;
  struct pci_config_s *pci_dev;
  unsigned int irq;
};

extern int ct_pcm_playback_open (struct snd_pcm_substream *substream);
extern int ct_pcm_playback_prepare (struct snd_pcm_substream *substream);
extern int ct_pcm_playback_trigger (struct snd_pcm_substream *substream, int cmd);
extern struct snd_pcm_ops ct_pcm_playback_ops;

static int
make_snd_pcm_substream (struct mpxplay_audioout_info_s *aui, struct ctxfi_card_s *card, struct snd_pcm_substream **substreamp)
{
  struct snd_pcm_substream *substream = NULL;
  struct snd_pcm_runtime *runtime = NULL;
  int err;

  substream = kzalloc(sizeof(*substream), GFP_KERNEL);
  if (!substream) {
    goto err;
  }
  substream->ops = &ct_pcm_playback_ops;
  substream->pcm = kzalloc(sizeof(struct snd_pcm), GFP_KERNEL);
  substream->pcm->device = FRONT;
  runtime = kzalloc(sizeof(struct snd_pcm_runtime), GFP_KERNEL);
  if (!runtime) {
    goto err;
  }
  substream->runtime = runtime;
  substream->private_data = card->atc;
  runtime->dma_buffer_p = kzalloc(sizeof(struct snd_dma_buffer), GFP_KERNEL);
  if (!runtime->dma_buffer_p) {
    goto err;
  }
//#define PCMBUFFERPAGESIZE 512
// size must be page aligned
#define PCMBUFFERPAGESIZE 4096
  aui->chan_card = 2;
  aui->bits_card = 16;
  aui->card_wave_id = MPXPLAY_WAVEID_PCM_SLE;
  size_t dmabuffsize = 4096;
  //size_t dmabuffsize = aui->card_dmasize;
  dmabuffsize = MDma_get_max_pcmoutbufsize(aui, 0, PCMBUFFERPAGESIZE, 2, 0);
  ctxfidbg("max dmabuffsize: %u\n", dmabuffsize);
  err = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, 
                            &card->linux_pci_dev->dev,
                            dmabuffsize,
                            runtime->dma_buffer_p);
  if (err) {
    goto err;
  }
  aui->card_DMABUFF = runtime->dma_buffer_p->area;
  dmabuffsize = MDma_init_pcmoutbuf(aui, dmabuffsize, PCMBUFFERPAGESIZE, 0);
  ctxfidbg("dmabuffsize: %u   buff: %8.8X\n", dmabuffsize, aui->card_DMABUFF);
  snd_pcm_set_runtime_buffer(substream, runtime->dma_buffer_p);
  runtime->buffer_size = dmabuffsize;
  runtime->channels = 2;
  runtime->frame_bits = 16;
  runtime->sample_bits = 16;
  runtime->rate = aui->freq_card;
  runtime->format = SNDRV_PCM_FORMAT_S16_LE;
  *substreamp = substream;
  //ctxfidbg("dmabuff: %8.8X\n", aui->card_DMABUFF);
  err = ct_pcm_playback_open(substream);
  if (err) {
    return -ENOMEM;
  }
  //ctxfidbg("runtime: %8.8X\n", runtime);
  int periods = max(1, dmabuffsize / PCMBUFFERPAGESIZE);
  runtime->periods = periods;
  runtime->period_size = (dmabuffsize / periods) >> 3;
  ctxfidbg("period: %u\n", runtime->period_size);
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
static pci_device_s ctxfi_devices[] = {
  {"EMU20K1", 0x1102, 0x0005, 1},
  {"EMU20K2", 0x1102, 0x000b, 2},
  {NULL,0,0,0}
};

extern int ct_atc_destroy(struct ct_atc *atc);

static void CTXFI_close (struct mpxplay_audioout_info_s *aui)
{
  struct ctxfi_card_s *card = aui->card_private_data;
  if (card) {
    if (card->atc)
      ct_atc_destroy(card->atc);
    if (card->pci_dev)
      pds_free(card->pci_dev);
    pds_free(card);
    aui->card_private_data = NULL;
  }
}

static void CTXFI_card_info (struct mpxplay_audioout_info_s *aui)
{
  struct ctxfi_card_s *card = aui->card_private_data;
  char sout[100];
  sprintf(sout, "CTXFI : Creative %s (%4.4X) IRQ %u", card->pci_dev->device_name, card->pci_dev->device_id, card->irq);
  pds_textdisplay_printf(sout);
}

static unsigned int reference_rate = 48000;
static unsigned int multiple = 2;

static int CTXFI_adetect (struct mpxplay_audioout_info_s *aui)
{
  struct ctxfi_card_s *card;
  struct ct_atc *atc;
  uint32_t iobase;
  int err;

  ctxfidbg("adetect\n");

  card = (struct ctxfi_card_s *)pds_zalloc(sizeof(struct ctxfi_card_s));
  if (!card)
    return 0;
  aui->card_private_data = card;

  card->pci_dev = (struct pci_config_s *)pds_zalloc(sizeof(struct pci_config_s));
  if (!card->pci_dev)
    goto err_adetect;

  if (pcibios_search_devices(ctxfi_devices, card->pci_dev) != PCI_SUCCESSFUL)
    goto err_adetect;

  //pcibios_set_master(card->pci_dev);

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
  ctxfidbg("atc %4.4X:%4.4X\n", card->linux_pci_dev->subsystem_vendor, card->linux_pci_dev->subsystem_device);
  err = ct_atc_create(card->linux_snd_card,
                      card->linux_pci_dev,
                      reference_rate,
                      multiple,
                      card->pci_dev->device_type,
                      0,
                      &atc);
  if (err) goto err_adetect;
  card->atc = atc;
  aui->freq_card = 22050;
  err = make_snd_pcm_substream(aui, card, &card->pcm_substream);
  if (err) goto err_adetect;
  ctxfidbg("CTXFI : Creative %s (%4.4X) IRQ %u\n", card->pci_dev->device_name, card->pci_dev->device_id, card->irq);

  return 1;

err_adetect:
  CTXFI_close(aui);
  return 0;
}

static void CTXFI_setrate (struct mpxplay_audioout_info_s *aui)
{
  struct ctxfi_card_s *card = aui->card_private_data;
  ctxfidbg("setrate\n");
  //aui->freq_card = 22050;
  if (aui->freq_card < 8000) {
    aui->freq_card = 8000;
  } else if (aui->freq_card > 192000) {
    aui->freq_card = 192000;
  }
  aui->freq_card = 22050; // Force rate to 22050 for now
  ct_pcm_playback_prepare(card->pcm_substream);
}

static void CTXFI_start (struct mpxplay_audioout_info_s *aui)
{
  ctxfidbg("start\n");
  struct ctxfi_card_s *card = aui->card_private_data;
  ct_pcm_playback_trigger(card->pcm_substream, SNDRV_PCM_TRIGGER_START);
}

static void CTXFI_stop (struct mpxplay_audioout_info_s *aui)
{
  ctxfidbg("stop\n");
  struct ctxfi_card_s *card = aui->card_private_data;
  ct_pcm_playback_trigger(card->pcm_substream, SNDRV_PCM_TRIGGER_STOP);
}

unsigned int ctxfi_int_cnt = 0;
extern snd_pcm_uframes_t ct_pcm_playback_pointer(struct snd_pcm_substream *substream);

static long CTXFI_getbufpos (struct mpxplay_audioout_info_s *aui)
{
  struct ctxfi_card_s *card = aui->card_private_data;
  unsigned long bufpos = ct_pcm_playback_pointer(card->pcm_substream);
  bufpos <<= 1;
#if CTXFI_DEBUG > 1
  if ((ctxfi_int_cnt % 9) == 0)
    ctxfidbg("getbufpos %u / %u\n", bufpos, aui->card_dmasize);
  if (bufpos == aui->card_dmasize)
    ctxfidbg("getbufpos %u == dmasize\n", bufpos);
#endif
  if (bufpos < aui->card_dmasize)
    aui->card_dma_lastgoodpos = bufpos;
  return aui->card_dma_lastgoodpos;
}

static aucards_onemixerchan_s CTXFI_master_vol={AU_MIXCHANFUNCS_PACK(AU_MIXCHAN_MASTER,AU_MIXCHANFUNC_VOLUME),2,{{0,15,4,0},{0,15,0,0}}};
static aucards_allmixerchan_s CTXFI_mixerset[] = {
 &CTXFI_master_vol,
 NULL
};

extern int ctxfi_alsa_mix_volume_get (struct ct_atc *atc, int type);
extern int ctxfi_alsa_mix_volume_put (struct ct_atc *atc, int type, int val);

static void CTXFI_writeMIXER (struct mpxplay_audioout_info_s *aui, unsigned long reg, unsigned long val)
{
  struct ctxfi_card_s *card = aui->card_private_data;
  // warning: uses only one channel's volume
  unsigned int lval = (val & 15) << 4;
  ctxfi_alsa_mix_volume_put(card->atc, reg, val);
}

static unsigned long CTXFI_readMIXER (struct mpxplay_audioout_info_s *aui, unsigned long reg)
{
  struct ctxfi_card_s *card = aui->card_private_data;
  unsigned int lval = ctxfi_alsa_mix_volume_get(card->atc, reg);
  unsigned long val = (lval >> 4) & 15;
  val |= (val << 4);
  return val;
}

extern irqreturn_t ct_20k1_interrupt(int irq, void *dev_id);
extern irqreturn_t ct_20k2_interrupt(int irq, void *dev_id);

static int CTXFI_IRQRoutine (struct mpxplay_audioout_info_s *aui)
{
  struct ctxfi_card_s *card = aui->card_private_data;
  irqreturn_t handled;
  if (card->atc->chip_type == ATC20K1)
    handled = ct_20k1_interrupt(card->irq, card->atc->hw);
  else
    handled = ct_20k2_interrupt(card->irq, card->atc->hw);
  if (handled)
    ctxfi_int_cnt++;
  return handled;
}

one_sndcard_info CTXFI_sndcard_info = {
 "CTXFI",
 SNDCARD_LOWLEVELHAND|SNDCARD_INT08_ALLOWED,

 NULL,
 NULL,
 &CTXFI_adetect,
 &CTXFI_card_info,
 &CTXFI_start,
 &CTXFI_stop,
 &CTXFI_close,
 &CTXFI_setrate,

 &MDma_writedata,
 &CTXFI_getbufpos,
 &MDma_clearbuf,
 &MDma_interrupt_monitor,
 &CTXFI_IRQRoutine,

 &CTXFI_writeMIXER,
 &CTXFI_readMIXER,
 &CTXFI_mixerset[0]
};

#endif // AUCARDS_LINK_CTXFI
