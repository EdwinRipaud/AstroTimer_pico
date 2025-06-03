#ifndef TIMER_CORE_H
#define TIMER_CORE_H

#include <FreeRTOS.h>
#include <task.h>

#include <pico/stdlib.h>

#define SHUTTER_PIN CYW43_WL_GPIO_LED_PIN // FIXME: replace by physical PIN

void timer_core(uint32_t nbPicture, uint32_t expTime, uint32_t delayTime);

#endif
