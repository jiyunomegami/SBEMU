// ALS4000 driver for SBEMU
// based on the Linux driver

#include "au_cards.h"

#ifdef AU_CARDS_LINK_ALS4000

#define ALS4000_DEBUG 0

#if ALS4000_DEBUG
#define als4000dbg(...) do { DBG_Logi("ALS4000: "); DBG_Logi(__VA_ARGS__); } while (0)
#else
#define als4000dbg(...)
#endif

#include "dmairq.h"
#include "pcibios.h"
#include "dpmi/dbgutil.h"
#include "sound/core.h"
#include "sound/pcm.h"
#include "sound/ac97_codec.h"
#include "sound/sb.h"
#include "../../drivers/als4000/als4000.h"

struct als4000_card_s {
  struct snd_card *linux_snd_card;
  struct snd_card_als4000 *als4000;
  struct pci_dev *linux_pci_dev;
  struct snd_pcm_substream *pcm_substream;
  struct pci_config_s *pci_dev;
  unsigned int irq;
};

extern int snd_card_als4000_create (struct snd_card *card,
                                    struct pci_dev *pci,
                                    struct snd_card_als4000 **racard);

extern unsigned char als4000_mpu401_read (void *card, unsigned int idx);
extern void als4000_mpu401_write (void *card, unsigned int idx, unsigned char data);

extern int snd_als4000_synth_open (struct snd_pcm_substream *substream);

extern int snd_als4000_playback_open (struct snd_pcm_substream *substream);
extern int snd_als4000_playback_prepare (struct snd_pcm_substream *substream);
extern int snd_als4000_playback_trigger (struct snd_pcm_substream *substream, int cmd);
extern struct snd_pcm_ops snd_als4000_playback_ops;
extern int snd_als4000_probe (struct snd_card *card, struct pci_dev *pci);

static int
make_snd_pcm_substream (struct mpxplay_audioout_info_s *aui, struct als4000_card_s *card, struct snd_pcm_substream **substreamp)
{
  struct snd_card_als4000 *als4000 = card->als4000;
  struct snd_pcm_substream *substream;
  struct snd_pcm_runtime *runtime;
  struct snd_interval *intervalparam;
  int err;

  substream = kzalloc(sizeof(*substream), GFP_KERNEL);
  if (!substream) {
    goto err;
  }
  substream->ops = &snd_als4000_playback_ops;
  substream->pcm = kzalloc(sizeof(struct snd_pcm), GFP_KERNEL);
  substream->pcm->card = card->linux_snd_card;
  substream->pcm->device = 0;
  runtime = kzalloc(sizeof(struct snd_pcm_runtime), GFP_KERNEL);
  if (!runtime) {
    goto err;
  }
  substream->runtime = runtime;
  substream->private_data = als4000->chip;
  substream->group = &substream->self_group;
  snd_pcm_group_init(&substream->self_group);
  list_add_tail(&substream->link_list, &substream->self_group.substreams);

  runtime->dma_buffer_p = kzalloc(sizeof(struct snd_dma_buffer), GFP_KERNEL);
  if (!runtime->dma_buffer_p) {
    goto err;
  }
  //#define PCMBUFFERPAGESIZE 4096
  //#define PCMBUFFERPAGESIZE 1024
#define PCMBUFFERPAGESIZE 512
  aui->chan_card = 2;
  aui->bits_card = 16;
  aui->card_wave_id = MPXPLAY_WAVEID_PCM_SLE;
  size_t dmabuffsize = 4096;
  size_t allocsize = dmabuffsize; //1024 * 64 * 4; // XXX
  dmabuffsize = MDma_get_max_pcmoutbufsize(aui, 0, PCMBUFFERPAGESIZE, 2, 0);
  als4000dbg("max dmabuffsize: %u\n", dmabuffsize);
  err = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, 
                            &card->linux_pci_dev->dev,
                            allocsize,
                            runtime->dma_buffer_p);
  if (err) {
    goto err;
  }
  als4000dbg("DMA buffer: size %u physical address: %8.8X\n", dmabuffsize, runtime->dma_buffer_p->addr);
  int retry_idx = 0, max_tries = 20;
  struct snd_dma_buffer *dmabuffers[20];
  if (runtime->dma_buffer_p->addr >= 0x1000000) {
    dmabuffers[0] = runtime->dma_buffer_p;
    runtime->dma_buffer_p = NULL;
    for (retry_idx = 1; retry_idx < max_tries; retry_idx++) {
      runtime->dma_buffer_p = kzalloc(sizeof(struct snd_dma_buffer), GFP_KERNEL);
      if (!runtime->dma_buffer_p) {
        goto retry_err;
      }
      err = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, 
                                &card->linux_pci_dev->dev,
                                allocsize,
                                runtime->dma_buffer_p);
      if (err) {
        goto retry_err;
      }
      als4000dbg("retrying DMA buffer physical address: %8.8X\n", runtime->dma_buffer_p->addr);
      if (runtime->dma_buffer_p->addr >= 0x1000000) {
        dmabuffers[retry_idx] = runtime->dma_buffer_p;
        runtime->dma_buffer_p = NULL;
      } else {
        break;
      }
    }
  }
  while (retry_idx--) {
    als4000dbg("freeing dma buffer index %i\n", retry_idx);
    snd_dma_free_pages(dmabuffers[retry_idx]);
  }
  retry_idx = 0;
  if (runtime->dma_buffer_p == NULL) {
  retry_err:
    while (retry_idx--) {
      als4000dbg("freeing dma buffer index %i\n", retry_idx);
      snd_dma_free_pages(dmabuffers[retry_idx]);
    }
    printf("ALS4000: Could not allocate DMA buffer with physical address below 0x1000000, try using HimemX2 or HimemX /MAX=32768\n");
    goto err;
  }
