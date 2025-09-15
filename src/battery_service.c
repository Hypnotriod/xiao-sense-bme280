#include "battery_service.h"

#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/logging/log.h>

#include "battery.h"

LOG_MODULE_REGISTER(battery_service, LOG_LEVEL_INF);

static volatile bool is_charging = false;
static volatile uint16_t last_millivolt = 0;

static void battery_voltage_update(uint16_t millivolt)
{
    uint8_t percentage = 0;

    if (is_charging && millivolt < last_millivolt) {
        millivolt = last_millivolt;
    } else if (!is_charging && millivolt > last_millivolt) {
        millivolt = last_millivolt;
    }
    last_millivolt = millivolt;

    int err = battery_get_percentage(&percentage, millivolt);
    if (err) {
        LOG_ERR("Failed to calculate battery percentage");
        return;
    }

    LOG_INF("Battery is at %d mV (capacity %d%%, %s)", millivolt, percentage, is_charging ? "charging" : "discharging");

    bt_bas_set_battery_level(percentage);
}

static void battery_charging_state_update(bool connected)
{
    is_charging = connected;
    LOG_INF("Charger %s", connected ? "connected" : "disconnected");
}

int battery_service_start(void)
{
    uint16_t battery_millivolt;
    int err;

    err = battery_init();
    if (err) {
        LOG_ERR("Failed to initialize battery management (error %d)", err);
        return err;
    }

    err = battery_register_charging_callback(battery_charging_state_update);
    if (err) {
        LOG_ERR("Failed to register charging callback (error %d)", err);
        return err;
    }

    err = battery_register_sample_callback(battery_voltage_update);
    if (err) {
        LOG_ERR("Failed to register sample callback (error %d)", err);
        return err;
    }

    err = battery_set_fast_charge();
    if (err) {
        LOG_ERR("Failed to set battery fast charging (error %d)", err);
        return err;
    }

    is_charging = battery_is_charging();
    battery_get_millivolt(&battery_millivolt);
    last_millivolt = battery_millivolt;
    battery_voltage_update(battery_millivolt);

    battery_start_sampling(5000);

    return 0;
}