/***************************************************************************//**
 * @file
 * @brief app_process.c
 *******************************************************************************
 * # License
 * <b>Copyright 2023 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * SPDX-License-Identifier: Zlib
 *
 * The licensor of this software is Silicon Laboratories Inc.
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
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 ******************************************************************************/

// -----------------------------------------------------------------------------
//                                   Includes
// -----------------------------------------------------------------------------
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include "app_process.h"
#include "app_init.h"
#include "app_assert.h"
#include "app_log.h"
#include "sid_api.h"
#include "app_cli.h"
#include "app_cli_settings.h"

#include "sl_sidewalk_common_config.h"
#include "sl_malloc.h"

#if (defined(SL_FSK_SUPPORTED) || defined(SL_CSS_SUPPORTED))
#include "app_subghz_config.h"
#endif

#if defined(SL_BLE_SUPPORTED)
#include "app_ble_config.h"
#include "sl_bt_api.h"
#endif

#if defined(SL_CATALOG_SIMPLE_BUTTON_PRESENT)
#include "sl_simple_button_instances.h"
#endif

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------

// Maximum number Queue elements
#define MSG_QUEUE_LEN       (10U)
#define MSG_CLI_QUEUE_LEN   (3U)

// Unused function parameter
#define UNUSED(x) (void)(x)
// -----------------------------------------------------------------------------
//                          Static Function Declarations
// -----------------------------------------------------------------------------

/*******************************************************************************
 * Function to send an array of 0x31 with the length specified in `len`
 *
 * @param[in] app_context The context which is applicable for the current application
 * @param[in] message_type_str type of message to be sent: notify, get, set or response
 * @param[in] message_str content of the message to be sent
 * @param[in] link_type link type
 * @returns None
 ******************************************************************************/
static void send(app_context_t *app_context, char *message_type_str, char *message_str, char *link_type);

/*******************************************************************************
 * Method for sending sidewalk events
 *
 * @param[in] in_isr If the event shall be handled from ISR context
 * @param[in] context The context which is applicable for the current application
 * @returns None
 ******************************************************************************/
static void on_sidewalk_event(bool in_isr, void *context);

/*******************************************************************************
 * Callback Method for receiving sidewalk messages
 *
 * @param[in] msg_desc Message descriptor
 * @param[in] msg The received message
 * @param[in] context The context which is applicable for the current application
 * @returns None
 ******************************************************************************/
static void on_sidewalk_msg_received(const struct sid_msg_desc *msg_desc, const struct sid_msg *msg, void *context);

/*******************************************************************************
 * Callback method for the case when a sidewalk message is sent
 *
 * @param[in] msg_desc Message descriptor
 * @param[in] context The context which is applicable for the current application
 * @returns None
 ******************************************************************************/
static void on_sidewalk_msg_sent(const struct sid_msg_desc *msg_desc, void *context);

/*******************************************************************************
 * Callback function if error happened during send operation
 *
 * @param[in] error The error type
 * @param[in] msg_desc Message descriptor
 * @param[in] context The context which is applicable for the current application
 * @returns None
 ******************************************************************************/
static void on_sidewalk_send_error(sid_error_t error, const struct sid_msg_desc *msg_desc, void *context);

/*******************************************************************************
 * Callback Function to handle status changes in the Sidewalk context
 *
 * @param[in] status  new status
 * @param[in] context The context which is applicable for the current application
 * @returns None
 ******************************************************************************/
static void on_sidewalk_status_changed(const struct sid_status *status, void *context);

/*******************************************************************************
 * Callback function which is called from factory reset sidewalk event
 *
 * @param[in] context The context which is applicable for the current application
 * @returns None
 ******************************************************************************/
static void on_sidewalk_factory_reset(void *context);

/*******************************************************************************
 * Function to parse the message type
 *
 * @param[in] link_type_str The link type string
 * @returns Link mask:  0 if link is not available,
                        SID_LINK_TYPE_1 for ble,
                        SID_LINK_TYPE_2 for fsk,
                        SID_LINK_TYPE_3 for css
 ******************************************************************************/
static uint8_t parse_link_type(char *link_type_str);

/*******************************************************************************
 * Function to parse the message type
 *
 * @param[in] message_type The message type string
 * @returns Message type
 ******************************************************************************/
