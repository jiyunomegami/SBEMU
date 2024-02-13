// OXYGEN(CMI8788) driver for SBEMU
// based on the Linux driver

#include "au_cards.h"

#ifdef AU_CARDS_LINK_OXYGEN

#define OXYGEN_DEBUG 0

#if OXYGEN_DEBUG
#define oxygendbg(...) do { DBG_Logi("OXYGEN: "); DBG_Logi(__VA_ARGS__); } while (0)
#else
#define oxygendbg(...)
#endif

#include "dmairq.h"
#include "pcibios.h"
#include "dpmi/dbgutil.h"
#include "sound/core.h"
#include "sound/pcm.h"
#include "sound/ac97_codec.h"

// Only tested with Xonar DG S/PDIF
#define OXYGEN_TYPE_XONAR_DG 0
static pci_device_s oxygen_devices[] = {
  {"Asus Xonar DG", 0x13F6, 0x8788, OXYGEN_TYPE_XONAR_DG},
  {"OXYGEN C-Media reference design", 0x10b0, 0x0216, 1},
  {"OXYGEN C-Media reference design", 0x10b0, 0x0217, 1},
  {"OXYGEN C-Media reference design", 0x10b0, 0x0218, 1},
  {"OXYGEN C-Media reference design", 0x10b0, 0x0219, 1},
  {"OXYGEN C-Media reference design", 0x13f6, 0x0001, 1},
  {"OXYGEN C-Media reference design", 0x13f6, 0x0010, 1},
  {"OXYGEN C-Media reference design", 0x13f6, 0x8788, 1},
  {"OXYGEN C-Media reference design", 0x147a, 0xa017, 1},
  {"OXYGEN C-Media reference design", 0x1a58, 0x0910, 1},
  {"Asus Xonar DGX", 0x1043, 0x8521, 2},
  {"PCI 2.0 HD Audio", 0x13f6, 0x8782, 3},
  {"Kuroutoshikou CMI8787-HG2PCI", 0x13f6, 0xffff, 4},
  {"TempoTec HiFier Fantasia", 0x14c3, 0x1710, 5},
  {"TempoTec HiFier Serenade", 0x14c3, 0x1711, 6},
  {"AuzenTech X-Meridian", 0x415a, 0x5431, 7},
  {"AuzenTech X-Meridian 2G", 0x5431, 0x017a, 8},
  {"HT-Omega Claro", 0x7284, 0x9761, 9},
  {"HT-Omega Claro halo", 0x7284, 0x9781, 10},
  {NULL,0,0,0}
};

struct oxygen_card_s {
  struct snd_card *linux_snd_card;
  struct pci_dev *linux_pci_dev;
  struct snd_pcm_substream *pcm_substream;
  struct pci_config_s *pci_dev;
  unsigned int irq;
};

extern struct snd_pcm_ops oxygen_spdif_ops;
extern struct snd_pcm_ops oxygen_multich_ops;
extern struct snd_pcm_ops oxygen_ac97_ops;
//static struct snd_pcm_ops *oxygen_ops = &oxygen_multich_ops;
//static struct snd_pcm_ops *oxygen_ops = &oxygen_ac97_ops;
static struct snd_pcm_ops *oxygen_ops = &oxygen_spdif_ops;
extern int snd_oxygen_probe (struct snd_card *card, struct pci_dev *pci, int probe_only);