#if 0 // should not be needed...
  uint32_t physaddr = runtime->dma_buffer_p->addr;
  runtime->dma_buffer_p->addr = ALIGN(runtime->dma_buffer_p->addr, 64 * 1024);
  uint32_t diff = runtime->dma_buffer_p->addr - physaddr;
  als4000dbg("diff: %u (%X)\n", diff, diff);
  runtime->dma_buffer_p->area += diff;
#endif
  aui->card_DMABUFF = runtime->dma_buffer_p->area;
  dmabuffsize = MDma_init_pcmoutbuf(aui, dmabuffsize, PCMBUFFERPAGESIZE, 0);
  snd_pcm_set_runtime_buffer(substream, runtime->dma_buffer_p);
  runtime->buffer_size = dmabuffsize >> 1; // ???
  runtime->channels = 2;
  runtime->frame_bits = 16;
  runtime->sample_bits = 16;
  runtime->rate = aui->freq_card;
  runtime->format = SNDRV_PCM_FORMAT_S16_LE;
  *substreamp = substream;
  als4000dbg("runtime: %8.8X  rate: %d\n", runtime, runtime->rate);
  int periods = max(1, dmabuffsize / PCMBUFFERPAGESIZE);
  runtime->periods = periods;
  runtime->period_size = (dmabuffsize / periods) >> 1;
  als4000dbg("periods: %u  size: %u\n", runtime->periods, runtime->period_size);
  aui->card_dmasize = aui->card_dma_buffer_size = dmabuffsize;
  aui->card_samples_per_int = runtime->period_size >> 2;
  err = snd_als4000_playback_open(substream);
  if (err) {
    goto err;
  }
  als4000dbg("DMA buffer: size %u physical address: %8.8X\n", dmabuffsize, runtime->dma_buffer_p->addr);
  
  return 0;

 err:
  if (runtime->dma_buffer_p) snd_dma_free_pages(runtime->dma_buffer_p);
  if (runtime) kfree(runtime);
  if (substream) kfree(substream);
  return -1;
}

//-------------------------------------------------------------------------
static pci_device_s als4000_devices[] = {
  {"ALS4000", 0x4005, 0x4000, 0},
  {NULL,0,0,0}
};

extern void snd_card_als4000_free (struct snd_card *card);

