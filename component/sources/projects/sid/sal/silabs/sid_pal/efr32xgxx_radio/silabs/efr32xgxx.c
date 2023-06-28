/***************************************************************************//**
 * @file
 * @brief efr32xgxx.c
 *******************************************************************************
 * # License
 * <b>Copyright 2023 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * SPDX-License-Identifier: Zlib
 *
 * The licensor of this software is Silicon Laboratories Inc.
 * Your use of this software is governed by the terms of
 * Silicon Labs Master Software License Agreement (MSLA)available at
 * www.silabs.com/about-us/legal/master-software-license-agreement.
 * This software contains Third Party Software licensed by Silicon Labs from
 * Amazon.com Services LLC and its affiliates and is governed by the sections
 * of the MSLA applicable to Third Party Software and the additional terms set
 * forth in amazon_sidewalk_license.txt.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *  claim that you wrote the original software. If you use this software
 *  in a product, an acknowledgment in the product documentation would be
 *  appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *  misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 ******************************************************************************/

// -----------------------------------------------------------------------------
//                                   Includes
// -----------------------------------------------------------------------------
#include <stdbool.h>
#include <sid_clock_ifc.h>
#include <sid_pal_delay_ifc.h>
#include <sid_pal_log_ifc.h>
#include <sid_pal_assert_ifc.h>
#include "silabs/efr32xgxx.h"
#include "efr32xgxx_radio.h"

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------
#define TX_FIFO_SIZE                                256 // Any power of 2 from [64, 4096] on the EFR32
#define RF_RANDOM_TIMES                             8
#define RSSI_QUARTER_ORDER                          2

#define EFR32XGXX_PHR_CRC16_BYTES                   2
#define EFR32XGXX_PHR_CRC32_BYTES                   4
#define EFR32XGXX_PHR_FIELD_LEN_SHIFT_BITS          8
#define EFR32XGXX_PHR_FIELD_CRC_SHIFT_BITS          4
#define EFR32XGXX_PHR_FIELD_WHITENING_SHIFT_BITS    3
#define EFR32XGXX_PHR_FIELD_MODESW_SHIFT_BITS       7
#define EFR32XGXX_PHR_FIELD_LEN_HI_MASK             0x07
#define EFR32XGXX_PHR_FIELD_LEN_LO_MASK             0xff

#define EFR32XGXX_RAIL_MAXPOWER                     20

#define EFR32XGXX_RAIL_INVALID_IDX                  -1
#define EFR32XGXX_RAIL_50KBPS_IDX                   0
#define EFR32XGXX_RAIL_150KBPS_IDX                  1
#define EFR32XGXX_RAIL_250KBPS_IDX                  2

#define EFR32XGXX_SYNCWORD_BYTES                    8

#define EFR32XGXX_CRC_16_TYPE                       0
#define EFR32XGXX_CRC_32_TYPE                       1
#define EFR32XGXX_CRC_BYTES_ZERO                    0
#define EFR32XGXX_CRC_BYTES_ONE                     1
#define EFR32XGXX_CRC_BYTES_TWO                     2

#define EFR32XGXX_MAX_PAYLOAD                       255
#define EFR32XGXX_PHR_LOW_BYTE                      0
#define EFR32XGXX_PHR_HIGH_BYTE                     1
#define EFR32XGXX_PHR_ENABLE                        1

#define POLYNOMIAL_CRC16                            0x1021
#define POLYNOMIAL_CRC32                            0x04C11DB7

// -----------------------------------------------------------------------------
//                          Static Function Declarations
// -----------------------------------------------------------------------------
static void radio_irq(RAIL_Handle_t rail_handle, RAIL_Events_t events);
static inline uint32_t efr32xgxx_get_gfsk_crc_len_in_bytes(efr32xgxx_gfsk_crc_types_t crc_type);
static void radio_cfg_changed_hander(RAIL_Handle_t rail_handle, const RAIL_ChannelConfigEntry_t *entry);
static void efr32xgxx_set_radio_idle(void);
static void efr32xgxx_rfready(RAIL_Handle_t rail_handle);
static void efr32xgxx_event_notify(sid_pal_radio_events_t radio_event);
static void radio_irq(RAIL_Handle_t rail_handle, RAIL_Events_t events);
static void efr32xgxx_rx_timer_expired(RAIL_Handle_t rail_handle);

// -----------------------------------------------------------------------------
//                                Global Variables
// -----------------------------------------------------------------------------
extern const RAIL_ChannelConfig_t *efr32xgxx_channelConfigs[]; // PHY link

