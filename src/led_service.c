#include "led_service.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(led_service, LOG_LEVEL_INF);

static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec led2 = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

static ssize_t write_led_state(const struct gpio_dt_spec *led, uint8_t index, struct bt_conn *conn,
                               const struct bt_gatt_attr *attr, const void *buf, uint16_t len, uint16_t offset,
                               uint8_t flags) {
    if (len != 1 || offset != 0) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    uint8_t val = *(uint8_t *)buf;
    if (val == 0) {
        gpio_pin_set_dt(led, 0);
        LOG_INF("LED%i is Off", index);
    } else {
        gpio_pin_set_dt(led, 1);
        LOG_INF("LED%i is On", index);
    }

    return len;
}

static ssize_t write_led0_state(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf, uint16_t len,
                                uint16_t offset, uint8_t flags) {
    return write_led_state(&led0, 0, conn, attr, buf, len, offset, flags);
}

static ssize_t write_led1_state(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf, uint16_t len,
                                uint16_t offset, uint8_t flags) {
    return write_led_state(&led1, 1, conn, attr, buf, len, offset, flags);
}

static ssize_t write_led2_state(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf, uint16_t len,
                                uint16_t offset, uint8_t flags) {
    return write_led_state(&led2, 2, conn, attr, buf, len, offset, flags);
}

BT_GATT_SERVICE_DEFINE(led_service, BT_GATT_PRIMARY_SERVICE(BT_UUID_DECLARE_128(LED_SERVICE_UUID)),
                       BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_128(LED0_CHARACTERISTIC_UUID), BT_GATT_CHRC_WRITE,
                                              BT_GATT_PERM_WRITE, NULL, write_led0_state, NULL),
                       BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_128(LED1_CHARACTERISTIC_UUID), BT_GATT_CHRC_WRITE,
                                              BT_GATT_PERM_WRITE, NULL, write_led1_state, NULL),
                       BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_128(LED2_CHARACTERISTIC_UUID), BT_GATT_CHRC_WRITE,
                                              BT_GATT_PERM_WRITE, NULL, write_led2_state, NULL), );

int led_service_start(void) {
    int err;

    err = gpio_pin_configure_dt(&led0, GPIO_OUTPUT_INACTIVE);
    if (err) {
        LOG_ERR("LED 0 pin configure failed (err %d)", err);
        return err;
    }

    err = gpio_pin_configure_dt(&led1, GPIO_OUTPUT_INACTIVE);
    if (err) {
        LOG_ERR("LED 1 pin configure failed (err %d)", err);
        return err;
    }

    err = gpio_pin_configure_dt(&led2, GPIO_OUTPUT_INACTIVE);
    if (err) {
        LOG_ERR("LED 2 pin configure failed (err %d)", err);
        return err;
    }

    return 0;
}
