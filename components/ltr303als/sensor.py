import esphome.codegen as cg
from esphome.components import i2c, sensor
import esphome.config_validation as cv
from esphome.const import DEVICE_CLASS_ILLUMINANCE, STATE_CLASS_MEASUREMENT, UNIT_LUX

DEPENDENCIES = ["i2c"]
CONF_WINDOW_CORRECTION = "window_correction"

ltr303als_ns = cg.esphome_ns.namespace("ltr303als")

LTR303ALSSensor = ltr303als_ns.class_(
    "LTR303ALSSensor", sensor.Sensor, cg.PollingComponent, i2c.I2CDevice
)

CONFIG_SCHEMA = (
    sensor.sensor_schema(
        LTR303ALSSensor,
        unit_of_measurement=UNIT_LUX,
        accuracy_decimals=1,
        device_class=DEVICE_CLASS_ILLUMINANCE,
        state_class=STATE_CLASS_MEASUREMENT,
    )
    .extend(cv.polling_component_schema("2s"))
    .extend(i2c.i2c_device_schema(0x29))
    .extend({cv.Optional(CONF_WINDOW_CORRECTION, default=1.0): cv.positive_float})
)


async def to_code(config):
    var = await sensor.new_sensor(config)
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
    cg.add(var.set_window_correction(config[CONF_WINDOW_CORRECTION]))
