import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import CONF_ID

ns = cg.esphome_ns.namespace("micronet_compass")
Compass = ns.class_("Compass", sensor.Sensor, cg.PollingComponent)
AXES = ("magnetic_x", "magnetic_y", "magnetic_z", "acceleration_x", "acceleration_y", "acceleration_z")
CONFIG_SCHEMA = sensor.sensor_schema(Compass, unit_of_measurement="rad", accuracy_decimals=4).extend({
    **{cv.Required(axis): cv.use_id(sensor.Sensor) for axis in AXES},
    cv.Optional("heading_offset", default=0): cv.float_,
    cv.Optional("stale_after", default="2s"): cv.positive_time_period_milliseconds,
}).extend(cv.polling_component_schema("500ms"))

async def to_code(config):
    var = await sensor.new_sensor(config)
    await cg.register_component(var, config)
    for index, axis in enumerate(AXES):
        source = await cg.get_variable(config[axis])
        cg.add(var.set_input(index, source))
    cg.add(var.set_heading_offset(config["heading_offset"]))
    cg.add(var.set_stale_after(config["stale_after"].total_milliseconds))