// -----------------------------------------------------------------------------
//                                Static Variables
// -----------------------------------------------------------------------------
static RAIL_Handle_t g_rail_handle = NULL;
static __ALIGNED(RAIL_FIFO_ALIGNMENT) uint8_t tx_fifo[TX_FIFO_SIZE];
static int8_t last_set_tx_power_level = 0;
static uint8_t preamble_detected = 0;
static uint16_t g_channel = 0;
static int8_t rf_profile = EFR32XGXX_RAIL_50KBPS_IDX;
static bool platform_init_once = false;
static bool radio_init_once = false;
static bool tx_pwr_cfg_init_once = false;
static sid_pal_radio_events_t last_radio_event = SID_PAL_RADIO_EVENT_UNKNOWN;
static RAIL_Config_t rail_cfg = {   // Must never be const
  .eventsCallback = &radio_irq,
  .protocol = NULL,                 // For BLE, RAIL needs additional state memory
  .scheduler = NULL,                // For MultiProtocol, additional scheduler memory
};

// -----------------------------------------------------------------------------
//                          Public Function Definitions
// -----------------------------------------------------------------------------
uint32_t compute_crc32(const uint8_t *buffer, uint16_t length)
{
  if (!buffer || !length) {
    return 0;
  }

  uint8_t temp_buffer[sizeof(uint32_t)] = { 0 };
  uint32_t crc32                        = 0xFFFFFFFF;
  const uint8_t *buffer_local;
  uint16_t length_local;

  if (length < sizeof(uint32_t)) {
    memcpy(temp_buffer, buffer, length);
    length_local = sizeof(uint32_t);
    buffer_local = temp_buffer;
  } else {
    length_local = length;
    buffer_local = buffer;
  }

  for (uint16_t index_buffer = 0; index_buffer < length_local; index_buffer++) {
    crc32 ^= (index_buffer < length) ? ((uint32_t)buffer_local[index_buffer] << 24) : 0x00000000;

    for (uint8_t i = 0; i < 8; i++) {
      crc32 = (crc32 & 0x80000000) ? (crc32 << 1) ^ POLYNOMIAL_CRC32 : (crc32 << 1);
    }
  }

  return ~crc32;
}

uint16_t compute_crc16(const uint8_t *buffer, uint16_t length)
{
  if (!buffer || !length) {
    return 0;
  }

  uint16_t crc16 = 0x0000;

  for (uint16_t index_buffer = 0; index_buffer < length; index_buffer++) {
    crc16 ^= ((uint16_t)buffer[index_buffer] << 8);

    for (uint8_t i = 0; i < 8; i++) {
      crc16 = (crc16 & 0x8000) ? (crc16 << 1) ^ POLYNOMIAL_CRC16 : (crc16 << 1);
    }
  }

  return crc16;
}

void efr32xgxx_event_handler(void)
{
  const halo_drv_silabs_ctx_t *drv_ctx = efr32xgxx_get_drv_ctx();

  if ((SID_PAL_RADIO_EVENT_UNKNOWN != last_radio_event)) {
    drv_ctx->report_radio_event(last_radio_event);
  }
}

void efr32xgxx_radio_irq_process(void)
{
  efr32xgxx_event_handler();
}

uint16_t reverse16(uint16_t n)
{
  return (reverse8(n >> 8) | (reverse8(n & 0xff) << 8));
}

uint8_t reverse8(uint8_t n)
{
  n = ((0xf0 & n) >> 4) | ((0x0f & n) << 4);
  n = ((0xcc & n) >> 2) | ((0x33 & n) << 2);
  n = ((0xaa & n) >> 1) | ((0x55 & n) << 1);
  return n;
}

uint16_t efr32xgxx_set_phr(uint8_t *hdr, uint8_t modesw, uint8_t crc, uint8_t whitening, uint16_t len)
{
  uint16_t tmp_len;
  uint8_t tmp_hdr;

  hdr[EFR32XGXX_PHR_LOW_BYTE] = hdr[EFR32XGXX_PHR_HIGH_BYTE] = 0;

  if (modesw == EFR32XGXX_PHR_ENABLE) {
    hdr[EFR32XGXX_PHR_LOW_BYTE] |= 0x1 << EFR32XGXX_PHR_FIELD_MODESW_SHIFT_BITS;
  }

  if (crc == EFR32XGXX_PHR_ENABLE) {
    hdr[EFR32XGXX_PHR_LOW_BYTE] |= (0x1 << EFR32XGXX_PHR_FIELD_CRC_SHIFT_BITS);
  }

  if (whitening == EFR32XGXX_PHR_ENABLE) {
    hdr[EFR32XGXX_PHR_LOW_BYTE] |= (0x1 << EFR32XGXX_PHR_FIELD_WHITENING_SHIFT_BITS);
  }

  tmp_len = reverse16(len);

  hdr[EFR32XGXX_PHR_LOW_BYTE] |= ((tmp_len << EFR32XGXX_PHR_FIELD_LEN_SHIFT_BITS)
                                  & EFR32XGXX_PHR_FIELD_LEN_HI_MASK);
  hdr[EFR32XGXX_PHR_HIGH_BYTE] = ((tmp_len >> EFR32XGXX_PHR_FIELD_LEN_SHIFT_BITS)
                                  & EFR32XGXX_PHR_FIELD_LEN_LO_MASK);

  tmp_hdr = reverse8(hdr[EFR32XGXX_PHR_LOW_BYTE]);

  hdr[EFR32XGXX_PHR_LOW_BYTE] = tmp_hdr;

  return len;
}

