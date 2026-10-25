import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from . import Micronet, micronet_ns

DEPENDENCIES = ["micronet"]
Measurement = micronet_ns.enum("Measurement", is_class=True)
TYPES = {'water_speed': ('WATER_SPEED', 'm/s', 3), 'depth': ('DEPTH', 'm', 2), 'water_temperature': ('WATER_TEMPERATURE', 'K', 2), 'wind_speed': ('WIND_SPEED', 'm/s', 3), 'wind_angle': ('WIND_ANGLE', 'rad', 4), 'heading': ('HEADING', 'rad', 4), 'speed_over_ground': ('SOG', 'm/s', 3), 'course_over_ground': ('COG', 'rad', 4), 'trip': ('TRIP', 'm', 1), 'log': ('LOG', 'm', 1), 'voltage': ('VOLTAGE', 'V', 1)}
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID("micronet_id"): cv.use_id(Micronet),
    **{cv.Optional(key): sensor.sensor_schema(unit_of_measurement=unit, accuracy_decimals=decimals)
       for key, (_, unit, decimals) in TYPES.items()},
})

async def to_code(config):
    parent = await cg.get_variable(config["micronet_id"])
    for key, (kind, _, _) in TYPES.items():
        if key in config:
            sens = await sensor.new_sensor(config[key])
            cg.add(parent.set_sensor(getattr(Measurement, kind), sens))
