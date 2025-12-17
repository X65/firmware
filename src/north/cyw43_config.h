#ifndef _CYW43_CONFIG_H
#define _CYW43_CONFIG_H

#include <cyw43_configport.h>

#define CYW43_RESOURCE_ATTRIBUTE    __attribute__((aligned(4))) __in_flash("cyw43firmware")
#define CYW43_PIO_CLOCK_DIV_DYNAMIC 1

#define CYW43_DEFAULT_PIN_WL_REG_ON    34
#define CYW43_DEFAULT_PIN_WL_DATA_OUT  36
#define CYW43_DEFAULT_PIN_WL_DATA_IN   36
#define CYW43_DEFAULT_PIN_WL_HOST_WAKE 36
#define CYW43_DEFAULT_PIN_WL_CLOCK     29
#define CYW43_DEFAULT_PIN_WL_CS        25
#define CYW43_WL_GPIO_COUNT            3
#define CYW43_WL_GPIO_LED_PIN          0

#endif