RAIL_Handle_t efr32xgxx_get_railhandle(void)
{
  return g_rail_handle;
}

uint16_t efr32xgxx_get_rxpacket(uint8_t *phr, uint8_t *payload, int8_t *rssi, bool msb)
{
  RAIL_RxPacketInfo_t     pktinfo;
  RAIL_RxPacketDetails_t  pktDetails;
  RAIL_RxPacketHandle_t   pktHandle;
  uint16_t                len = 0;
  RAIL_Status_t           status;

  pktHandle = RAIL_HoldRxPacket(g_rail_handle);
  if (pktHandle == RAIL_RX_PACKET_HANDLE_INVALID) {
    SID_PAL_LOG_ERROR("radio rx pkt hndl invalid");
    goto ret;
  }

  pktHandle = RAIL_GetRxPacketInfo(g_rail_handle, RAIL_RX_PACKET_HANDLE_OLDEST, &pktinfo);
  if (!(pktHandle != RAIL_RX_PACKET_HANDLE_INVALID)
      || ((pktinfo.packetStatus != RAIL_RX_PACKET_READY_SUCCESS)
          && (pktinfo.packetStatus != RAIL_RX_PACKET_READY_CRC_ERROR))) {
    SID_PAL_LOG_ERROR("radio unexpected rx pkt status: %d", pktinfo.packetStatus);
    goto ret;
  }

  status = RAIL_GetRxPacketDetails(g_rail_handle, pktHandle, &pktDetails);
  if (status != RAIL_STATUS_NO_ERROR) {
    SID_PAL_LOG_ERROR("radio get rx pkt detail err: %d", status);
    goto ret;
  }

  if (pktinfo.packetBytes <= EFR32XGXX_MAX_PAYLOAD) {
    uint16_t peek_len = RAIL_PeekRxPacket(g_rail_handle, pktHandle, payload, pktinfo.packetBytes, 0);
    if (peek_len != pktinfo.packetBytes) {
      SID_PAL_LOG_ERROR("radio rx pkt len not consistent: %d", peek_len);
      goto ret;
    }

    memcpy(phr, payload, EFR32XGXX_PHR_LENGTH);

    if (msb) {
      for (uint16_t i = 0; i < pktinfo.packetBytes - EFR32XGXX_PHR_LENGTH; i++) {
        payload[i] = reverse8(payload[i + EFR32XGXX_PHR_LENGTH]);
      }
    } else {
      SID_PAL_LOG_ERROR("radio unsupported endianness");
      goto ret;
    }
  } else {
    SID_PAL_LOG_ERROR("radio rx pkt len more than supported: %d", pktinfo.packetBytes);
    goto ret;
  }

  len = reverse16((phr[EFR32XGXX_PHR_LOW_BYTE] << EFR32XGXX_PHR_FIELD_LEN_SHIFT_BITS
                   | phr[EFR32XGXX_PHR_HIGH_BYTE]));
  len = ((len & EFR32XGXX_PHR_FIELD_LEN_HI_MASK) << EFR32XGXX_PHR_FIELD_LEN_SHIFT_BITS)
        | ((len >> EFR32XGXX_PHR_FIELD_LEN_SHIFT_BITS) & EFR32XGXX_PHR_FIELD_LEN_LO_MASK);
  *rssi = pktDetails.rssi;

  status = RAIL_ReleaseRxPacket(g_rail_handle, pktHandle);
  if (status != RAIL_STATUS_NO_ERROR) {
    SID_PAL_LOG_ERROR("radio release rx pkt err: %d", status);
    goto ret;
  }

  ret:
  return len;
}

int32_t efr32xgxx_set_platform(void)
{
  int32_t err = RADIO_ERROR_NONE;

  if (platform_init_once) {
    // Already initialized
    err = RADIO_ERROR_NONE;
    goto ret;
  }

  g_rail_handle = RAIL_Init(&rail_cfg, &efr32xgxx_rfready);
  if (g_rail_handle == NULL) {
    SID_PAL_LOG_ERROR("radio rail init err");
    err = RADIO_ERROR_HARDWARE_ERROR;
    goto ret;
  }
  while (!RAIL_IsInitialized()) ;
  platform_init_once = true;

  ret:
  RAIL_Idle(g_rail_handle, RAIL_IDLE, true);

  return err;
}

