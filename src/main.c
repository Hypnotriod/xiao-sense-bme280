#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "battery_service.h"
#include "led_service.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define SLOW_DOWN_AD_RATE_AFTER_SEC 30

#define BT_LE_ADV_CONN_FAST \
    BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONN, BT_GAP_ADV_FAST_INT_MIN_2, BT_GAP_ADV_FAST_INT_MAX_2, NULL)

#define BT_LE_ADV_CONN_SLOW BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONN, BT_GAP_ADV_SLOW_INT_MIN, BT_GAP_ADV_SLOW_INT_MAX, NULL)

static const struct bt_data ad[] = {BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
                                    BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_BAS_VAL)),
                                    BT_DATA_BYTES(BT_DATA_UUID128_ALL, LED_SERVICE_UUID)};

static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static struct k_work_delayable slow_down_ad_rate_work;

static bool restart_adverisiment = false;

static void bt_ready(int err)
{
    if (err) {
        LOG_ERR("Bluetooth initialization failed (err %d)", err);
        return;
    }

    LOG_INF("Bluetooth initialized");

    err = bt_le_adv_start(BT_LE_ADV_CONN_FAST, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err) {
        LOG_ERR("Advertising failed to start (err %d)", err);
        return;
    }

    k_work_schedule(&slow_down_ad_rate_work, K_SECONDS(SLOW_DOWN_AD_RATE_AFTER_SEC));

    LOG_INF("Advertising successfully started");
}

static void bt_connected(struct bt_conn *conn, uint8_t err)
{
    char addr[BT_ADDR_LE_STR_LEN];

    k_work_cancel_delayable(&slow_down_ad_rate_work);

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    if (err) {
        LOG_ERR("Connection failed, err 0x%02x %s", err, bt_hci_err_to_str(err));
    } else {
        LOG_INF("Connected to %s", addr);
    }
}

static void bt_disconnected(struct bt_conn *conn, uint8_t reason)
{
    char addr[BT_ADDR_LE_STR_LEN];

    restart_adverisiment = true;

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("Disconnected from %s (reason 0x%02x)", addr, reason);
}

static void bt_recycled()
{
    if (!restart_adverisiment) {
        return;
    }
    restart_adverisiment = false;

    int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err) {
        LOG_ERR("Advertising failed to restart (err %d)", err);
        return;
    }

    k_work_schedule(&slow_down_ad_rate_work, K_SECONDS(SLOW_DOWN_AD_RATE_AFTER_SEC));

    LOG_INF("Advertising successfully restarted");
}

static void slow_down_ad_rate(struct k_work *work)
{
    int err;

    err = bt_le_adv_stop();
    if (err) {
        LOG_ERR("Advertising failed to stop (err %d)", err);
        return;
    }

    err = bt_le_adv_start(BT_LE_ADV_CONN_SLOW, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err) {
        LOG_ERR("Advertising failed start (err %d)", err);
        return;
    }

    LOG_INF("Advertising rate changed");
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = bt_connected,
    .disconnected = bt_disconnected,
    .recycled = bt_recycled,
};

int main(void)
{
    int err;

    k_work_init_delayable(&slow_down_ad_rate_work, slow_down_ad_rate);

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

    err = led_service_start();
    if (err) {
        LOG_ERR("Failed to start LED service (err %d)", err);
        return 0;
    }

    return 0;
}