static void ALS4000_close (struct mpxplay_audioout_info_s *aui)
{
  als4000dbg("close");
  struct als4000_card_s *card = aui->card_private_data;
  if (card) {
    if (card->pcm_substream) {
      if (card->pcm_substream->runtime) {
        if (card->pcm_substream->runtime->dma_buffer_p)
          snd_dma_free_pages(card->pcm_substream->runtime->dma_buffer_p);
        kfree(card->pcm_substream->runtime);
      }
      kfree(card->pcm_substream);
    }
    if (card->linux_snd_card)
      snd_card_als4000_free(card->linux_snd_card);
    if (card->pci_dev)
      pds_free(card->pci_dev);
    pds_free(card);
    aui->card_private_data = NULL;
  }
}

static void ALS4000_card_info (struct mpxplay_audioout_info_s *aui)
{
  struct als4000_card_s *card = aui->card_private_data;
  char sout[100];
  sprintf(sout, "ALS4000 IRQ %u", card->irq);
  pds_textdisplay_printf(sout);
}

static int ALS4000_adetect (struct mpxplay_audioout_info_s *aui)
{
  struct als4000_card_s *card;
  uint32_t iobase;
  int err;

  als4000dbg("adetect\n");

  card = (struct als4000_card_s *)pds_zalloc(sizeof(struct als4000_card_s));
  if (!card)
    return 0;
  aui->card_private_data = card;

  card->pci_dev = (struct pci_config_s *)pds_zalloc(sizeof(struct pci_config_s));
  if (!card->pci_dev)
    goto err_adetect;

  if (pcibios_search_devices(als4000_devices, card->pci_dev) != PCI_SUCCESSFUL)
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

  struct snd_card_als4000 *als4000;
  err = snd_card_als4000_create(card->linux_snd_card,
                                card->linux_pci_dev,
                                &als4000);
  if (err < 0)
    goto err_adetect;

  als4000dbg("iobase %X pci subsystem %4.4X:%4.4X\n", als4000->iobase, card->linux_pci_dev->subsystem_vendor, card->linux_pci_dev->subsystem_device);

  card->linux_snd_card->private_data = als4000;
  card->als4000 = als4000;
  if (!aui->card_select_index_fm || aui->card_select_index_fm == aui->card_test_index) {
    aui->fm_port = als4000->iobase + ALS4K_IOB_10_ADLIB_ADDR0;
    aui->fm = 1;
  }
  if (!aui->card_select_index_mpu401 || aui->card_select_index_mpu401 == aui->card_test_index) {
    aui->mpu401_port = als4000->iobase + ALS4K_IOB_30_MIDI_DATA;
    aui->mpu401 = 1;
  }
#if 0 // done in au_cards.c
  // Disable HW FM/MPU if another card was selected
  if (!aui->card_select_index_fm && !(!aui->card_select_index || aui->card_select_index == aui->card_test_index)) {
    aui->fm_port = 0;
    aui->fm = 0;
  }
  if (!aui->card_select_index_mpu401 && !(!aui->card_select_index || aui->card_select_index == aui->card_test_index)) {
    aui->mpu401_port = 0;
    aui->mpu401 = 0;
  }
#endif

  als4000dbg("ALS4000 : %s (%4.4X) IRQ %u\n", card->pci_dev->device_name, card->pci_dev->device_id, card->irq);

  return 1;

err_adetect:
  ALS4000_close(aui);
  return 0;
}

static void ALS4000_setrate (struct mpxplay_audioout_info_s *aui)
{
  struct als4000_card_s *card = aui->card_private_data;
  int err;

  als4000dbg("setrate %u\n", aui->freq_card);
  if (aui->freq_card < 4000) {
    aui->freq_card = 4000;
  } else if (aui->freq_card > 48000) {
    aui->freq_card = 48000;
  }
  err = make_snd_pcm_substream(aui, card, &card->pcm_substream);
  if (err) goto err_setrate;
  snd_als4000_playback_prepare(card->pcm_substream);
  return;

 err_setrate:
  als4000dbg("setrate error\n");
}

static void ALS4000_start (struct mpxplay_audioout_info_s *aui)
{
  struct als4000_card_s *card = aui->card_private_data;
  struct snd_card_als4000 *als4000 = card->als4000;
  als4000dbg("start\n");
  snd_als4000_playback_trigger(card->pcm_substream, SNDRV_PCM_TRIGGER_START);
}

