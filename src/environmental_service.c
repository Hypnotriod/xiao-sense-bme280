#include "environmental_service.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor_data_types.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/rtio/rtio.h>

LOG_MODULE_REGISTER(environmental_service, LOG_LEVEL_INF);

#define SAMPLING_INTERVAL_MS 5000

const struct device *const bme280_dev = DEVICE_DT_GET(DT_NODELABEL(bme280_dev));

SENSOR_DT_READ_IODEV(bme280_iodev, DT_NODELABEL(bme280_dev), {SENSOR_CHAN_AMBIENT_TEMP, 0}, {SENSOR_CHAN_HUMIDITY, 0},
                     {SENSOR_CHAN_PRESS, 0});

RTIO_DEFINE(bme280_ctx, 1, 1);

const struct sensor_decoder_api *decoder;
static struct k_work_delayable sample_periodic_work;

static const struct bt_gatt_cpf temperature_att_format_cpf = {
    .format = 0x0E, /* sint16 */
    .exponent = -2,
    .unit = 0x272F,        /* Degree Celsius */
    .name_space = 0x01,    /* Bluetooth SIG */
    .description = 0x010C, /* "outside" */
};

static const struct bt_gatt_cpf pressure_att_format_cpf = {
    .format = 0x0E, /* sint16 */
    .exponent = 0,
    .unit = 0x2724,        /* Pascal (1 hPa = 100 Pa) */
    .name_space = 0x01,    /* Bluetooth SIG */
    .description = 0x010C, /* "outside" */
};

static const struct bt_gatt_cpf humidity_att_format_cpf = {
    .format = 0x0E, /* sint16 */
    .exponent = -2,
    .unit = 0x2A6F,        /* Humidity */
    .name_space = 0x01,    /* Bluetooth SIG */
    .description = 0x010C, /* "outside" */
};

static ssize_t read_temperature(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len,
                                uint16_t offset);
static ssize_t read_pressure(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len,
                             uint16_t offset);
static ssize_t read_humidity(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len,
                             uint16_t offset);
static void blvl_ccc_temperature_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value);
static void blvl_ccc_pressure_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value);
static void blvl_ccc_humidity_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value);

BT_GATT_SERVICE_DEFINE(ess_service, BT_GATT_PRIMARY_SERVICE(BT_UUID_ESS),
                       BT_GATT_CHARACTERISTIC(BT_UUID_TEMPERATURE, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                                              BT_GATT_PERM_READ, read_temperature, NULL, NULL),
                       BT_GATT_CCC(blvl_ccc_temperature_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
                       BT_GATT_CPF(&temperature_att_format_cpf),
                       BT_GATT_CHARACTERISTIC(BT_UUID_PRESSURE, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                                              BT_GATT_PERM_READ, read_pressure, NULL, NULL),
                       BT_GATT_CCC(blvl_ccc_pressure_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
                       BT_GATT_CPF(&pressure_att_format_cpf),
                       BT_GATT_CHARACTERISTIC(BT_UUID_HUMIDITY, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                                              BT_GATT_PERM_READ, read_humidity, NULL, NULL),
                       BT_GATT_CCC(blvl_ccc_humidity_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
                       BT_GATT_CPF(&humidity_att_format_cpf), );

static int16_t temperature = 0;
static int16_t pressure = 0;
static int16_t humidity = 0;

static int16_t sensor_q31_data_to_int16_attr(struct sensor_q31_data *data)
{
    float32_t v = data->readings[0].value;
    int8_t s = data->shift;

    return v / (1 << (31 - s)) * 100.f;
}

static void sample_periodic_handler(struct k_work *work)
{
    uint8_t buff[32];
    uint32_t temp_fit = 0;
    uint32_t press_fit = 0;
    uint32_t hum_fit = 0;
    struct sensor_q31_data temp_data = {0};
    struct sensor_q31_data press_data = {0};
    struct sensor_q31_data hum_data = {0};
    int err;

    err = sensor_read(&bme280_iodev, &bme280_ctx, buff, sizeof(buff));
    if (err) {
        LOG_ERR("Failed to read the sensor data (error %d)", err);
        goto reschedule;
    }

    err = sensor_get_decoder(bme280_dev, &decoder);
    if (err) {
        LOG_ERR("Failed to get the sensor's decoder API (error %d)", err);
        goto reschedule;
    }

    decoder->decode(buff, (struct sensor_chan_spec){SENSOR_CHAN_AMBIENT_TEMP, 0}, &temp_fit, 1, &temp_data);
    decoder->decode(buff, (struct sensor_chan_spec){SENSOR_CHAN_PRESS, 0}, &press_fit, 1, &press_data);
    decoder->decode(buff, (struct sensor_chan_spec){SENSOR_CHAN_HUMIDITY, 0}, &hum_fit, 1, &hum_data);

    temperature = sensor_q31_data_to_int16_attr(&temp_data);
    pressure = sensor_q31_data_to_int16_attr(&press_data);
    humidity = sensor_q31_data_to_int16_attr(&hum_data);

    bt_gatt_notify(NULL, &ess_service.attrs[2], &temperature, sizeof(temperature));
    bt_gatt_notify(NULL, &ess_service.attrs[6], &pressure, sizeof(pressure));
    bt_gatt_notify(NULL, &ess_service.attrs[10], &humidity, sizeof(humidity));

    LOG_INF("Temp: %s%d.%d DegC; Press: %s%d.%d hPa; Humidity: %s%d.%d %%RH",
            PRIq_arg(temp_data.readings[0].temperature, 2, temp_data.shift),
            PRIq_arg(press_data.readings[0].pressure, 2, press_data.shift),
            PRIq_arg(hum_data.readings[0].humidity, 2, hum_data.shift));

reschedule:
    k_work_reschedule(&sample_periodic_work, K_MSEC(SAMPLING_INTERVAL_MS));
}

static ssize_t read_temperature(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len,
                                uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &temperature, sizeof(temperature));
}

static ssize_t read_pressure(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len,
                             uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &pressure, sizeof(pressure));
}

static ssize_t read_humidity(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len,
                             uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &humidity, sizeof(humidity));
}

static void blvl_ccc_temperature_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    ARG_UNUSED(attr);
    bool enabled = (value == BT_GATT_CCC_NOTIFY);
    LOG_INF("Temperature notifications %s", enabled ? "enabled" : "disabled");
}

static void blvl_ccc_pressure_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    ARG_UNUSED(attr);
    bool enabled = (value == BT_GATT_CCC_NOTIFY);
    LOG_INF("Pressure notifications %s", enabled ? "enabled" : "disabled");
}

static void blvl_ccc_humidity_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    ARG_UNUSED(attr);
    bool enabled = (value == BT_GATT_CCC_NOTIFY);
    LOG_INF("Humidity notifications %s", enabled ? "enabled" : "disabled");
}

int environmental_service_start(void)
{
    if (bme280_dev == NULL) {
        LOG_ERR("No BME280 device found");
        return -ENXIO;
    }

    if (!device_is_ready(bme280_dev)) {
        LOG_ERR("BME280 device is not ready");
        return -EIO;
    }

    k_work_init_delayable(&sample_periodic_work, sample_periodic_handler);
    k_work_schedule(&sample_periodic_work, K_MSEC(SAMPLING_INTERVAL_MS));

    return 0;
}
