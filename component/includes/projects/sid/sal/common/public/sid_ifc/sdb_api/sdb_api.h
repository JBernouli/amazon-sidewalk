/*
 * Copyright 2021 Amazon.com, Inc. or its affiliates. All rights reserved.
 *
 * AMAZON PROPRIETARY/CONFIDENTIAL
 *
 * You may not use this file except in compliance with the terms and
 * conditions set forth in the accompanying LICENSE.txt file.
 *
 * THESE MATERIALS ARE PROVIDED ON AN "AS IS" BASIS. AMAZON SPECIFICALLY
 * DISCLAIMS, WITH RESPECT TO THESE MATERIALS, ALL WARRANTIES, EXPRESS,
 * IMPLIED, OR STATUTORY, INCLUDING THE IMPLIED WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, AND NON-INFRINGEMENT.
 */

#ifndef SDB_API_H
#define SDB_API_H

/// @cond sid_ifc_internal_en

/** @file
 *
 * @defgroup SIDEBAND_API Sideband API
 * @brief API for communicating with the Sideband network
 * @include docs/sideband_architecture.md
 * @{
 * @ingroup  SIDEBAND_API
 */

#include <sid_error.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SDB_MSG_DESTINATION_AWS_IOT_CORE 33

/**
 * Describes a message payload.
 */
struct sdb_msg {
    /** Pointer to buffer to be sent or received */
    void *data;
    /** Size (in bytes) of the buffer pointed to by data */
    size_t size;
};

/**
 * Describes the link connection status with the gateway device
 */
enum sdb_link_status {
    /** Used to indicate the Sideband library is connected to a Sideband gateway */
    SDB_STATUS_LINK_UP = 0,

    /** Used when the Sideband library is disconnected from a Sideband gateway */
    SDB_STATUS_LINK_DOWN = 1,
};

/**
 * The Sideband library message types.
 * The messages from cloud services to the End device are designated as "Downlink Messages"
 * The messages from End device to Cloud services are designated as "Uplink Messages"
 */
enum sdb_msg_type {
    /** #SDB_MSG_TYPE_GET is used by the sender to retrieve information from the receiver, the sender
     * expects a mandatory response from the receiver. On reception of #SDB_MSG_TYPE_GET, the receiver
     * is expected to send a message with type #SDB_MSG_TYPE_RESPONSE with the same message id it received
     * from the message type #SDB_MSG_TYPE_GET.
     * This is to ensure the sender can map message type #SDB_MSG_TYPE_GET with the received #SDB_MSG_TYPE_RESPONSE.
     * Both uplink and downlink messages use this message type.
     * @see sdb_put_msg().
     * @see on_msg_received in #sdb_event_callbacks.
     */
    SDB_MSG_TYPE_GET = 0,
    /** #SDB_MSG_TYPE_SET indicates that the sender is expecting the receiver to take an action on receiving the
     * message and the sender does not expect a response.
     * #SDB_MSG_TYPE_SET type is used typically by Cloud services to trigger an action to be performed by the End device.
     * Typical users for this message type are downlink messages.
     */
    SDB_MSG_TYPE_SET = 1,
    /** #SDB_MSG_TYPE_NOTIFY is used to notify cloud services of any periodic events or events
     * triggered/originated from the device. Cloud services do not typically use #SDB_MSG_TYPE_NOTIFY as the
     * nature of messages from cloud services to the devices are explicit commands instead of notifications.
     * Typical users for this message type are uplink messages.
     */
    SDB_MSG_TYPE_NOTIFY = 2,
    /** #SDB_MSG_TYPE_RESPONSE is sent as a response to the message of type #SDB_MSG_TYPE_GET.
     * The sender of #SDB_MSG_TYPE_RESPONSE is required to copy the message id received in
     * the message of type #SDB_MSG_TYPE_GET.
     * Both uplink and downlink messages use this message type.
     * @see sdb_put_msg().
     * @see on_msg_received in #sdb_event_callbacks.
     */
    SDB_MSG_TYPE_RESPONSE = 3,
};

/**
 * A message descriptor given by the Sideband library to identify a message.
 */
struct sdb_msg_desc {
    /** The message type */
    enum sdb_msg_type type;
    /** The id associated with a message, generated by the Sideband library
     * The maximum value the id can take is 0x3FFF after which the id resets to 1
     */
    uint16_t id;
};

/**
 * Opaque handle returned by sid_init().
 */
struct sdb_handle;

/**
 * The set of callbacks a user can register through sdb_init().
 */
struct sdb_event_callbacks {
    /** A place where you can store user data */
    void *context;