static int
make_snd_pcm_substream (struct mpxplay_audioout_info_s *aui, struct oxygen_card_s *card, struct snd_pcm_substream **substreamp)
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
  substream->ops = oxygen_ops;
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
#define PCMBUFFERPAGESIZE 512
//#define PCMBUFFERPAGESIZE 4096
  aui->chan_card = 2;
  aui->bits_card = 16;
  aui->card_wave_id = MPXPLAY_WAVEID_PCM_SLE;
  size_t dmabuffsize = 4096;
  //size_t dmabuffsize = aui->card_dmasize;
  dmabuffsize = MDma_get_max_pcmoutbufsize(aui, 0, PCMBUFFERPAGESIZE, 2, 0);
  oxygendbg("max dmabuffsize: %u\n", dmabuffsize);
  err = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, 
                            &card->linux_pci_dev->dev,
                            dmabuffsize,
                            runtime->dma_buffer_p);
  if (err) {
    goto err;
  }
  aui->card_DMABUFF = runtime->dma_buffer_p->area;
  dmabuffsize = MDma_init_pcmoutbuf(aui, dmabuffsize, PCMBUFFERPAGESIZE, 0);
  oxygendbg("dmabuffsize: %u   buff: %8.8X\n", dmabuffsize, aui->card_DMABUFF);
  snd_pcm_set_runtime_buffer(substream, runtime->dma_buffer_p);
  runtime->buffer_size = dmabuffsize;
  runtime->channels = 2;
  runtime->frame_bits = 16;
  runtime->sample_bits = 16;
  runtime->rate = aui->freq_card;
  runtime->format = SNDRV_PCM_FORMAT_S16_LE;
  *substreamp = substream;
  oxygendbg("open substream\n");
  err = oxygen_ops->open(substream);
  if (err) {
    goto err;
  }
  intervalparam = hw_param_interval(&hwparams, SNDRV_PCM_HW_PARAM_BUFFER_BYTES);
  intervalparam->min = dmabuffsize;
  intervalparam = hw_param_interval(&hwparams, SNDRV_PCM_HW_PARAM_PERIOD_SIZE);
  intervalparam->min = runtime->period_size;
  intervalparam = hw_param_interval(&hwparams, SNDRV_PCM_HW_PARAM_RATE);
  intervalparam->min = aui->freq_card;
  intervalparam = hw_param_interval(&hwparams, SNDRV_PCM_HW_PARAM_CHANNELS);
  intervalparam->min = 2;
  oxygendbg("hw params\n");
  err = oxygen_ops->hw_params(substream, &hwparams);
  if (err) {
    goto err;
  }
  //oxygendbg("runtime: %8.8X\n", runtime);
  int periods = max(1, dmabuffsize / PCMBUFFERPAGESIZE);
  runtime->periods = periods;
  runtime->period_size = (dmabuffsize / periods) >> 1;
  oxygendbg("periods: %u  size: %u\n", runtime->periods, runtime->period_size);
  aui->card_dmasize = aui->card_dma_buffer_size = dmabuffsize;
  aui->card_samples_per_int = runtime->period_size >> 2;

  return 0;

 err:
  if (runtime->dma_buffer_p) snd_dma_free_pages(runtime->dma_buffer_p);
  if (runtime) kfree(runtime);
  if (substream) kfree(substream);
  return -1;
}

extern void oxygen_card_free(struct snd_card *card);

static void OXYGEN_close (struct mpxplay_audioout_info_s *aui)
{
  struct oxygen_card_s *card = aui->card_private_data;
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
      oxygen_card_free(card->linux_snd_card);
    if (card->pci_dev)
      pds_free(card->pci_dev);
    pds_free(card);
    aui->card_private_data = NULL;
  }
}

static void OXYGEN_card_info (struct mpxplay_audioout_info_s *aui)
{
  struct oxygen_card_s *card = aui->card_private_data;
  char sout[100];
  sprintf(sout, "OXYGEN : %s (%4.4X) IRQ %u", card->pci_dev->device_name, card->pci_dev->device_id, card->irq);
  pds_textdisplay_printf(sout);
}

