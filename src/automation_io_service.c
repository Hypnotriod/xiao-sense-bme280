#include "automation_io_service.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(automation_io_service, LOG_LEVEL_INF);

#define NUM_OF_DIGITALS 3

static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec led2 = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

static volatile uint8_t leds_state = 0x00;

static void update_led_state(const struct gpio_dt_spec *led, uint8_t index, bool is_on)
{
    int val = is_on > 0 ? 1 : 0;
    gpio_pin_set_dt(led, val);
    LOG_INF("LED%i is %s", index, is_on ? "On" : "Off");
}

static ssize_t write_do_state(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf, uint16_t len,
                              uint16_t offset, uint8_t flags)
{
    if (offset) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    } else if (len != 1) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    uint8_t state = *(uint8_t *)buf;
    leds_state = state;
    update_led_state(&led0, 0, state & 0x01);
    update_led_state(&led1, 1, state & 0x04);
    update_led_state(&led2, 2, state & 0x10);

    return len;
}

static ssize_t read_do_state(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len,
                             uint16_t offset)
{
    uint8_t state = leds_state;

    return bt_gatt_attr_read(conn, attr, buf, len, offset, &state, sizeof(state));
}

static ssize_t read_num_of_digitals(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len,
                                    uint16_t offset)
{
    uint8_t num = NUM_OF_DIGITALS;

    return bt_gatt_attr_read(conn, attr, buf, len, offset, &num, sizeof(num));
}

BT_GATT_SERVICE_DEFINE(automation_io_service, BT_GATT_PRIMARY_SERVICE(BT_UUID_AIOS),
                       BT_GATT_CHARACTERISTIC(BT_UUID_GATT_NUM_OF_DIGITALS, BT_GATT_CHRC_READ, BT_GATT_PERM_READ,
                                              read_num_of_digitals, NULL, NULL),
                       BT_GATT_CHARACTERISTIC(BT_UUID_GATT_DO, (BT_GATT_CHRC_WRITE | BT_GATT_CHRC_READ),
                                              (BT_GATT_PERM_WRITE | BT_GATT_PERM_READ), read_do_state, write_do_state,
                                              NULL), );

int automation_io_service_start(void)
{
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
