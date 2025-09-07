#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "battery_service.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_BAS_VAL)),
};

static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void bt_ready(int err) {
    if (err) {
        LOG_ERR("Bluetooth initialization failed (err %d)", err);
        return;
    }

    LOG_INF("Bluetooth initialized");

    err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err) {
        LOG_ERR("Advertising failed to start (err %d)", err);
        return;
    }

    LOG_INF("Advertising successfully started");
}

static void bt_connected(struct bt_conn *conn, uint8_t err) {
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    if (err) {
        LOG_ERR("Connection failed, err 0x%02x %s", err, bt_hci_err_to_str(err));
    } else {
        LOG_INF("Connected to %s", addr);
    }
}

static void bt_disconnected(struct bt_conn *conn, uint8_t reason) {
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("Disconnected from %s (reason 0x%02x)", addr, reason);
}

static void bt_recycled() {
    int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err) {
        LOG_ERR("Advertising failed to restart (err %d)", err);
        return;
    }

    LOG_INF("Advertising successfully restarted");
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = bt_connected,
    .disconnected = bt_disconnected,
    .recycled = bt_recycled,
};

int main(void) {
    int err = 0;

    err = bt_enable(bt_ready);
    if (err) {
        LOG_ERR("Bluetooth initialization failed (err %d)", err);
        return 0;
    }

    err = battery_service_start();
    if (err) {
        LOG_ERR("Failed to start battery service (error %d)", err);
        return 0;
    }

    return 0;
}