static int8_t parse_message_type(char *message_type);

/*******************************************************************************
 * Function to initialize sidewalk stack
 *
 * @param[in] app_context The context which is applicable for the current application
 * @param[in] link_str Link type string
 * @returns Status of the request
 ******************************************************************************/
static sl_status_t init_sidewalk(app_context_t *app_context, char *link_str);

/*******************************************************************************
 * Function to start sidewalk stack
 *
 * @param[in] app_context The context which is applicable for the current application
 * @param[in] link_str Link type string
 * @returns Status of the request
 ******************************************************************************/
static sl_status_t start_sidewalk(app_context_t *app_context, char *link_str);

/*******************************************************************************
 * Function to stop sidewalk stack
 *
 * @param[in] app_context The context which is applicable for the current application
 * @param[in] link_str Link type string
 * @returns Status of the request
 ******************************************************************************/
static sl_status_t stop_sidewalk(app_context_t *app_context, char *link_str);

/*******************************************************************************
 * Function to deinitialize sidewalk stack
 *
 * @param[in] app_context The context which is applicable for the current application
 * @param[in] link_str Link type string
 * @returns Status of the request
 ******************************************************************************/
static sl_status_t deinit_sidewalk(app_context_t *app_context);

/*******************************************************************************
 * Function to reset sidewalk stack
 *
 * @param[in] context The context which is applicable for the current application
 * @returns None
 ******************************************************************************/
static void reset_sidewalk(app_context_t *context);

/*******************************************************************************
 * Function to request connection
 *
 * @param[in] context The context which is applicable for the current application
 * @returns None
 ******************************************************************************/
static void connection_request(app_context_t *context);

// -----------------------------------------------------------------------------
//                          Public Functions Declarations
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
//                                Global Variables
// -----------------------------------------------------------------------------

// Link status (BLE, FSK, CSS)
link_status_t link_status;

// Last message received
struct sid_msg_desc LAST_MESSG_RCVD_DESC = { 0 };

// Queue for sending sidewalk events
QueueHandle_t g_event_queue;

// Currently initialized link
uint32_t current_init_link = 0;

// -----------------------------------------------------------------------------
//                                Static Variables
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
//                          Public Function Definitions
// -----------------------------------------------------------------------------

/*******************************************************************************
 * Main task
 *
 * @param[in] context The context which is applicable for the current application
 * @returns None
 ******************************************************************************/
