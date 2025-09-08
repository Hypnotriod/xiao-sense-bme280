#ifndef __LED_SERVICE_H__
#define __LED_SERVICE_H__

#include <zephyr/bluetooth/bluetooth.h>

#define LED_SERVICE_UUID BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef0)
#define LED_CHARACTERISTIC_UUID BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef1)

int led_service_start(void);

#endif  //__LED_SERVICE_H__