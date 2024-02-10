// Trident 4D Wave driver for SBEMU
// based on the Linux driver

#include "au_cards.h"

#ifdef AU_CARDS_LINK_TRIDENT

#define TRIDENT_DEBUG 0

#if TRIDENT_DEBUG
#define tridentdbg(...) do { DBG_Logi("TRIDENT: "); DBG_Logi(__VA_ARGS__); } while (0)
#else
#define tridentdbg(...)
#endif

#include "dmairq.h"
#include "pcibios.h"
#include "dpmi/dbgutil.h"
#include "sound/core.h"
#include "sound/pcm.h"
#include "sound/ac97_codec.h"
#include "../../drivers/trident/trident.h"

struct trident_card_s {
  struct snd_card *linux_snd_card;
  struct snd_trident *trident;
  struct pci_dev *linux_pci_dev;
  struct snd_pcm_substream *pcm_substream;
  struct pci_config_s *pci_dev;
  unsigned int irq;
};

extern unsigned char trident_mpu401_read (void *card, unsigned int idx);
extern void trident_mpu401_write (void *card, unsigned int idx, unsigned char data);

extern int snd_trident_synth_open (struct snd_pcm_substream *substream);

extern int snd_trident_playback_open (struct snd_pcm_substream *substream);
extern int snd_trident_hw_params(struct snd_pcm_substream *substream,
                                 struct snd_pcm_hw_params *hw_params);
extern int snd_trident_playback_prepare (struct snd_pcm_substream *substream);
extern int snd_trident_trigger (struct snd_pcm_substream *substream, int cmd);
extern struct snd_pcm_ops snd_trident_playback_ops;
extern int snd_trident_probe (struct snd_card *card, struct pci_dev *pci);