void main_task(void *context)
{
  // Creating application context
  app_context_t *app_context = (app_context_t *)context;

  // Queue creation for the sidewalk events
  g_event_queue = xQueueCreate(MSG_QUEUE_LEN, sizeof(enum event_type));
  g_cli_event_queue = xQueueCreate(MSG_CLI_QUEUE_LEN, sizeof(app_setting_cli_queue_t));
  app_assert((g_event_queue != NULL) || (g_cli_event_queue != NULL), "app: queue creation failed");

  // Initialize to not ready state
  app_context->state = STATE_SIDEWALK_NOT_READY;

  // Assign queue to the application context
  app_context->event_queue = g_event_queue;

  while (1) {
    enum event_type event = EVENT_TYPE_INVALID;

    if (xQueueReceive(app_context->event_queue, &event, portMAX_DELAY) == pdTRUE) {
      switch (event) {
        case EVENT_TYPE_SIDEWALK:
          sid_process(app_context->sidewalk_handle);
          break;

        case EVENT_TYPE_SID_SEND:
          send(app_context, cli_arg_str, cli_arg_str_2, cli_arg_str_3);
          break;

        case EVENT_TYPE_SID_RESET:
          reset_sidewalk(app_context);
          break;

        case EVENT_TYPE_SID_INIT:
          if (init_sidewalk(app_context, cli_arg_str) != SL_STATUS_OK) {
            app_log_error("app: sid init failed\n");
          }
          break;

        case EVENT_TYPE_SID_START:
          if (start_sidewalk(app_context, cli_arg_str) != SL_STATUS_OK) {
            app_log_error("app: sid start failed\n");
          }
          break;

        case EVENT_TYPE_SID_STOP:
          if (stop_sidewalk(app_context, cli_arg_str) != SL_STATUS_OK) {
            app_log_error("app: sid stop failed\n");
          }
          break;

        case EVENT_TYPE_SID_DEINIT:
          if (deinit_sidewalk(app_context) != SL_STATUS_OK) {
            app_log_error("app: sid deinit failed\n");
          }
          break;

        case EVENT_TYPE_SID_GET_CSS_DEV_PROF_ID:
          get_sidewalk_css_dev_prof_id(app_context);
          break;

        case EVENT_TYPE_SID_SET_CSS_DEV_PROF_ID:
          set_sidewalk_css_dev_prof_id(app_context);
          break;

        case EVENT_TYPE_SID_GET_FSK_DEV_PROF_ID:
          get_sidewalk_fsk_dev_prof_id(app_context);
          break;

        case EVENT_TYPE_SID_SET_FSK_DEV_PROF_ID:
          set_sidewalk_fsk_dev_prof_id(app_context);
          break;

        case EVENT_TYPE_SID_SET_DEV_PROF_ID:
          set_sidewalk_dev_prof_id(app_context, cli_arg_uint8_t);
          break;

        case EVENT_TYPE_SID_GET_DEV_PROF_RX_WIN_CNT:
          get_sidewalk_dev_prof_rx_win_cnt(app_context);
          break;

        case EVENT_TYPE_SID_SET_DEV_PROF_RX_WIN_CNT:
          set_sidewalk_dev_prof_rx_win_cnt(app_context, (uint16_t)cli_arg_int16_t);
          break;

        case EVENT_TYPE_SID_GET_DEV_PROF_RX_INTERV_MS:
          get_sidewalk_dev_prof_rx_interv_ms(app_context);
          break;

        case EVENT_TYPE_SID_SET_DEV_PROF_RX_INTERV_MS:
          set_sidewalk_dev_prof_rx_interv_ms(app_context, cli_arg_uint16_t);
          break;

        case EVENT_TYPE_SID_GET_DEV_PROF_WAKEUP_TYPE:
          get_sidewalk_dev_prof_wakeup_type(app_context);
          break;

        case EVENT_TYPE_SID_SET_DEV_PROF_WAKEUP_TYPE:
          set_sidewalk_dev_prof_wakeup_type(app_context, cli_arg_uint8_t);
          break;

        case EVENT_TYPE_GET_TIME:
          get_sidewalk_time(app_context);
          break;

        case EVENT_TYPE_GET_STATUS:
          get_sidewalk_status(app_context);
          break;

        case EVENT_TYPE_GET_MTU_BLE:
          get_sidewalk_mtu(app_context, SID_LINK_TYPE_1);
          break;

        case EVENT_TYPE_GET_MTU_CSS:
          get_sidewalk_mtu(app_context, SID_LINK_TYPE_3);
          break;

        case EVENT_TYPE_GET_MTU_FSK:
          get_sidewalk_mtu(app_context, SID_LINK_TYPE_2);
          break;

        case EVENT_TYPE_GET_LINK_CONNECTION_POLICY:
          get_link_connection_policy(app_context);
          break;

        case EVENT_TYPE_SET_LINK_CONNECTION_POLICY:
          set_link_connection_policy(app_context, cli_arg_uint8_t);
          break;

        case EVENT_TYPE_GET_MULTI_LINK_POLICY:
          get_multi_link_policy(app_context);
          break;

        case EVENT_TYPE_SET_MULTI_LINK_POLICY:
          set_multi_link_policy(app_context, cli_arg_uint8_t);
          break;

        case EVENT_TYPE_GET_AUTO_CONNECT_PARAMS:
          get_auto_connect_params(app_context);
          break;

        case EVENT_TYPE_SET_AUTO_CONNECT_PARAMS:
          set_auto_connect_params(app_context, cli_arg_sid_link_auto_connect_params);
          break;

        case EVENT_TYPE_SID_BLE_CONNECTION_REQUEST:
          connection_request(app_context);
          break;

        default:
          app_log_error("app: unexpected evt: %d\n", (int)event);
          break;
      }
    }
  }

  // should never reach here
  app_log_error("app: fatal error\n");
  sid_platform_deinit();
  vTaskDelete(NULL);
}

/*******************************************************************************
 * Issue a queue event.
 *
 * @param[in] queue The queue handle which will be used ofr the event
 * @param[in] event The event to be sent
 * @returns None
 ******************************************************************************/
