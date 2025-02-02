/***************************************************************************//**
 * @file
 * @brief app_gpio_config.h
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

#ifndef APP_GPIO_CONFIG_H
#define APP_GPIO_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

// -----------------------------------------------------------------------------
//                                   Includes
// -----------------------------------------------------------------------------

/******************************************************************************
   This config only supports Adaptor board Rev A02!
 *******************************************************************************/
#include "sl_spidrv_exp_config.h"

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------

// BUSY on PD03
// Used to indicate the status of internal state machine
#define SL_BUSY_PIN                              3
#define SL_BUSY_PORT                             gpioPortD

// ANT_SW on PA03
// External antenna switch to control antenna switch to RECEIVE or
// TRANSMIT.
#define SL_ANTSW_PIN                             3
#define SL_ANTSW_PORT                            gpioPortA

// DIO1 on PA00
// IRQ line from sx126x chip
// See sx126x datasheet for IRQs list.
#define SL_DIO_PIN                               0
#define SL_DIO_PORT                              gpioPortA

// SX NRESET on PD02
// Factory reset pin. Will be followed by standard calibration procedure
// and previous context will be lost.
#define SL_NRESET_PIN                            2
#define SL_NRESET_PORT                           gpioPortD

#define SL_SX_CS_PIN                             SL_SPIDRV_EXP_CS_PIN
#define SL_SX_CS_PORT                            SL_SPIDRV_EXP_CS_PORT

#define SL_PIN_BAND_SEL_PIN                      (4)
#define SL_PIN_BAND_SEL_PORT                     GPIO_PORTD

#ifdef __cplusplus
}
#endif

#endif /* APP_GPIO_CONFIG_H */