static int OXYGEN_adetect (struct mpxplay_audioout_info_s *aui)
{
  struct oxygen_card_s *card;
  uint32_t iobase;
  int err;

  oxygendbg("adetect\n");

  card = (struct oxygen_card_s *)pds_zalloc(sizeof(struct oxygen_card_s));
  if (!card)
    return 0;
  aui->card_private_data = card;

  card->pci_dev = (struct pci_config_s *)pds_zalloc(sizeof(struct pci_config_s));
  if (!card->pci_dev)
    goto err_adetect;

  if (pcibios_search_devices(oxygen_devices, card->pci_dev) != PCI_SUCCESSFUL)
    goto err_adetect;

  iobase = pcibios_ReadConfig_Dword(card->pci_dev, PCIR_NAMBAR);
  iobase &= 0xfffffff8;
  if (!iobase)
    goto err_adetect;

  aui->card_irq = card->irq = pcibios_ReadConfig_Byte(card->pci_dev, PCIR_INTR_LN);
#ifdef SBEMU
  aui->card_pci_dev = card->pci_dev;
#endif
  if ((aui->card_select_config & 1) == 0) {
    oxygen_ops = &oxygen_multich_ops;
  } else {
    oxygen_ops = &oxygen_spdif_ops;
  }

  card->linux_snd_card = pds_zalloc(sizeof(struct snd_card));
  card->linux_pci_dev = pds_zalloc(sizeof(struct pci_dev));
  card->linux_pci_dev->pcibios_dev = card->pci_dev;
  card->linux_pci_dev->irq = card->irq;
  card->linux_pci_dev->vendor = card->pci_dev->vendor_id;
  card->linux_pci_dev->device = card->pci_dev->device_id;
  card->linux_pci_dev->subsystem_vendor = pcibios_ReadConfig_Dword(card->pci_dev, PCIR_SSVID);
  card->linux_pci_dev->subsystem_device = pcibios_ReadConfig_Dword(card->pci_dev, PCIR_SSID);
  card->linux_pci_dev->revision = pcibios_ReadConfig_Byte(card->pci_dev, PCIR_RID);
  oxygendbg("pci subsystem %4.4X:%4.4X\n", card->linux_pci_dev->subsystem_vendor, card->linux_pci_dev->subsystem_device);
  //err = snd_oxygen_probe(card->linux_snd_card, card->linux_pci_dev, pcidevids);
  int probe_only = aui->card_controlbits & AUINFOS_CARDCNTRLBIT_TESTCARD;
  err = snd_oxygen_probe(card->linux_snd_card, card->linux_pci_dev, probe_only);
  if (err) goto err_adetect;
  //aui->mpu401 = 1;
  oxygendbg("OXYGEN : %s (%4.4X) IRQ %u\n", card->pci_dev->device_name, card->pci_dev->device_id, card->irq);

  return 1;

err_adetect:
  OXYGEN_close(aui);
  return 0;
}

static void OXYGEN_setrate (struct mpxplay_audioout_info_s *aui)
{
  struct oxygen_card_s *card = aui->card_private_data;
  int err;

  oxygendbg("setrate %u\n", aui->freq_card);
  if (oxygen_ops == &oxygen_ac97_ops) {
    aui->freq_card = 48000;
  } else {
    if (aui->freq_card < 32000) {
      aui->freq_card = 32000;
    } else if (aui->freq_card > 192000) {
      aui->freq_card = 192000;
    }
  }
  oxygendbg("-> %u\n", aui->freq_card);
  err = make_snd_pcm_substream(aui, card, &card->pcm_substream);
  if (err) goto err_setrate;
  oxygen_ops->prepare(card->pcm_substream);
  return;

 err_setrate:
  oxygendbg("setrate error\n");
}

static void OXYGEN_start (struct mpxplay_audioout_info_s *aui)
{
  oxygendbg("start\n");
  struct oxygen_card_s *card = aui->card_private_data;
  oxygen_ops->trigger(card->pcm_substream, SNDRV_PCM_TRIGGER_START);
}

static void OXYGEN_stop (struct mpxplay_audioout_info_s *aui)
{
  oxygendbg("stop\n");
  struct oxygen_card_s *card = aui->card_private_data;
  oxygen_ops->trigger(card->pcm_substream, SNDRV_PCM_TRIGGER_STOP);
}

unsigned int oxygen_int_cnt = 0;

static long OXYGEN_getbufpos (struct mpxplay_audioout_info_s *aui)
{
  struct oxygen_card_s *card = aui->card_private_data;
  unsigned long bufpos = oxygen_ops->pointer(card->pcm_substream);
  bufpos <<= 1;
#if OXYGEN_DEBUG > 1
  if ((oxygen_int_cnt % 450) == 0)
    oxygendbg("getbufpos %u / %u\n", bufpos, aui->card_dmasize);
  //if (bufpos == aui->card_dmasize)
  //  oxygendbg("getbufpos %u == dmasize\n", bufpos);
#endif
  if (bufpos < aui->card_dmasize)
    aui->card_dma_lastgoodpos = bufpos;
  return aui->card_dma_lastgoodpos;
}

static aucards_onemixerchan_s OXYGEN_master_vol={AU_MIXCHANFUNCS_PACK(AU_MIXCHAN_MASTER,AU_MIXCHANFUNC_VOLUME),2,{{0,255,8,0},{0,255,0,0}}};
static aucards_allmixerchan_s OXYGEN_mixerset[] = {
 &OXYGEN_master_vol,
 NULL
};