    /**
     * Callback to invoke when any Sideband event occurs.
     *
     * The Sideband library invokes this callback when there is at least one event to process,
     * including internal events. Upon receiving this callback you are required to schedule a call
     * to sdb_process() within your main loop or running context.
     *
     * @warning sdb_process() MUST NOT be called from within the #on_event callback to avoid
     * re-entrancy and recursion problems.
     *
     * @see sdb_process
     *
     * @param[in] in_isr  true if invoked from within an ISR context, false otherwise.
     * @param[in] context The context pointer given in sdb_event_callbacks.context
     */
    void (*on_event)(bool in_isr, void *context);

    /**
     * Callback to invoke when a message from the Sideband network is received.
     *
     * @warning sdb_put_msg() MUST NOT be called from within the #on_msg_received callback
     * to avoid re-entrancy and recursion problems.
     *
     * @param[in] msg_desc A pointer to the received message descriptor, which is never NULL.
     * @param[in] msg      A pointer to the received message payload, which is never NULL.
     * @param[in] context  The context pointer given in sdb_event_callbacks.context
     */
    void (*on_msg_received)(const struct sdb_msg_desc *msg_desc, const struct sdb_msg *msg, void *context);

    /**
     * Callback to invoke when a message was successfully delivered to the Sideband network.
     *
     * @param[in] msg_desc A pointer to the sent message descriptor, which is never NULL.
     * @param[in] context  The context pointer given in sdb_event_callbacks.context
     */
    void (*on_msg_sent)(const struct sdb_msg_desc *msg_desc, void *context);

    /**
     * Callback to invoke when a queued message failed to be delivered to the Sideband network.
     *
     * A user can use this notification to schedule retrying sending a message or invoke other error
     * handling.
     *
     * @see sdb_put_msg
     *
     * @warning sdb_put_msg() MUST NOT be called from within the #on_send_error callback to
     * avoid re-entrancy and recursion problems.
     *
     * @param[in] error    The error code associated with the failure
     * @param[in] msg_desc A pointer to the unsent message descriptor, which is never NULL.
     * @param[in] context  The context pointer given in sdb_event_callbacks.context
     */
    void (*on_send_error)(sid_error_t error, const struct sdb_msg_desc *msg_desc, void *context);

    /**
     * Callback to invoke when the Sideband library status changes.
     *
     * Once sdb_start() is called, a #SID_STATE_READY status indicates the library is ready to
     * accept messages sdb_put_msg().
     *
     * When receiving #SID_STATE_ERROR, you can call sdb_get_error() from within the
     * #on_status_changed callback context to obtain more detail about the error condition.
     * Receiving this status means the Sideband library encountered a fatal condition and won't be
     * able to proceed. Hence, this notification is mostly for diagnostic purposes.
     *
     * @param[in] status  The current status, valid until the next invocation of this callback.
     * @param[in] context The context pointer given in sdb_event_callbacks.context
     */
    void (*on_status_changed)(const enum sdb_link_status *status, void *context);
};

/**
 * Describes the configuration associated with the chosen link.
 */
struct sdb_config {
    /** The event callbacks associated with the chosen link. */
    struct sdb_event_callbacks *event_callbacks;
    /** sdb configuration. */
    const void *config;
};

/**
 * Initializes the Sideband library for the chosen link type.
 *
 * sdb_init() can only be called once unless sdb_deinit() is called first.
 *
 * @see sdb_deinit
 *
 * @param[in] config  The configuration used to initialize Sideband.
 * @param[out] handle A pointer where the the opaque handle type will be stored. `handle` is set to
 *                    NULL on error.
 *
 * @returns #SID_ERROR_NONE                on success.
 * @returns #SID_ERROR_ALREADY_INITIALIZED if Sideband was already initialized for the given link
 *                                         type.
 */
sid_error_t sdb_init(const struct sdb_config *config, struct sdb_handle **handle);

/**
 * De-initialize the portions of the Sideband library associated with the given handle.
 *
 * @see sdb_init
 *
 * @param[in] handle A pointer to the handle returned by sdb_init()
 *
 * @returns #SID_ERROR_NONE in case of success
 */
sid_error_t sdb_deinit(struct sdb_handle *handle);

/**
 * Makes the Sideband library start operating.
 *
 * The notifications registered during sdb_init() are invoked once sdb_start() is called.
 *
 * @see sdb_stop
 *
 * @param[in] handle A pointer to the handle returned by sdb_init()
 *
 * @returns #SID_ERROR_NONE in case of success.
 */
sid_error_t sdb_start(struct sdb_handle *handle);