void queue_event(QueueHandle_t queue, enum event_type event)
{
  // Check if queue_event was called from ISR
  if ((bool)xPortIsInsideInterrupt()) {
    BaseType_t task_woken = pdFALSE;

    xQueueSendFromISR(queue, &event, &task_woken);
    portYIELD_FROM_ISR(task_woken);
  } else {
    xQueueSend(queue, &event, 0);
  }
}

#if defined(SL_CATALOG_SIMPLE_BUTTON_PRESENT)
/*******************************************************************************
 * Button handler callback
 * @param[in] handle button handler
 * @returns None
 ******************************************************************************/
void sl_button_on_change(const sl_button_t *handle)
{
  if (sl_button_get_state(handle) == SL_SIMPLE_BUTTON_PRESSED) {
    if (&sl_button_btn0 == handle) {
      // Button0 action
    } else if (&sl_button_btn1 == handle) {
      // Button1 action
    }
  }
}
#endif

// -----------------------------------------------------------------------------
//                          Static Function Definitions
// -----------------------------------------------------------------------------

static void on_sidewalk_event(bool in_isr, void *context)
{
  UNUSED(in_isr);
  app_context_t *app_context = (app_context_t *)context;
  // Issue sidewalk event to the queue
  queue_event(app_context->event_queue, EVENT_TYPE_SIDEWALK);
}

static void on_sidewalk_msg_received(const struct sid_msg_desc *msg_desc,
                                     const struct sid_msg *msg,
                                     void *context)
{
  UNUSED(context);
  // Copy last received message sturct before it is freed by the sidewalk stack.
  // Used for cli utility, to print rssi and snr of the last received message.
  memcpy(&LAST_MESSG_RCVD_DESC, msg_desc, sizeof(struct sid_msg_desc));

  app_log_info("app: rcvd msg (type: %d, id: %u, size: %u, snr: %d, rssi: %d)\n",
               msg_desc->type, msg_desc->id, msg->size, msg_desc->msg_desc_attr.rx_attr.snr, msg_desc->msg_desc_attr.rx_attr.rssi);
  app_log_info("app: %s\n", (char *) msg->data);
}

static void on_sidewalk_msg_sent(const struct sid_msg_desc *msg_desc, void *context)
{
  UNUSED(context);
  app_log_info("app: sent msg (type: %d, id: %u)\n", msg_desc->type, msg_desc->id);
}

static void on_sidewalk_send_error(sid_error_t error,
                                   const struct sid_msg_desc *msg_desc,
                                   void *context)
{
  UNUSED(context);
  app_log_error("app: msg send failed (type: %d, id: %u, err: %d)\n",
                msg_desc->type, msg_desc->id, error);
}

static void on_sidewalk_status_changed(const struct sid_status *status, void *context)
{
  app_context_t *app_context = (app_context_t *)context;

  switch (status->state) {
    case SID_STATE_READY:
      app_log_info("app: sid status changed to STATE_SIDEWALK_READY\n");
      app_context->state = STATE_SIDEWALK_READY;
      break;

    case SID_STATE_NOT_READY:
      app_log_info("app: sid status changed to STATE_SIDEWALK_NOT_READY\n");
      app_context->state = STATE_SIDEWALK_NOT_READY;
      break;

    case SID_STATE_ERROR:
      app_log_error("app: sid state err: %d\n", sid_get_error(app_context->sidewalk_handle));
      break;

    case SID_STATE_SECURE_CHANNEL_READY:
      app_log_info("app: sid status changed to STATE_SIDEWALK_SECURE_CONNECTION\n");
      app_context->state = STATE_SIDEWALK_SECURE_CONNECTION;
      break;

    default:
      // Invalid state, nothing to do
      break;
  }

  app_log_info("app: REG: %u, TIME: %u, LINK: %lu\n",
               status->detail.registration_status,
               status->detail.time_sync_status,
               status->detail.link_status_mask);

  app_context->link_status.link_mask = status->detail.link_status_mask;

  for (uint8_t i = 0; i < SID_LINK_TYPE_MAX_IDX; i++) {
    app_context->link_status.supported_link_mode[i] = status->detail.supported_link_modes[i];
    app_log_info("app: Link %d Mode %p\n", i, (void *) status->detail.supported_link_modes[i]);
  }
}

