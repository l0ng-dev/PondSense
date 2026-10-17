/* generated configuration header file - do not edit */
#ifndef BSP_PIN_CFG_H_
#define BSP_PIN_CFG_H_
#include "r_ioport.h"

/* Common macro for FSP header files. There is also a corresponding FSP_FOOTER macro at the end of this file. */
FSP_HEADER

#define OLED_SCL (BSP_IO_PORT_00_PIN_00) /* OLED software I2C SCL, CN4 pin 6, open-drain; external 3.3V pull-up required */
#define OLED_SDA (BSP_IO_PORT_00_PIN_01) /* OLED software I2C SDA, CN4 pin 7, open-drain; external 3.3V pull-up required */
#define PH_PO (BSP_IO_PORT_00_PIN_14) /* pH module PO analog output to ADC0 AN12; verify input voltage range before use */
#define DS18B20_DAT (BSP_IO_PORT_00_PIN_15) /* DS18B20 1-Wire DAT; requires external pull-up to 3.3V */

extern const ioport_cfg_t g_bsp_pin_cfg; /* R7FA4M2AD3CFL.pincfg */

void BSP_PinConfigSecurityInit();

/* Common macro for FSP header files. There is also a corresponding FSP_HEADER macro at the top of this file. */
FSP_FOOTER
#endif /* BSP_PIN_CFG_H_ */