int32_t efr32xgxx_set_radio_init_hard(void)
{
  int32_t err = RADIO_ERROR_NONE;
  RAIL_Status_t status;
  RAIL_CalMask_t pending_calib;

  if (radio_init_once) {
    // Already calibrated
    err = RADIO_ERROR_NONE;
    goto ret;
  }

  if (rf_profile == EFR32XGXX_RAIL_INVALID_IDX) {
    SID_PAL_LOG_ERROR("radio wrong rf profile: %d", rf_profile);
    err = RADIO_ERROR_HARDWARE_ERROR;
    goto ret;
  }

  RAIL_EnablePaCal(true);

  status = RAIL_ConfigCal(g_rail_handle, RAIL_CAL_ALL);
  if (status != RAIL_STATUS_NO_ERROR) {
    SID_PAL_LOG_ERROR("radio calibration cfg err: %d", status);
    err = RADIO_ERROR_HARDWARE_ERROR;
    goto ret;
  }

  pending_calib = RAIL_GetPendingCal(g_rail_handle);

  if (pending_calib & RAIL_CAL_TEMP_VCO) {
    status = RAIL_CalibrateTemp(g_rail_handle);
    if (status != RAIL_STATUS_NO_ERROR) {
      SID_PAL_LOG_ERROR("radio temp calibration err: %d", status);
      err = RADIO_ERROR_HARDWARE_ERROR;
      goto ret;
    }
  }

  if (pending_calib & RAIL_CAL_ONETIME_IRCAL) {
    status = RAIL_CalibrateIr(g_rail_handle, NULL);
    if (status != RAIL_STATUS_NO_ERROR) {
      SID_PAL_LOG_ERROR("radio ir calibration err: %d", status);
      err = RADIO_ERROR_HARDWARE_ERROR;
      goto ret;
    }
  }

  radio_init_once = true;

  ret:
  return err;
}

int32_t efr32xgxx_set_radio_init(void)
{
  int32_t err = RADIO_ERROR_NONE;
  RAIL_Status_t status;

  err = efr32xgxx_set_radio_init_hard();
  if (err != RADIO_ERROR_NONE) {
    err = RADIO_ERROR_HARDWARE_ERROR;
    goto ret;
  }

  RAIL_ConfigChannels(g_rail_handle, efr32xgxx_channelConfigs[rf_profile], &radio_cfg_changed_hander);

  RAIL_Events_t events = RAIL_EVENT_CAL_NEEDED
                         | RAIL_EVENT_RX_PACKET_RECEIVED
                         | RAIL_EVENT_RX_FRAME_ERROR
                         | RAIL_EVENT_RX_FIFO_OVERFLOW
                         | RAIL_EVENT_RX_PACKET_ABORTED
                         | RAIL_EVENT_TX_PACKET_SENT
                         | RAIL_EVENT_TX_ABORTED
                         | RAIL_EVENT_TX_BLOCKED
                         | RAIL_EVENT_TX_UNDERFLOW
                         | RAIL_EVENT_RX_PREAMBLE_DETECT;

  status = RAIL_ConfigEvents(g_rail_handle, RAIL_EVENTS_ALL, events);
  if (status != RAIL_STATUS_NO_ERROR) {
    SID_PAL_LOG_ERROR("radio events cfg err: %d", status);
    err = RADIO_ERROR_HARDWARE_ERROR;
    goto ret;
  }

  RAIL_StateTransitions_t railStateTransitions = {
    .success = RAIL_RF_STATE_RX,
    .error   = RAIL_RF_STATE_RX,
  };

  status = RAIL_SetRxTransitions(g_rail_handle, &railStateTransitions);
  if (status != RAIL_STATUS_NO_ERROR) {
    SID_PAL_LOG_ERROR("radio rx trans err: %d", status);
    err = RADIO_ERROR_HARDWARE_ERROR;
    goto ret;
  }

  status = RAIL_SetTxTransitions(g_rail_handle, &railStateTransitions);
  if (status != RAIL_STATUS_NO_ERROR) {
    SID_PAL_LOG_ERROR("radio tx trans err err: %d", status);
    err = RADIO_ERROR_HARDWARE_ERROR;
    goto ret;
  }

  if (!RAIL_SetTxFifo(g_rail_handle, tx_fifo, 0, TX_FIFO_SIZE)) {
    SID_PAL_LOG_ERROR("radio set tx fifo err");
    err = RADIO_ERROR_HARDWARE_ERROR;
    goto ret;
  }

  status = RAIL_InitPowerManager();
  if (status != RAIL_STATUS_NO_ERROR) {
    SID_PAL_LOG_ERROR("radio pwr mngr init err: %d", status);
    err = RADIO_ERROR_HARDWARE_ERROR;
    goto ret;
  }

  status = RAIL_ConfigSleep(g_rail_handle, RAIL_SLEEP_CONFIG_TIMERSYNC_ENABLED);
  if (status != RAIL_STATUS_NO_ERROR) {
    SID_PAL_LOG_ERROR("radio sleep cfg err: %d", status);
    err = RADIO_ERROR_HARDWARE_ERROR;
    goto ret;
  }

  ret:
  return err;
}