static void on_sidewalk_factory_reset(void *context)
{
  UNUSED(context);
  app_log_info("app: factory reset notif rcvd\n");
  // This is the callback function of the factory reset and as the last step a reset is applied.
  NVIC_SystemReset();
}

static uint8_t parse_link_type(char *link_type_str)
{
  uint8_t link_type = 0;

  if (strstr(link_type_str, "ble") != NULL) {
#if defined(SL_BLE_SUPPORTED)
    link_type |= SID_LINK_TYPE_1;
#endif
  }

  if (strstr(link_type_str, "fsk") != NULL) {
#if defined(SL_FSK_SUPPORTED)
    link_type |= SID_LINK_TYPE_2;
#endif
  }

  if (strstr(link_type_str, "css") != NULL) {
#if defined(SL_CSS_SUPPORTED)
    link_type |= SID_LINK_TYPE_3;
#endif
  }

  return link_type;
}

static int8_t parse_message_type(char *message_type)
{
  if (strstr(message_type, "notify") != NULL) {
    return SID_MSG_TYPE_NOTIFY;
  }

  if (strstr(message_type, "get") != NULL) {
    return SID_MSG_TYPE_GET;
  }

  if (strstr(message_type, "set") != NULL) {
    return SID_MSG_TYPE_SET;
  }

  if (strstr(message_type, "response") != NULL) {
    return SID_MSG_TYPE_RESPONSE;
  }

  return -1;
}

static void connection_request(app_context_t *context)
{
#if defined(SL_BLE_SUPPORTED)
  if (context->state == STATE_SIDEWALK_READY) {
    app_log_info("app: sid ready, operation invalid\n");
  } else {
    sid_error_t ret = sid_ble_bcn_connection_request(context->sidewalk_handle, true);
    if (ret == SID_ERROR_NONE) {
      app_log_info("app: conn req sent success\n");
      context->ble_connection_status = true;
    } else {
      app_log_error("app: conn req failed: %d\n", ret);
    }
  }
#else
  (void)context;
  app_log_error("app: Platform does not support BLE");
#endif
}

static void send(app_context_t *app_context, char *message_type_str, char *message_str, char *link_type)
{
  int8_t message_type = parse_message_type(message_type_str);

  if (message_type < 0) {
    app_log_error("app: wrong msg type: %s\n", message_type_str);
    return;
  }

  app_log_info("app: sending %s\n", message_str);

  struct sid_msg msg = {
    .data = (uint8_t *)message_str,
    .size = strlen(message_str)
  };
  struct sid_msg_desc desc = {
    .type = (uint8_t)message_type,
    .link_type = *link_type == 0 ? app_context->sid_cfg.link_mask : parse_link_type(link_type),
  };

  sid_error_t ret = sid_put_msg(app_context->sidewalk_handle, &msg, &desc);
  if (ret != SID_ERROR_NONE) {
    app_log_error("app: queueing data failed: %d\n", ret);
  } else {
    app_log_info("app: queued data msg id: %u\n", desc.id);
  }

  app_context->counter++;
}

