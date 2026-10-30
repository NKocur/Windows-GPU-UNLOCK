/* Exact local RTX3090 + PG500216 guarded unlock RESEARCH.
 * Release hardware verified; full stock core and additional path/fault replay passed.
 * This is experimental admission for one supervised USB attempt, not hardware success.
 */
#if !defined(GU_MIXED_COMPANION_GUARD)
#error Mixed unlock requires the companion and read-only DMA-path guard
#endif
#if defined(GU_MIXED_LIVE_RESEARCH)
#if defined(GU_MIXED_RELEASE_ONLY) || defined(GU_MIXED_PORT_HOST_TEST) || !defined(GU_AUTOMATIC_BOOT) || !defined(GU_HEADLESS_RESEARCH) || !defined(GU_USB_MEDIA)
#error Live mixed research requires the exact guarded USB/headless build
#endif
#if defined(GU_UEFI_PORT) && (!defined(GU_EXPERIMENTAL_3090) || !defined(GU_ENTRY_LATCHED_SS1_FULL) || !defined(GU_INTERNAL_BOOT))
#error Live mixed child requires full immutable entry-latched transaction policy
#endif
#elif !defined(GU_MIXED_PORT_HOST_TEST)
#error This profile is only for explicit guarded research or hosted tests
#endif
#if defined(GU_MIXED_PORT_HOST_TEST) && __STDC_HOSTED__ != 1
#error Hosted transaction tests cannot be booted as firmware
#endif
#define GU_MIXED_UNLOCK_ADMISSION_V1 1
#define GU_MIXED_PROFILE_GENERATOR_V1 1
#define GU_PROFILE_NAME "MSI-B550M-2N-ASUS3090-PG500216-UNLOCK-RESEARCH"
#define GU_BOARD_ID 0x220410deU
#define GU_BOARD_SUBSYSTEM 0x87af1043U
#define GU_TARGET_SEGMENT 0U
#define GU_TARGET_BUS 0x2bU
#define GU_BAR0_BASE 0xfb000000ULL
#define GU_BAR0_END 0xfbffffffULL
#define GU_DEVICE_MEMORY_SIZE 0x6000U
#define GU_CHIP_ID 0xb72000a1U
#define GU_PROM_SHA256 "658b1898b899a3e3efd0313eba1f484229d06c4fde70ac84d04a4128881ff9fd"
#define GU_NO_EDID_DRIVER 1
#define GU_LOCAL_CONSOLE_AGGREGATE 0
#define GU_LOCAL_CHILD_COUNT 1
#define GU_ROOT_PORT_DEVICE 3U
#define GU_ROOT_PORT_FUNCTION 1U
#define GU_RTX_PATH_BYTES 2,1,12,0,208,65,3,10,0,0,0,0,1,1,6,0,1,3,1,1,6,0,0,0,2,3,8,0,112,3,1,128,127,255,4,0
#define GU_NVGOP_VERSION 0x60009U
#define GU_NVGOP_IMAGE_SIZE 0x2b700U
#define GU_NVGOP_STOP 0x698U
#define GU_NVGOP_TEXT_BYTES 0x23d82U
#define GU_NVGOP_TEXT_SHA256 "d045cee2113e461c7c678ec21542b1d0f18356765c0fc6a0bf2dcf824088e011"
#define GU_CONSOLE_TEXT_SIZE 0x60c0U
#define GU_CONSOLE_TEXT_STOP 0x1290U
#define GU_CONSOLE_TEXT_VERSION 0x10U
#define GU_CONSOLE_GFX_SIZE 0x39e0U
#define GU_CONSOLE_GFX_STOP 0xa3cU
#define GU_CONSOLE_GFX_VERSION 0x10U

/* Host core fixture metadata, not a live unlock eligibility switch. */
#define GU_LOCAL_FWSEC_IMAGE_OFF 272540U
#define GU_LOCAL_FWSEC_IMAGE_SIZE 57856U
