#include <H1B3C/peripherals/usb.h>

#include <common/logger.h>
#include <common/peripherals/define.h>

DEFINE_BASIC_REGISTER(gotgctl)
DEFINE_BASIC_REGISTER(gotgint)
DEFINE_BASIC_REGISTER(gahbcfg)
DEFINE_BASIC_REGISTER(gusbcfg)
DEFINE_BASIC_REGISTER(grstctl)
DEFINE_BASIC_REGISTER(gintsts)
DEFINE_BASIC_REGISTER(gintmsk)
DEFINE_BASIC_REGISTER(grxstsr)
DEFINE_BASIC_REGISTER(grxstsp)
DEFINE_BASIC_REGISTER(grxfsiz)
DEFINE_BASIC_REGISTER(gnptxfsiz)
DEFINE_BASIC_REGISTER(ggpio)
DEFINE_BASIC_REGISTER(guid)
DEFINE_BASIC_REGISTER(gsnpsid)
DEFINE_BASIC_REGISTER(ghwcfg1)
DEFINE_BASIC_REGISTER(ghwcfg2)
DEFINE_BASIC_REGISTER(ghwcfg3)
DEFINE_BASIC_REGISTER(ghwcfg4)
DEFINE_BASIC_REGISTER(gdfifocfg)
DEFINE_BASIC_REGISTER(dcfg)
DEFINE_BASIC_REGISTER(dctl)
DEFINE_BASIC_REGISTER(dsts)
DEFINE_BASIC_REGISTER(diepmsk)
DEFINE_BASIC_REGISTER(doepmsk)
DEFINE_BASIC_REGISTER(daint)
DEFINE_BASIC_REGISTER(daintmsk)
DEFINE_BASIC_REGISTER(dvbusdis)
DEFINE_BASIC_REGISTER(dvbuspulse)
DEFINE_BASIC_REGISTER(dthrctl)
DEFINE_BASIC_REGISTER(diepempmsk)
DEFINE_BASIC_REGISTER(diepctl0)
DEFINE_BASIC_REGISTER(diepint0)
DEFINE_BASIC_REGISTER(dieptsiz0)
DEFINE_BASIC_REGISTER(diepdma0)
DEFINE_BASIC_REGISTER(dtxfsts0)
DEFINE_BASIC_REGISTER(diepdmab0)
DEFINE_BASIC_REGISTER(doepctl0)
DEFINE_BASIC_REGISTER(doepint0)
DEFINE_BASIC_REGISTER(doeptsiz0)
DEFINE_BASIC_REGISTER(doepdma0)
DEFINE_BASIC_REGISTER(doepdmab0)
DEFINE_BASIC_REGISTER(pcgcctl)

