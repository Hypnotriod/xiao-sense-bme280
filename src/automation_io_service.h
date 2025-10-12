#ifndef __AIOS_SERVICE_H__
#define __AIOS_SERVICE_H__

int automation_io_service_start(void);

#include <zephyr/bluetooth/bluetooth.h>

// GATT Descriptor Number of Digitals UUID Value
#define BT_UUID_GATT_NUM_OF_DIGITALS_VAL 0x2909
// GATT Descriptor Number of Digitals
#define BT_UUID_GATT_NUM_OF_DIGITALS BT_UUID_DECLARE_16(BT_UUID_GATT_NUM_OF_DIGITALS_VAL)

#endif  //__AIOS_SERVICE_H__