extern uint16_t xonar_stereo_volume_get (struct snd_card *card);
extern int xonar_stereo_volume_put (struct snd_card *card, uint16_t val);

static void OXYGEN_writeMIXER (struct mpxplay_audioout_info_s *aui, unsigned long reg, unsigned long val)
{
  struct oxygen_card_s *card = aui->card_private_data;
  // Xonar DG Analog (DAC) only
  if (card->pci_dev->device_type == OXYGEN_TYPE_XONAR_DG) {
    // map 0-255 to 0,128-255
    uint16_t val1 = val & 0xff;
    uint16_t val2 = (val >> 8) & 0xff;
    val1 = 128 + (val1 >> 1);
    val2 = 128 + (val2 >> 1);
    if (val1 == 128) val1 = 0;
    if (val2 == 128) val2 = 0;
    val = ((val2 << 8) & 0xff00) | (val1 & 0xff);
    oxygendbg("write mixer val: %X\n", val);
    xonar_stereo_volume_put(card->linux_snd_card, val);
  }
}

static unsigned long OXYGEN_readMIXER (struct mpxplay_audioout_info_s *aui, unsigned long reg)
{
  struct oxygen_card_s *card = aui->card_private_data;
  if (card->pci_dev->device_type == OXYGEN_TYPE_XONAR_DG) {
    uint16_t val = xonar_stereo_volume_get(card->linux_snd_card);
    // map 0,128-255 to 0-255
    uint16_t val1 = val & 0xff;
    uint16_t val2 = (val >> 8) & 0xff;
    val1 = ((val1 - 128) << 1) + 1;
    val2 = ((val2 - 128) << 1) + 1;
    if (val1 == 1) val1 = 0;
    if (val2 == 1) val2 = 0;
    val = ((val2 << 8) & 0xff00) | (val1 & 0xff);
    oxygendbg("read mixer returning %X\n", val);
    return (unsigned long)val;
  } else {
    return 0xffff;
  }
}

extern irqreturn_t oxygen_interrupt(int irq, void *dev_id);

static int OXYGEN_IRQRoutine (struct mpxplay_audioout_info_s *aui)
{
  struct oxygen_card_s *card = aui->card_private_data;
  int handled = oxygen_interrupt(card->irq, card->linux_snd_card->private_data);
#if OXYGEN_DEBUG
  if (handled) {
    if ((oxygen_int_cnt % 500) == 0) DBG_Logi("oxygenirq %u\n", oxygen_int_cnt);
    oxygen_int_cnt++;
  }
#endif
  return handled;
}

#if 0
extern unsigned char oxygen_mpu401_read (void *card, unsigned int idx);
extern void oxygen_mpu401_write (void *card, unsigned int idx, unsigned char data);

static uint8_t OXYGEN_mpu401_read (struct mpxplay_audioout_info_s *aui, unsigned int idx)
{
  struct oxygen_card_s *card = aui->card_private_data;
  return oxygen_mpu401_read(card, idx);
}

static void OXYGEN_mpu401_write (struct mpxplay_audioout_info_s *aui, unsigned int idx, uint8_t data)
{
  struct oxygen_card_s *card = aui->card_private_data;
  oxygen_mpu401_write(card, idx, data);
}
#endif

one_sndcard_info OXYGEN_sndcard_info = {
 "OXYGEN",
 SNDCARD_LOWLEVELHAND|SNDCARD_INT08_ALLOWED,

 NULL,
 NULL,
 &OXYGEN_adetect,
 &OXYGEN_card_info,
 &OXYGEN_start,
 &OXYGEN_stop,
 &OXYGEN_close,
 &OXYGEN_setrate,

 &MDma_writedata,
 &OXYGEN_getbufpos,
 &MDma_clearbuf,
 &MDma_interrupt_monitor,
 &OXYGEN_IRQRoutine,

 &OXYGEN_writeMIXER,
 &OXYGEN_readMIXER,
 &OXYGEN_mixerset[0],

 NULL,
 NULL,
 NULL,
 NULL,
 //&OXYGEN_mpu401_write,
 //&OXYGEN_mpu401_read,
};

#endif // AUCARDS_LINK_OXYGEN