static const BasicRegister usb_registers[] = {
  BASIC_REGISTER_ENTRY(REG_USB_GOTGCTL_OFFSET, gotgctl),
  BASIC_REGISTER_ENTRY(REG_USB_GOTGINT_OFFSET, gotgint),
  BASIC_REGISTER_ENTRY(REG_USB_GAHBCFG_OFFSET, gahbcfg),
  BASIC_REGISTER_ENTRY(REG_USB_GUSBCFG_OFFSET, gusbcfg),
  BASIC_REGISTER_ENTRY(REG_USB_GRSTCTL_OFFSET, grstctl),
  BASIC_REGISTER_ENTRY(REG_USB_GINTSTS_OFFSET, gintsts),
  BASIC_REGISTER_ENTRY(REG_USB_GINTMSK_OFFSET, gintmsk),
  BASIC_REGISTER_ENTRY(REG_USB_GRXSTSR_OFFSET, grxstsr),
  BASIC_REGISTER_ENTRY(REG_USB_GRXSTSP_OFFSET, grxstsp),
  BASIC_REGISTER_ENTRY(REG_USB_GRXFSIZ_OFFSET, grxfsiz),
  BASIC_REGISTER_ENTRY(REG_USB_GNPTXFSIZ_OFFSET, gnptxfsiz),
  BASIC_REGISTER_ENTRY(REG_USB_GGPIO_OFFSET, ggpio),
  BASIC_REGISTER_ENTRY(REG_USB_GUID_OFFSET, guid),
  BASIC_REGISTER_ENTRY(REG_USB_GSNPSID_OFFSET, gsnpsid),
  BASIC_REGISTER_ENTRY(REG_USB_GHWCFG1_OFFSET, ghwcfg1),
  BASIC_REGISTER_ENTRY(REG_USB_GHWCFG2_OFFSET, ghwcfg2),
  BASIC_REGISTER_ENTRY(REG_USB_GHWCFG3_OFFSET, ghwcfg3),
  BASIC_REGISTER_ENTRY(REG_USB_GHWCFG4_OFFSET, ghwcfg4),
  BASIC_REGISTER_ENTRY(REG_USB_GDFIFOCFG_OFFSET, gdfifocfg),
  BASIC_REGISTER_ENTRY(REG_USB_DCFG_OFFSET, dcfg),
  BASIC_REGISTER_ENTRY(REG_USB_DCTL_OFFSET, dctl),
  BASIC_REGISTER_ENTRY(REG_USB_DSTS_OFFSET, dsts),
  BASIC_REGISTER_ENTRY(REG_USB_DIEPMSK_OFFSET, diepmsk),
  BASIC_REGISTER_ENTRY(REG_USB_DOEPMSK_OFFSET, doepmsk),
  BASIC_REGISTER_ENTRY(REG_USB_DAINT_OFFSET, daint),
  BASIC_REGISTER_ENTRY(REG_USB_DAINTMSK_OFFSET, daintmsk),
  BASIC_REGISTER_ENTRY(REG_USB_DVBUSDIS_OFFSET, dvbusdis),
  BASIC_REGISTER_ENTRY(REG_USB_DVBUSPULSE_OFFSET, dvbuspulse),
  BASIC_REGISTER_ENTRY(REG_USB_DTHRCTL_OFFSET, dthrctl),
  BASIC_REGISTER_ENTRY(REG_USB_DIEPEMPMSK_OFFSET, diepempmsk),
  BASIC_REGISTER_ENTRY(REG_USB_DIEPCTL0_OFFSET, diepctl0),
  BASIC_REGISTER_ENTRY(REG_USB_DIEPINT0_OFFSET, diepint0),
  BASIC_REGISTER_ENTRY(REG_USB_DIEPTSIZ0_OFFSET, dieptsiz0),
  BASIC_REGISTER_ENTRY(REG_USB_DIEPDMA0_OFFSET, diepdma0),
  BASIC_REGISTER_ENTRY(REG_USB_DTXFSTS0_OFFSET, dtxfsts0),
  BASIC_REGISTER_ENTRY(REG_USB_DIEPDMAB0_OFFSET, diepdmab0),
  BASIC_REGISTER_ENTRY(REG_USB_DOEPCTL0_OFFSET, doepctl0),
  BASIC_REGISTER_ENTRY(REG_USB_DOEPINT0_OFFSET, doepint0),
  BASIC_REGISTER_ENTRY(REG_USB_DOEPTSIZ0_OFFSET, doeptsiz0),
  BASIC_REGISTER_ENTRY(REG_USB_DOEPDMA0_OFFSET, doepdma0),
  BASIC_REGISTER_ENTRY(REG_USB_DOEPDMAB0_OFFSET, doepdmab0),
  BASIC_REGISTER_ENTRY(REG_USB_PCGCCTL_OFFSET, pcgcctl),
};

static uint64_t USB_Read(Component *self, uint64_t offset, unsigned size) {
  const BasicRegister *reg = basic_register_find(usb_registers, ARRAY_LEN(usb_registers), offset);

  if (reg != NULL) {
    LOG_VBS("USB: read %s (size=%u) -> 0x%08x\n", reg->name, size, *reg->value);
    return *reg->value;
  }

  component_log_unhandled(self, "read", offset);
  return 0;
}

static void USB_Write(Component *self, uint64_t offset, unsigned size, uint64_t value) {
  const BasicRegister *reg = basic_register_find(usb_registers, ARRAY_LEN(usb_registers), offset);

  if (reg != NULL) {
    *reg->value = (uint32_t)value;
    LOG_VBS("USB: wrote %s (size=%u) <- 0x%08x\n", reg->name, size, *reg->value);
    return;
  }

  component_log_unhandled(self, "write", offset);
}

Component *USB_Initialize(void) {
  gotgctl = 0;
  gotgint = 0;
  gahbcfg = 0;
  gusbcfg = 0;
  grstctl = 0;
  gintsts = 0;
  gintmsk = 0;
  grxstsr = 0;
  grxstsp = 0;
  grxfsiz = 0;
  gnptxfsiz = 0;
  ggpio = 0;
  guid = 0;
  gsnpsid = 0;
  ghwcfg1 = 0;
  ghwcfg2 = 0;
  ghwcfg3 = 0;
  ghwcfg4 = 0;
  gdfifocfg = 0;
  dcfg = 0;
  dctl = 0;
  dsts = 0;
  diepmsk = 0;
  doepmsk = 0;
  daint = 0;
  daintmsk = 0;
  dvbusdis = 0;
  dvbuspulse = 0;
  dthrctl = 0;
  diepempmsk = 0;
  diepctl0 = 0;
  diepint0 = 0;
  dieptsiz0 = 0;
  diepdma0 = 0;
  dtxfsts0 = 0;
  diepdmab0 = 0;
  doepctl0 = 0;
  doepint0 = 0;
  doeptsiz0 = 0;
  doepdma0 = 0;
  doepdmab0 = 0;
  pcgcctl = 0;

  return component_register("USB", NULL, USB_Read, USB_Write);
}