void efr32xgxx_get_txpower(int8_t *max_tx_power, int8_t *min_tx_power)
{
  const halo_drv_silabs_ctx_t *drv_ctx = efr32xgxx_get_drv_ctx();
  RAIL_TxPowerCurves_t *min_max_in_deci = (RAIL_TxPowerCurves_t *)RAIL_GetTxPowerCurve(drv_ctx->config->tx_power_cfg.mode);

  if (max_tx_power) {
    *max_tx_power = min_max_in_deci->maxPower / 10;
  }
  if (min_tx_power) {
    *min_tx_power = min_max_in_deci->minPower / 10;
  }
}

int32_t efr32xgxx_set_txpower(int8_t power)
{
  RAIL_Status_t status;
  int32_t err = RADIO_ERROR_NONE;
  const halo_drv_silabs_ctx_t *drv_ctx = efr32xgxx_get_drv_ctx();

  if (!tx_pwr_cfg_init_once) {
    status = RAIL_ConfigTxPower(g_rail_handle, &drv_ctx->config->tx_power_cfg);
    if (status != RAIL_STATUS_NO_ERROR) {
      SID_PAL_LOG_ERROR("radio PA init err: %d", status);
      err = RADIO_ERROR_HARDWARE_ERROR;
      goto ret;
    }
  }

  if (power != last_set_tx_power_level) {
    int8_t max_pwr;

    efr32xgxx_get_txpower(&max_pwr, NULL);
    if (power > max_pwr) {
      power = max_pwr;
    }

    RAIL_TxPower_t powerLevelDeciDbm = (int16_t)power * 10;     // convert from dBm to deci-dBm
    RAIL_TxPowerLevel_t powerLevelRaw = RAIL_ConvertDbmToRaw(g_rail_handle, drv_ctx->config->tx_power_cfg.mode, powerLevelDeciDbm);

    status = RAIL_SetTxPower(g_rail_handle, powerLevelRaw);
    if (status != RAIL_STATUS_NO_ERROR) {
      SID_PAL_LOG_ERROR("radio set tx pwr err: %d", status);
      err = RADIO_ERROR_HARDWARE_ERROR;
      goto ret;
    }

    last_set_tx_power_level = power;
  }

  tx_pwr_cfg_init_once = true;

  ret:
  return err;
}

int32_t efr32xgxx_set_sleep(void)
{
  efr32xgxx_set_radio_idle();

  return RADIO_ERROR_NONE;
}

int32_t efr32xgxx_set_standby(void)
{
  efr32xgxx_set_radio_idle();

  return RADIO_ERROR_NONE;
}

int32_t efr32xgxx_set_tx_payload(const uint8_t *buffer, uint8_t size, bool msb)
{
  uint8_t data[EFR32XGXX_FSK_MAX_PAYLOAD_LENGTH] = { 0 };
  uint8_t *payload = data;
  int32_t err = RADIO_ERROR_NONE;

  if (msb) {
    memcpy(payload, buffer, EFR32XGXX_PHR_LENGTH);
    for (uint16_t i = EFR32XGXX_PHR_LENGTH; i < (size - EFR32XGXX_PHR_LENGTH); i++) {
      payload[i] = reverse8(buffer[i]);
    }
  } else {
    payload = (uint8_t *)buffer;
  }

  if (!RAIL_WriteTxFifo(g_rail_handle, payload, size, true)) {
    SID_PAL_LOG_ERROR("radio write tx fifo err");
    err = RADIO_ERROR_HARDWARE_ERROR;
    goto ret;
  }

  ret:
  return err;
}

static void efr32xgxx_tx_timer_expired(RAIL_Handle_t rail_handle)
{
  efr32xgxx_event_notify(SID_PAL_RADIO_EVENT_TX_TIMEOUT);
  RAIL_Idle(rail_handle, RAIL_IDLE, true);
}