static sl_status_t init_sidewalk(app_context_t *app_context, char *link_str)
{
  app_context->sid_event_cb = (struct sid_event_callbacks)
  {
    .context           = app_context,
    .on_event          = on_sidewalk_event,               // Called from ISR context
    .on_msg_received   = on_sidewalk_msg_received,        // Called from sid_process()
    .on_msg_sent       = on_sidewalk_msg_sent,            // Called from sid_process()
    .on_send_error     = on_sidewalk_send_error,          // Called from sid_process()
    .on_status_changed = on_sidewalk_status_changed,      // Called from sid_process()
    .on_factory_reset  = on_sidewalk_factory_reset,       // Called from sid_process()
  };

  app_context->sid_cfg = (struct sid_config)
  {
    .link_mask   = parse_link_type(link_str),
    .callbacks   = &app_context->sid_event_cb,
    .link_config = NULL,
    .sub_ghz_link_config = NULL
  };

  if (app_context->sid_cfg.link_mask == 0) {
    // Issued link is not available on current platform
    // Ignore command but don't return error as this will cause main task to be deleted.
    app_log_error("app: chosen link is not available on curr platform, ignore init cmd");
    return SL_STATUS_OK;
  }

#if (defined(SL_FSK_SUPPORTED) || defined(SL_CSS_SUPPORTED))
  app_context->sid_cfg.sub_ghz_link_config = app_get_sub_ghz_config();
#endif

#if defined(SL_BLE_SUPPORTED)
  app_context->sid_cfg.link_config = app_get_ble_config();
  app_context->ble_connection_status = false;
#endif

  struct sid_handle *tmp_sidewalk_handle;

  sid_error_t ret = sid_init(&app_context->sid_cfg, &tmp_sidewalk_handle);
  if (ret != SID_ERROR_NONE) {
    if ( ret != SID_ERROR_ALREADY_INITIALIZED) {
      // reset context sidewalk_handle
      app_context->sidewalk_handle = NULL;
      app_context->sid_cfg.link_mask = 0;
      app_log_error("app: sid init failed: %d\n", ret);
    } else {
      // here we want to preserve the context sidewalk_handle from previous initialized link
      app_log_error("app: sid already inited\n");
    }
    return SL_STATUS_FAIL;
  }
  app_log_info("app: sid init success\n");
  // update the  context sidewalk_handle with the one returned by sid_init
  app_context->sidewalk_handle = tmp_sidewalk_handle;
  current_init_link = app_context->sid_cfg.link_mask;

  return SL_STATUS_OK;
}

static sl_status_t start_sidewalk(app_context_t *app_context, char *link_str)
{
  uint8_t link_mask = parse_link_type(link_str);
  if (link_mask == 0) {
    // Issued link is not available on current platform
    // Ignore command but don't return error as this will cause main task to be deleted.
    app_log_error("app: chosen link is not available on curr platform, ignore start cmd");
    return SL_STATUS_OK;
  }

  sid_error_t ret = sid_start(app_context->sidewalk_handle, link_mask);
  if (ret != SID_ERROR_NONE) {
    app_log_error("app: start sid failed: %d\n", ret);
    app_context->sidewalk_handle = NULL;
    app_context->sid_cfg.link_mask = 0;

    return SL_STATUS_FAIL;
  } else {
    app_log_info("app: sid started\n");
  }

  return SL_STATUS_OK;
}

static sl_status_t stop_sidewalk(app_context_t *app_context, char *link_str)
{
  uint8_t link_mask = parse_link_type(link_str);
  if (link_mask == 0) {
    // Issued link is not available on current platform
    // Ignore the command but don't return error as this will cause main task to be deleted.
    app_log_error("app: chosen link is not available on curr platform, ignore stop cmd");
    return SL_STATUS_OK;
  }

  sid_error_t ret = sid_stop(app_context->sidewalk_handle, link_mask);
  if (ret != SID_ERROR_NONE) {
    app_log_error("app: sid stop failed: %d\n", ret);
    app_context->sidewalk_handle = NULL;
    app_context->sid_cfg.link_mask = 0;

    return SL_STATUS_FAIL;
  } else {
    app_log_info("app: sid stopped\n");
  }

  return SL_STATUS_OK;
}

static sl_status_t deinit_sidewalk(app_context_t *app_context)
{
  sid_error_t ret = sid_deinit(app_context->sidewalk_handle);
  if (ret != SID_ERROR_NONE) {
    app_log_error("app: sid deinit failed: %d\n", ret);
    app_context->sidewalk_handle = NULL;
    app_context->sid_cfg.link_mask = 0;

    return SL_STATUS_FAIL;
  } else {
    app_log_info("app: sid deinited\n");
  }

  return SL_STATUS_OK;
}

static void reset_sidewalk(app_context_t *context)
{
  // Check the state of the connection to sidewalk
  // Must be established for the API to send de-registration to cloud
  if (context->state == STATE_SIDEWALK_READY) {
    sid_error_t ret = sid_set_factory_reset(context->sidewalk_handle);
    if (ret != SID_ERROR_NONE) {
      app_log_error("app: factory reset notif failed: %d\n", ret);
      vTaskDelay(pdMS_TO_TICKS(200));
      NVIC_SystemReset();
    } else {
      app_log_info("app: wait to proceed with factory reset\n");
    }
  } else {
    app_log_error("app: Initiate/wait for curr conn to be successful before reseting sid\n");
  }
}