static void ALS4000_stop (struct mpxplay_audioout_info_s *aui)
{
  struct als4000_card_s *card = aui->card_private_data;
  als4000dbg("stop\n");
  snd_als4000_playback_trigger(card->pcm_substream, SNDRV_PCM_TRIGGER_STOP);
}

unsigned int als4000_int_cnt = 0;
extern snd_pcm_uframes_t snd_als4000_playback_pointer(struct snd_pcm_substream *substream);

static long ALS4000_getbufpos (struct mpxplay_audioout_info_s *aui)
{
  struct als4000_card_s *card = aui->card_private_data;
  dma_addr_t physaddr = card->pcm_substream->runtime->dma_buffer_p->addr;
  unsigned long bufpos = snd_als4000_playback_pointer(card->pcm_substream);
  bufpos -= physaddr;
#if ALS4000_DEBUG > 1
  if ((als4000_int_cnt % 90) == 0)
    als4000dbg("getbufpos %u (%X) / %u\n", bufpos, bufpos, aui->card_dmasize);
  //if (bufpos == aui->card_dmasize) als4000dbg("getbufpos %u == dmasize\n", bufpos);
#endif
  if (bufpos < aui->card_dmasize)
    aui->card_dma_lastgoodpos = bufpos;
  else aui->card_dma_lastgoodpos = 0; // XXX
  return aui->card_dma_lastgoodpos;
}

static aucards_onemixerchan_s ALS4000_master_vol={AU_MIXCHANFUNCS_PACK(AU_MIXCHAN_MASTER,AU_MIXCHANFUNC_VOLUME),2,{{0,15,4,0},{0,15,0,0}}};
static aucards_allmixerchan_s ALS4000_mixerset[] = {
  &ALS4000_master_vol,
  NULL
};

// XXX need to defer writing until start is called???
static void ALS4000_writeMIXER (struct mpxplay_audioout_info_s *aui, unsigned long reg, unsigned long val)
{
  struct als4000_card_s *card = aui->card_private_data;
  als4000dbg("write mixer val: %X\n", val);
}

static unsigned long ALS4000_readMIXER (struct mpxplay_audioout_info_s *aui, unsigned long reg)
{
  struct als4000_card_s *card = aui->card_private_data;
  return 0;
}

extern irqreturn_t snd_als4000_interrupt(int irq, void *dev_id);

static int ALS4000_IRQRoutine (struct mpxplay_audioout_info_s *aui)
{
  struct als4000_card_s *card = aui->card_private_data;
  struct snd_card_als4000 *als4000 = card->als4000;
  int handled = snd_als4000_interrupt(card->irq, als4000->chip);
#if ALS4000_DEBUG
  if (handled) {
    if ((als4000_int_cnt % 500) == 0) DBG_Logi("als4000irq %u\n", als4000_int_cnt);
    als4000_int_cnt++;
  }
#endif
  return handled;
}

one_sndcard_info ALS4000_sndcard_info = {
  "ALS4000",
  SNDCARD_LOWLEVELHAND|SNDCARD_INT08_ALLOWED,

  NULL,
  NULL,
  &ALS4000_adetect,
  &ALS4000_card_info,
#if 1
  &ALS4000_start,
  &ALS4000_stop,
  &ALS4000_close,
  &ALS4000_setrate,

  &MDma_writedata,
  &ALS4000_getbufpos,
  &MDma_clearbuf,
  &MDma_interrupt_monitor,
  &ALS4000_IRQRoutine,
#else
  NULL,NULL,NULL,NULL,
  NULL,NULL,NULL,NULL,NULL,
#endif

  //&ALS4000_writeMIXER,
  //&ALS4000_readMIXER,
  //&ALS4000_mixerset[0],
  NULL,NULL,NULL,

  &ioport_fm_write,
  &ioport_fm_read,
  &ioport_mpu401_write,
  &ioport_mpu401_read,
};

#endif // AUCARDS_LINK_ALS4000