int32_t efr32xgxx_set_tx(const uint32_t timeout)
{
  int32_t err = RADIO_ERROR_NONE;

  efr32xgxx_set_radio_idle();

  RAIL_Status_t status = RAIL_StartTx(g_rail_handle, g_channel, RAIL_TX_OPTION_ALT_PREAMBLE_LEN, NULL);
  if (status != RAIL_STATUS_NO_ERROR) {
    SID_PAL_LOG_ERROR("radio start sch tx err: %d", status);
    err = RADIO_ERROR_HARDWARE_ERROR;
    goto ret;
  }

  if (timeout) {
    status = RAIL_SetTimer(g_rail_handle, timeout, RAIL_TIME_DELAY, &efr32xgxx_tx_timer_expired);
    if (status != RAIL_STATUS_NO_ERROR) {
      SID_PAL_LOG_ERROR("radio set tmr err: %d", status);
      err = RADIO_ERROR_HARDWARE_ERROR;
      goto ret;
    }
  }

  ret:
  return err;
}

int32_t efr32xgxx_set_rx(const uint32_t timeout)
{
  // timeout 0 is continuous receive
  int32_t err = RADIO_ERROR_NONE;
  uint32_t rail_timeout = timeout;

  preamble_detected = 0;

  efr32xgxx_set_radio_idle();

  RAIL_Status_t status = RAIL_StartRx(g_rail_handle, g_channel, NULL);
  if (status != RAIL_STATUS_NO_ERROR) {
    SID_PAL_LOG_ERROR("radio schedule rx err: %d", status);
    err = RADIO_ERROR_HARDWARE_ERROR;
    goto ret;
  }

  if (rail_timeout) {
    RAIL_Status_t status = RAIL_SetTimer(g_rail_handle, rail_timeout, RAIL_TIME_DELAY, &efr32xgxx_rx_timer_expired);
    if (status != RAIL_STATUS_NO_ERROR) {
      SID_PAL_LOG_ERROR("radio set tmr err: %d", status);
      err = RADIO_ERROR_HARDWARE_ERROR;
      goto ret;
    }
  }

  ret:
  return err;
}

int32_t efr32xgxx_set_tx_cw(void)
{
  return RADIO_ERROR_NOT_SUPPORTED;
}

int32_t efr32xgxx_set_tx_cpbl(void)
{
  return RADIO_ERROR_NOT_SUPPORTED;
}

int32_t efr32xgxx_set_rf_freq(const uint32_t freq_in_hz)
{
  // Compute channel from frequency
  g_channel = (freq_in_hz - efr32xgxx_channelConfigs[rf_profile]->configs->baseFrequency) / efr32xgxx_channelConfigs[rf_profile]->configs->channelSpacing;

  return RADIO_ERROR_NONE;
}

/*
 * Todo : Silabs sdk not support functions.
 */
int32_t efr32xgxx_set_gfsk_mod_params(const efr32xgxx_mod_params_gfsk_t *params)
{
  int32_t err = RADIO_ERROR_NONE;
  uint32_t old = params->br_in_bps;

  if (params->br_in_bps != old) {
    if (params->br_in_bps == RADIO_FSK_BR_50KBPS) {
      rf_profile = EFR32XGXX_RAIL_50KBPS_IDX;
    } else if (params->br_in_bps == RADIO_FSK_BR_150KBPS) {
      rf_profile = EFR32XGXX_RAIL_150KBPS_IDX;
    } else if (params->br_in_bps == RADIO_FSK_BR_250KBPS) {
      rf_profile = EFR32XGXX_RAIL_250KBPS_IDX;
    } else {
      SID_PAL_LOG_ERROR("radio wrong rf profile: %d", rf_profile);
      rf_profile = EFR32XGXX_RAIL_INVALID_IDX;
      err = RADIO_ERROR_HARDWARE_ERROR;
      goto ret;
    }
  }

  if (efr32xgxx_set_standby() != RADIO_ERROR_NONE) {
    err = RADIO_ERROR_HARDWARE_ERROR;
    goto ret;
  }

  if (efr32xgxx_set_radio_init() != RADIO_ERROR_NONE) {
    err = RADIO_ERROR_HARDWARE_ERROR;
    goto ret;
  }

  ret:
  return err;
}

int32_t efr32xgxx_set_gfsk_pkt_params(const efr32xgxx_pkt_params_gfsk_t *params)
{
  int32_t err = RADIO_ERROR_NONE;
  RAIL_Status_t status;

  status = RAIL_SetTxAltPreambleLength(g_rail_handle, params->pbl_len_in_bits);
  if (status != RAIL_STATUS_NO_ERROR) {
    SID_PAL_LOG_ERROR("radio preamble set err: %d (bits: %d)", status, params->pbl_len_in_bits);
    err = RADIO_ERROR_HARDWARE_ERROR;
    goto ret;
  }

  ret:
  return err;
}

