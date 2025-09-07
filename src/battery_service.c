#include "battery_service.h"

#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/logging/log.h>

#include "battery.h"

LOG_MODULE_REGISTER(battery_service, LOG_LEVEL_INF);

static void battery_voltage_update(uint16_t millivolt) {
    uint8_t percentage = 0;

    int err = battery_get_percentage(&percentage, millivolt);
    if (err) {
        LOG_ERR("Failed to calculate battery percentage");
        return;
    }

    LOG_INF("Battery is at %d mV (capacity %d%%)", millivolt, percentage);

    bt_bas_set_battery_level(percentage);
}

static void battery_charging_state_update(bool is_charging) {
    LOG_INF("Charger %s", is_charging ? "connected" : "disconnected");
}

int battery_service_start(void) {
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

    battery_start_sampling(5000);

    return 0;
}