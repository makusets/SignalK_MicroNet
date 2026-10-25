import esphome.codegen as cg
from esphome.components import i2c, sensor
import esphome.config_validation as cv
from esphome.const import CONF_ID
DEPENDENCIES = ["i2c"]
ns = cg.esphome_ns.namespace("lsm303dlhc")
LSM303DLHC = ns.class_("LSM303DLHC", cg.PollingComponent, i2c.I2CDevice)
AXES = ("magnetic_x", "magnetic_y", "magnetic_z", "acceleration_x", "acceleration_y", "acceleration_z")
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(LSM303DLHC),
    **{cv.Optional(axis): sensor.sensor_schema(unit_of_measurement="µT" if axis.startswith("magnetic") else "m/s²", accuracy_decimals=3) for axis in AXES},
}).extend(cv.polling_component_schema("200ms")).extend(i2c.i2c_device_schema(0x1E))

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
    for index, axis in enumerate(AXES):
        if axis in config:
            sens = await sensor.new_sensor(config[axis])
            cg.add(var.set_sensor(index, sens))