int32_t efr32xgxx_get_rssi_inst(int16_t *rssi_in_dbm)
{
  int16_t rssi_local;
  int32_t err = RADIO_ERROR_NONE;

  if ((rssi_local = RAIL_GetRssi(g_rail_handle, true)) == RAIL_RSSI_INVALID) {
    err = RADIO_ERROR_HARDWARE_ERROR;
    goto ret;
  }
  *rssi_in_dbm = (rssi_local >> RSSI_QUARTER_ORDER);

  ret:
  return err;
}

uint32_t efr32xgxx_get_gfsk_time_on_air_numerator(const efr32xgxx_pkt_params_gfsk_t* pkt_p)
{
  return pkt_p->pbl_len_in_bits + (pkt_p->hdr_type == EFR32XGXX_GFSK_PKT_VAR_LEN ? 8 : 0)
         + pkt_p->sync_word_len_in_bits
         + ((pkt_p->pld_len_in_bytes + (pkt_p->addr_cmp == EFR32XGXX_GFSK_ADDR_CMP_FILT_OFF ? 0 : 1)
             + efr32xgxx_get_gfsk_crc_len_in_bytes(pkt_p->crc_type)) << 3);
}

uint32_t efr32xgxx_get_gfsk_time_on_air_in_ms(const efr32xgxx_pkt_params_gfsk_t *pkt_p,
                                              const efr32xgxx_mod_params_gfsk_t *mod_p)
{
  uint32_t numerator   = MS_IN_SEC * efr32xgxx_get_gfsk_time_on_air_numerator(pkt_p);
  uint32_t denominator = mod_p->br_in_bps;

  // Perform integral ceil()
  return (numerator + denominator - 1) / denominator;
}

int32_t efr32xgxx_get_random_numbers(uint32_t *numbers, unsigned int n)
{
  int8_t seed, cnt = 0, err_cnt = 0;
  int16_t rssi;
  uint32_t noise = 0, r_val = 0;
  int32_t err = RADIO_ERROR_NONE;

  if (n < 1) {
    n = 1;
  }

  do {
    sid_pal_delay_us(n);
    if (((rssi = sid_pal_radio_rssi()) == 0) || (rssi == INT16_MAX)) {
      if (err_cnt++ > RSSI_ERROR_COUNT) {
        err = RADIO_ERROR_HARDWARE_ERROR;
        goto ret;
      }
      continue;
    }

    noise += (-1 * rssi);
    seed = (noise % 8);
    r_val |= (((noise % 16) & 0xf) << (((seed + cnt) % 8) * 4));
    r_val |= noise;
    cnt++;
  } while (RF_RANDOM_TIMES > cnt);
  *numbers = r_val;

  ret:
  return err;
}

int32_t efr32xgxx_set_gfsk_sync_word(const uint8_t* sync_word, const uint8_t sync_word_len)
{
  return RADIO_ERROR_NONE;
}

// -----------------------------------------------------------------------------
//                          Static Function Definitions
// -----------------------------------------------------------------------------
static inline uint32_t efr32xgxx_get_gfsk_crc_len_in_bytes(efr32xgxx_gfsk_crc_types_t crc_type)
{
  switch (crc_type) {
    case EFR32XGXX_GFSK_CRC_OFF:
      return EFR32XGXX_CRC_BYTES_ZERO;
    case EFR32XGXX_GFSK_CRC_1_BYTE:
    case EFR32XGXX_GFSK_CRC_1_BYTE_INV:
      return EFR32XGXX_CRC_BYTES_ONE;
    case EFR32XGXX_GFSK_CRC_2_BYTES:
    case EFR32XGXX_GFSK_CRC_2_BYTES_INV:
      return EFR32XGXX_CRC_BYTES_TWO;
  }

  return EFR32XGXX_CRC_BYTES_ZERO;
}

static void radio_cfg_changed_hander(RAIL_Handle_t rail_handle, const RAIL_ChannelConfigEntry_t *entry)
{
  (void)rail_handle;
  (void)entry;

  efr32xgxx_set_txpower(last_set_tx_power_level);
}

static void efr32xgxx_set_radio_idle(void)
{
  if (RAIL_IsTimerRunning(g_rail_handle)) {
    RAIL_CancelTimer(g_rail_handle);
  }

  RAIL_Idle(g_rail_handle, RAIL_IDLE, true);
}

static void efr32xgxx_rfready(RAIL_Handle_t rail_handle)
{
}

static void efr32xgxx_event_notify(sid_pal_radio_events_t radio_event)
{
  const halo_drv_silabs_ctx_t *drv_ctx = efr32xgxx_get_drv_ctx();

  last_radio_event = radio_event;

  drv_ctx->irq_handler();
}