static int
make_snd_pcm_substream (struct mpxplay_audioout_info_s *aui, struct trident_card_s *card, struct snd_pcm_substream **substreamp)
{
  struct snd_pcm_substream *substream;
  struct snd_pcm_runtime *runtime;
  struct snd_pcm_hw_params hwparams;
  struct snd_interval *intervalparam;
  int err;

  substream = kzalloc(sizeof(*substream), GFP_KERNEL);
  if (!substream) {
    goto err;
  }
  substream->ops = &snd_trident_playback_ops;
  substream->pcm = kzalloc(sizeof(struct snd_pcm), GFP_KERNEL);
  substream->pcm->card = card->linux_snd_card;
  substream->pcm->device = 0;
  runtime = kzalloc(sizeof(struct snd_pcm_runtime), GFP_KERNEL);
  if (!runtime) {
    goto err;
  }
  substream->runtime = runtime;
  substream->private_data = card->linux_snd_card->private_data;
  substream->group = &substream->self_group;
  snd_pcm_group_init(&substream->self_group);
  list_add_tail(&substream->link_list, &substream->self_group.substreams);

  runtime->dma_buffer_p = kzalloc(sizeof(struct snd_dma_buffer), GFP_KERNEL);
  if (!runtime->dma_buffer_p) {
    goto err;
  }
  aui->freq_card = 22050;
#define PCMBUFFERPAGESIZE 512
  aui->chan_card = 2;
  aui->bits_card = 16;
  aui->card_wave_id = MPXPLAY_WAVEID_PCM_SLE;
  size_t dmabuffsize = 4096;
  dmabuffsize = MDma_get_max_pcmoutbufsize(aui, 0, PCMBUFFERPAGESIZE, 2, 0);
  tridentdbg("max dmabuffsize: %u\n", dmabuffsize);
  err = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, 
                            &card->linux_pci_dev->dev,
                            dmabuffsize,
                            runtime->dma_buffer_p);
  if (err) {
    goto err;
  }
  tridentdbg("DMA buffer: size %u physical address: %8.8X\n", dmabuffsize, runtime->dma_buffer_p->addr);
  int retry_idx = 0, max_tries = 20;
  struct snd_dma_buffer *dmabuffers[100];
  if (runtime->dma_buffer_p->addr >= 0x40000000) {
    dmabuffers[0] = runtime->dma_buffer_p;
    runtime->dma_buffer_p = NULL;
    for (retry_idx = 1; retry_idx < max_tries; retry_idx++) {
      runtime->dma_buffer_p = kzalloc(sizeof(struct snd_dma_buffer), GFP_KERNEL);
      if (!runtime->dma_buffer_p) {
        goto retry_err;
      }
      err = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, 
                                &card->linux_pci_dev->dev,
                                dmabuffsize,
                                runtime->dma_buffer_p);
      if (err) {
        goto retry_err;
      }
      tridentdbg("retrying DMA buffer physical address: %8.8X\n", runtime->dma_buffer_p->addr);
      if (runtime->dma_buffer_p->addr >= 0x40000000) {
        dmabuffers[retry_idx] = runtime->dma_buffer_p;
        runtime->dma_buffer_p = NULL;
      } else {
        break;
      }
    }
  }
  while (retry_idx--) {
    tridentdbg("freeing dma buffer index %i\n", retry_idx);
    snd_dma_free_pages(dmabuffers[retry_idx]);
  }
  retry_idx = 0;
  if (runtime->dma_buffer_p == NULL) {
  retry_err:
    while (retry_idx--) {
      tridentdbg("freeing dma buffer index %i\n", retry_idx);
      snd_dma_free_pages(dmabuffers[retry_idx]);
    }
    printf("Trident: Could not allocate DMA buffer with physical address below 0x40000000, try using HimemX2 or HimemX /MAX=32768\n");
    goto err;
  }
  aui->card_DMABUFF = runtime->dma_buffer_p->area;
  // works without this:
  //dmabuffsize = MDma_init_pcmoutbuf(aui, dmabuffsize, PCMBUFFERPAGESIZE, 0);
  dmabuffsize = MDma_init_pcmoutbuf(aui, dmabuffsize, PCMBUFFERPAGESIZE, 0);
  snd_pcm_set_runtime_buffer(substream, runtime->dma_buffer_p);
  runtime->buffer_size = dmabuffsize / 4; // size in samples
  runtime->channels = 2;
  runtime->frame_bits = 16;
  runtime->sample_bits = 16;
  runtime->rate = aui->freq_card;
  runtime->format = SNDRV_PCM_FORMAT_S16_LE;
  *substreamp = substream;
  tridentdbg("runtime: %8.8X\n", runtime);
  int periods = max(1, dmabuffsize / PCMBUFFERPAGESIZE);
  runtime->periods = periods;
  runtime->period_size = (dmabuffsize / periods) >> 1;
  tridentdbg("periods: %u  size: %u\n", runtime->periods, runtime->period_size);
  aui->card_dmasize = aui->card_dma_buffer_size = dmabuffsize;
  aui->card_samples_per_int = runtime->period_size >> 2;
  intervalparam = hw_param_interval(&hwparams, SNDRV_PCM_HW_PARAM_BUFFER_BYTES);
  intervalparam->min = dmabuffsize;
  intervalparam = hw_param_interval(&hwparams, SNDRV_PCM_HW_PARAM_PERIOD_SIZE);
  intervalparam->min = runtime->period_size;
  err = snd_trident_playback_open(substream);
  if (err) {
    goto err;
  }
  err = snd_trident_hw_params(substream, &hwparams);
  if (err) {
    goto err;
  }
  tridentdbg("DMA buffer: size %u physical address: %8.8X\n", dmabuffsize, runtime->dma_buffer_p->addr);
  
  return 0;

 err:
  if (runtime->dma_buffer_p) snd_dma_free_pages(runtime->dma_buffer_p);
  if (runtime) kfree(runtime);
  if (substream) kfree(substream);
  return -1;
}

//-------------------------------------------------------------------------
static pci_device_s trident_devices[] = {
  {"Trident 4DWave DX", PCI_VENDOR_ID_TRIDENT, PCI_DEVICE_ID_TRIDENT_4DWAVE_DX, 1},
  {"Trident 4dWave NX", PCI_VENDOR_ID_TRIDENT, PCI_DEVICE_ID_TRIDENT_4DWAVE_NX, 1},
  {"SI 7018", PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_7018, 2},
  {NULL,0,0,0}
};

