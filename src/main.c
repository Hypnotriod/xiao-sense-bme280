#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/hci_vs.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include "automation_io_service.h"
#include "battery_service.h"
#include "environmental_service.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define BT_CENTRAL_ADV_TX_POWER_LEVEL_DB 0
#define BT_CENTRAL_CONN_TX_POWER_LEVEL_DB 8
#define SLOW_DOWN_AD_RATE_AFTER_SEC 30

#define BT_LE_ADV_CONN_FAST \
    BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONN, BT_GAP_ADV_FAST_INT_MIN_2, BT_GAP_ADV_FAST_INT_MAX_2, NULL)

#define BT_LE_ADV_CONN_SLOW BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONN, BT_GAP_ADV_SLOW_INT_MIN, BT_GAP_ADV_SLOW_INT_MAX, NULL)

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_BAS_VAL), BT_UUID_16_ENCODE(BT_UUID_ESS_VAL),
                  BT_UUID_16_ENCODE(BT_UUID_AIOS_VAL)),
};

static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static struct k_work_delayable slow_down_ad_rate_work;

static bool restart_advertisement = false;

static int bt_set_tx_power(uint8_t handle_type, uint16_t handle, int8_t tx_pwr_lvl)
{
    struct bt_hci_cp_vs_write_tx_power_level* cp;
    struct bt_hci_rp_vs_write_tx_power_level* rp;
    struct net_buf *buf, *rsp = NULL;
    int err;

    buf = bt_hci_cmd_alloc(K_FOREVER);
    if (!buf) {
        LOG_ERR("Unable to allocate command buffer");
        return -ENOMEM;
    }

    cp = net_buf_add(buf, sizeof(*cp));
    cp->handle = sys_cpu_to_le16(handle);
    cp->handle_type = handle_type;
    cp->tx_power_level = tx_pwr_lvl;

    err = bt_hci_cmd_send_sync(BT_HCI_OP_VS_WRITE_TX_POWER_LEVEL, buf, &rsp);
    if (err) {
        LOG_ERR("Set Tx power err (err %d)", err);
        return err;
    }

    rp = (void*)rsp->data;
    LOG_INF("Actual Tx Power: %d", rp->selected_tx_power);

    net_buf_unref(rsp);

    return 0;
}

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

    err = bt_set_tx_power(BT_HCI_VS_LL_HANDLE_TYPE_ADV, 0, BT_CENTRAL_ADV_TX_POWER_LEVEL_DB);
    if (err) {
        LOG_INF("Unable to set adv TX power (err %d)", err);
        return;
    }

    k_work_schedule(&slow_down_ad_rate_work, K_SECONDS(SLOW_DOWN_AD_RATE_AFTER_SEC));

    LOG_INF("Advertising successfully started");
}

static void bt_connected(struct bt_conn* conn, uint8_t err)
{
    char addr[BT_ADDR_LE_STR_LEN];
    static uint16_t conn_handle;

    k_work_cancel_delayable(&slow_down_ad_rate_work);

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    if (err) {
        LOG_ERR("Connection failed, err 0x%02x %s", err, bt_hci_err_to_str(err));
    } else {
        LOG_INF("Connected to %s", addr);
    }

    err = bt_hci_get_conn_handle(conn, &conn_handle);
    if (err) {
        LOG_INF("Unable to get connection handle (err %d)", err);
        return;
    }

    err = bt_set_tx_power(BT_HCI_VS_LL_HANDLE_TYPE_CONN, conn_handle, BT_CENTRAL_CONN_TX_POWER_LEVEL_DB);
    if (err) {
        LOG_INF("Unable to set conn TX power (err %d)", err);
        return;
    }
}

static void bt_disconnected(struct bt_conn* conn, uint8_t reason)
{
    char addr[BT_ADDR_LE_STR_LEN];

    restart_advertisement = true;

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("Disconnected from %s (reason 0x%02x)", addr, reason);
}

static void bt_recycled()
{
    int err;

    if (!restart_advertisement) {
        return;
    }
    restart_advertisement = false;

    err = bt_le_adv_start(BT_LE_ADV_CONN_FAST, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err) {
        LOG_ERR("Advertising failed to restart (err %d)", err);
        return;
    }

    err = bt_set_tx_power(BT_HCI_VS_LL_HANDLE_TYPE_ADV, 0, BT_CENTRAL_ADV_TX_POWER_LEVEL_DB);
    if (err) {
        LOG_INF("Unable to set adv TX power (err %d)", err);
        return;
    }

    k_work_schedule(&slow_down_ad_rate_work, K_SECONDS(SLOW_DOWN_AD_RATE_AFTER_SEC));

    LOG_INF("Advertising successfully restarted");
}

static void slow_down_ad_rate(struct k_work* work)
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

    err = bt_set_tx_power(BT_HCI_VS_LL_HANDLE_TYPE_ADV, 0, BT_CENTRAL_ADV_TX_POWER_LEVEL_DB);
    if (err) {
        LOG_INF("Unable to set adv TX power (err %d)", err);
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

    err = environmental_service_start();
    if (err) {
        LOG_ERR("Failed to start Environmental Sensing Service (error %d)", err);
        return 0;
    }

    err = battery_service_start();
    if (err) {
        LOG_ERR("Failed to start Battery Service (error %d)", err);
        return 0;
    }

    err = automation_io_service_start();
    if (err) {
        LOG_ERR("Failed to start LED Service (err %d)", err);
        return 0;
    }

    return 0;
}