// to mimic semtech behaviour
static void radio_irq(RAIL_Handle_t rail_handle, RAIL_Events_t events)
{
  RAIL_Status_t status;

  if (events & RAIL_EVENT_CAL_NEEDED) {
    status = RAIL_Calibrate(rail_handle, NULL, RAIL_CAL_ALL_PENDING);
    if (status != RAIL_STATUS_NO_ERROR) {
      SID_PAL_LOG_ERROR("radio calibration err: %d", status);
    }
  }

  // RX Events

  if (events & (RAIL_EVENT_RX_FIFO_OVERFLOW | RAIL_EVENT_RX_PACKET_ABORTED | RAIL_EVENT_RX_FRAME_ERROR)) {
    if (events & RAIL_EVENT_RX_FIFO_OVERFLOW) {
      efr32xgxx_event_notify(SID_PAL_RADIO_EVENT_RX_ERROR);
      SID_PAL_LOG_ERROR("radio rx fifo overflow");
    }

    if (events & RAIL_EVENT_RX_PACKET_ABORTED) {
      efr32xgxx_event_notify(SID_PAL_RADIO_EVENT_RX_ERROR);
      SID_PAL_LOG_ERROR("radio rx pkt abort");
    }

    if (events & RAIL_EVENT_RX_FRAME_ERROR) {
      SID_PAL_LOG_ERROR("radio rx frame err");
      efr32xgxx_event_notify(SID_PAL_RADIO_EVENT_RX_ERROR);
    }
  } else if (events & RAIL_EVENT_RX_PACKET_RECEIVED) {
    halo_drv_silabs_ctx_t *drv_ctx = efr32xgxx_get_drv_ctx();

    sid_clock_now(SID_CLOCK_SOURCE_UPTIME, &drv_ctx->radio_rx_packet->rcv_tm, NULL);
    if (radio_fsk_process_rx_done(drv_ctx) == RADIO_ERROR_NONE) {
      memset(&drv_ctx->radio_rx_packet->lora_rx_packet_status, 0, sizeof(sid_pal_radio_lora_rx_packet_status_t));
      efr32xgxx_event_notify(SID_PAL_RADIO_EVENT_RX_DONE);
    } else {
      SID_PAL_LOG_ERROR("radio pkt rcv err");
      efr32xgxx_event_notify(SID_PAL_RADIO_EVENT_RX_ERROR);
    }

    efr32xgxx_set_radio_idle();
  } else if (events & RAIL_EVENT_RX_PREAMBLE_DETECT) {
    halo_drv_silabs_ctx_t *drv_ctx = efr32xgxx_get_drv_ctx();

    if (drv_ctx->cad_exit_mode == SID_PAL_RADIO_CAD_EXIT_MODE_CS_LBT) {
      // listen before talk has detected activity on current channel
      // report this event to stack
      drv_ctx->radio_rx_packet->fsk_rx_packet_status.rssi_sync = sid_pal_radio_rssi();
      drv_ctx->cad_exit_mode = SID_PAL_RADIO_CAD_EXIT_MODE_NONE;
      efr32xgxx_event_notify(SID_PAL_RADIO_EVENT_CS_DONE);
    }

    preamble_detected = 1;
  }

  // TX Events

  if (events & (RAIL_EVENT_TX_ABORTED | RAIL_EVENT_TX_BLOCKED | RAIL_EVENT_TX_UNDERFLOW)) {
    efr32xgxx_event_notify(SID_PAL_RADIO_EVENT_TX_TIMEOUT);
    efr32xgxx_set_radio_idle();
    SID_PAL_LOG_ERROR("radio tx err");
  } else if (events & RAIL_EVENT_TX_PACKET_SENT) {
    efr32xgxx_event_notify(SID_PAL_RADIO_EVENT_TX_DONE);
    efr32xgxx_set_radio_idle();
  }
}

static void efr32xgxx_rx_timer_expired(RAIL_Handle_t rail_handle)
{
  halo_drv_silabs_ctx_t *drv_ctx = efr32xgxx_get_drv_ctx();

  if (preamble_detected == 0) {
    if (drv_ctx->cad_exit_mode == SID_PAL_RADIO_CAD_EXIT_MODE_CS_LBT) {
      // rx window was a listen before talk
      // channel is free so we can schedule tx
      drv_ctx->cad_exit_mode = SID_PAL_RADIO_CAD_EXIT_MODE_NONE;
      efr32xgxx_event_notify(SID_PAL_RADIO_EVENT_UNKNOWN);
      sid_pal_radio_start_tx(0);
      return;
    }
    // for standard rx windows, report timeout event
    efr32xgxx_event_notify(SID_PAL_RADIO_EVENT_RX_TIMEOUT);
    // switch radio mode
    RAIL_Idle(rail_handle, RAIL_IDLE, true);
  } else {
    //preamble have been received since start of the rx window
    //continue rx until RAIL_EVENT_RX_PACKET_RECEIVED event
  }
}