extern void snd_trident_free(struct snd_trident *trident);

static void TRIDENT_close (struct mpxplay_audioout_info_s *aui)
{
  struct trident_card_s *card = aui->card_private_data;
  if (card) {
    if (card->trident)
      snd_trident_free(card->trident);
    if (card->pci_dev)
      pds_free(card->pci_dev);
    pds_free(card);
    aui->card_private_data = NULL;
  }
}

static void TRIDENT_card_info (struct mpxplay_audioout_info_s *aui)
{
  struct trident_card_s *card = aui->card_private_data;
  char sout[100];
  sprintf(sout, "TRIDENT : %s (%4.4X) IRQ %u", card->pci_dev->device_name, card->pci_dev->device_id, card->irq);
  pds_textdisplay_printf(sout);
}

static int TRIDENT_adetect (struct mpxplay_audioout_info_s *aui)
{
  struct trident_card_s *card;
  uint32_t iobase;
  int err;

  tridentdbg("adetect\n");

  card = (struct trident_card_s *)pds_zalloc(sizeof(struct trident_card_s));
  if (!card)
    return 0;
  aui->card_private_data = card;

  card->pci_dev = (struct pci_config_s *)pds_zalloc(sizeof(struct pci_config_s));
  if (!card->pci_dev)
    goto err_adetect;

  if (pcibios_search_devices(trident_devices, card->pci_dev) != PCI_SUCCESSFUL)
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
  tridentdbg("pci subsystem %4.4X:%4.4X\n", card->linux_pci_dev->subsystem_vendor, card->linux_pci_dev->subsystem_device);

  struct snd_trident *trident;
  int pcm_channels = 32;
  int wavetable_size = 8192;
  err = snd_trident_create(card->linux_snd_card,
                           card->linux_pci_dev,
                           pcm_channels,
                           card->pci_dev->device_type == 2 ? 1 : 2,
                           wavetable_size,
                           &trident);
  if (err < 0)
    goto err_adetect;

  aui->mpu401_port = trident->midi_port;
  aui->mpu401 = 1;
  card->linux_snd_card->private_data = trident;
  card->trident = trident;

  aui->freq_card = 22050;
  err = make_snd_pcm_substream(aui, card, &card->pcm_substream);
  if (err) goto err_adetect;
  tridentdbg("TRIDENT : %s (%4.4X) IRQ %u\n", card->pci_dev->device_name, card->pci_dev->device_id, card->irq);

  return 1;

err_adetect:
  TRIDENT_close(aui);
  return 0;
}

static void TRIDENT_setrate (struct mpxplay_audioout_info_s *aui)
{
  struct trident_card_s *card = aui->card_private_data;
  tridentdbg("setrate %u\n", aui->freq_card);
  if (aui->freq_card < 4000) {
    aui->freq_card = 4000;
  } else if (aui->freq_card > 48000) {
    aui->freq_card = 48000;
  }
  aui->freq_card = 22050; // Force rate to 22050 for now
  snd_trident_playback_prepare(card->pcm_substream);
}

static void TRIDENT_start (struct mpxplay_audioout_info_s *aui)
{
  tridentdbg("start\n");
  struct trident_card_s *card = aui->card_private_data;
  snd_trident_trigger(card->pcm_substream, SNDRV_PCM_TRIGGER_START);
}

static void TRIDENT_stop (struct mpxplay_audioout_info_s *aui)
{
  tridentdbg("stop\n");
  struct trident_card_s *card = aui->card_private_data;
  snd_trident_trigger(card->pcm_substream, SNDRV_PCM_TRIGGER_STOP);
}

unsigned int trident_int_cnt = 0;
extern snd_pcm_uframes_t snd_trident_playback_pointer(struct snd_pcm_substream *substream);