/**
 * Makes the Sideband library stop operating.
 *
 * No messages will be sent or received and no notifications will occur after sdb_stop() is called.
 * Link status will be changed to LINK_DOWN.
 *
 * @see sdb_start
 *
 * @warning sdb_stop() should be called in the same caller context as sdb_process().
 *
 * @warning sdb_stop() must not be called from within the caller context of any of the
 * sdb_event_callbacks registered during sdb_init() to avoid re-entrancy and recursion problems.
 *
 * @param[in] handle A pointer to the handle returned by sdb_init()
 *
 * @returns #SID_ERROR_NONE in case of success.
 */
sid_error_t sdb_stop(struct sdb_handle *handle);

/**
 * Process Sideband events.
 *
 * When there are no events to process, the function returns immediately.
 * When events are present, sdb_process() invokes the sdb_event_callbacks registered during
 * sdb_init() within its calling context. You may not receive any callbacks for internal events.
 *
 * You are required to schedule sdb_process() to run within your main-loop or running context when
 * the sdb_event_callbacks.on_event callback is received.
 *
 * Although not recommended for efficiency and power usage reasons, sdb_process() can also be called
 * even if sdb_event_callbacks.on_event has not been received, to support main loops that operate in
 * a polling manner.
 *
 * @warning sdb_process() must not be called from within the caller context of any of the
 * sdb_event_callbacks registered during sdb_init() to avoid re-entrancy and recursion problems.
 *
 * @see sdb_init
 * @see sdb_start
 * @see sdb_event_callbacks
 *
 * @param[in] handle A pointer to the handle returned by sdb_init()
 *
 * @returns #SID_ERROR_NONE in case of success.
 * @returns #SID_ERROR_STOPPED if sdb_start() has not been called.
 */
sid_error_t sdb_process(struct sdb_handle *handle);

/**
 * Queues a message.
 *
 * @note msg_desc can be used to correlate this message with the sdb_event_callbacks.on_msg_sent
 * and sdb_event_callbacks.on_send_error callbacks.
 *
 * @note When sending #SDB_MSG_TYPE_RESPONSE in response to #SID_MSG_TYPE_GET, the user is expected to fill
 * the id field of message descriptor with id from the corresponding #SDB_MSG_TYPE_GET message descriptor.
 * This allows the sdb_api to match each unique #SDB_MSG_TYPE_RESPONSE with #SDB_MSG_TYPE_GET.
 *
 * @param[in]  handle   A pointer to the handle returned by sdb_init()
 * @param[in]  msg      The message data to send
 * @param[out] msg_desc The message descriptor id this function fills which identifies this message.
 *                      Only valid when #SID_ERROR_NONE is returned.
 *
 * @returns #SID_ERROR_NONE when the message is successfully placed in the transmit queue.
 * @returns #SID_ERROR_TRY_AGAIN when there is no space in the transmit queue.
 */
sid_error_t sdb_put_msg(struct sdb_handle *handle, const struct sdb_msg *msg, struct sdb_msg_desc *msg_desc);

/**
 * Get the current error code.
 *
 * When the sdb_event_callbacks.on_status_changed callback is called with a #SID_STATE_ERROR
 * you can use this function to retrieve the detailed error code. The error code will only be valid
 * in the calling context of the sdb_event_callbacks.on_status_changed callback.
 *
 * @param[in] handle A pointer to the handle returned by sdb_init()
 *
 * @returns The current error code
 */
sid_error_t sdb_get_error(struct sdb_handle *handle);

/**
 * Set destination ID for messages.
 *
 * By default, the destination ID is set to the gateway to which the device is connected unless
 * changed by sdb_set_msg_dest_id(). The destination ID is retained until the device
 * resets or its changed by another invocation of sdb_set_msg_dest_id().
 *
 * @see sdb_put_msg().
 *
 * @param[in] handle A pointer to the handle returned by sdb_init().
 * @param[in] id     The new destination ID.
 *
 * @returns #SID_ERROR_NONE on success.
 */
sid_error_t sdb_set_msg_dest_id(struct sdb_handle *handle, uint32_t id);

/**
 * Get current status from Sideband library.
 *
 * @warning sdb_get_status() should be called in the same caller context as sdb_process().
 *
 * @warning sdb_get_status() must not be called from within the caller context of any of the
 * sdb_event_callbacks registered during sdb_init() to avoid re-entrancy and recursion problems.
 *
 * @param[in] handle A pointer to the handle returned by sdb_init()
 * @param[out] current_status A pointer to store the sdk current status
 *
 * @returns #SID_ERROR_NONE in case of success.
 * @returns #SID_ERROR_INVALID_ARGS when Sideband library is not initialized.
 */
sid_error_t sdb_get_status(struct sdb_handle *handle, enum sdb_link_status *current_status);

#ifdef __cplusplus
}
#endif

/** @} */

/// @endcond

#endif