static long TRIDENT_getbufpos (struct mpxplay_audioout_info_s *aui)
{
  struct trident_card_s *card = aui->card_private_data;
  unsigned long bufpos = snd_trident_playback_pointer(card->pcm_substream);
  bufpos <<= 2;
#if TRIDENT_DEBUG > 1
  if ((trident_int_cnt % 90) == 0)
    tridentdbg("getbufpos %u / %u\n", bufpos, aui->card_dmasize);
  if (bufpos == aui->card_dmasize)
    tridentdbg("getbufpos %u == dmasize\n", bufpos);
#endif
  if (bufpos < aui->card_dmasize)
    aui->card_dma_lastgoodpos = bufpos;
  return aui->card_dma_lastgoodpos;
}

static aucards_onemixerchan_s TRIDENT_master_vol={AU_MIXCHANFUNCS_PACK(AU_MIXCHAN_MASTER,AU_MIXCHANFUNC_VOLUME),2,{{0,15,4,0},{0,15,0,0}}};
static aucards_allmixerchan_s TRIDENT_mixerset[] = {
 &TRIDENT_master_vol,
 NULL
};

extern u16 snd_trident_ac97_read (struct snd_card *card, u16 reg);
extern void snd_trident_ac97_write (struct snd_card *card, u16 reg, u16 val);

static void TRIDENT_writeMIXER (struct mpxplay_audioout_info_s *aui, unsigned long reg, unsigned long val)
{
  struct trident_card_s *card = aui->card_private_data;
  tridentdbg("write mixer val: %X\n", val);
  // warning: uses only one channel's volume
  val = ((val & 15) << 1) | 1;
  u16 lval = 31 - (val & 31);
  u16 ac97val = (lval << 8) | lval;
  if (val <= 1) ac97val |= 0x8000;
  tridentdbg("write mixer ac97val: %4.4X\n", ac97val);
  snd_trident_ac97_write(card->linux_snd_card, AC97_MASTER, ac97val);
}

static unsigned long TRIDENT_readMIXER (struct mpxplay_audioout_info_s *aui, unsigned long reg)
{
  struct trident_card_s *card = aui->card_private_data;
  u16 ac97val = snd_trident_ac97_read(card->linux_snd_card, AC97_MASTER);
  tridentdbg("read ac97val %4.4X\n", ac97val);
  u16 lval = 31 - (ac97val & 31);
  lval >>= 1;
  u16 val = (lval << 4) | lval;
  if (ac97val & 0x8000)
    return 0;
  tridentdbg("read mixer returning %X\n", val);
  return val;
}

extern irqreturn_t snd_trident_interrupt(int irq, void *dev_id);

static int TRIDENT_IRQRoutine (struct mpxplay_audioout_info_s *aui)
{
  struct trident_card_s *card = aui->card_private_data;
  int handled = snd_trident_interrupt(card->irq, card->linux_snd_card->private_data);
#if TRIDENT_DEBUG
  if (handled) {
    if ((trident_int_cnt % 500) == 0) DBG_Logi("tridentirq %u\n", trident_int_cnt);
    trident_int_cnt++;
  }
#endif
  return handled;
}

one_sndcard_info TRIDENT_sndcard_info = {
 "TRIDENT",
 SNDCARD_LOWLEVELHAND|SNDCARD_INT08_ALLOWED,

 NULL,
 NULL,
 &TRIDENT_adetect,
 &TRIDENT_card_info,
 &TRIDENT_start,
 &TRIDENT_stop,
 &TRIDENT_close,
 &TRIDENT_setrate,

 &MDma_writedata,
 &TRIDENT_getbufpos,
 &MDma_clearbuf,
 &MDma_interrupt_monitor,
 &TRIDENT_IRQRoutine,

 &TRIDENT_writeMIXER,
 &TRIDENT_readMIXER,
 &TRIDENT_mixerset[0],

 NULL,
 NULL,
 &ioport_mpu401_write,
 &ioport_mpu401_read,
};

#endif // AUCARDS_LINK_TRIDENT
