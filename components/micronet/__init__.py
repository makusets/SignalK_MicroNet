import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

AUTO_LOAD = ["sensor"]
MULTI_CONF = True
micronet_ns = cg.esphome_ns.namespace("micronet")
Micronet = micronet_ns.class_("Micronet", cg.Component)
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(Micronet),
    cv.Optional("network_id"): cv.hex_uint32_t,
    cv.Optional("device_id"): cv.hex_uint32_t,
    cv.Optional("device_type"): cv.hex_uint8_t,
    cv.Optional("stale_after", default="10s"): cv.positive_time_period_milliseconds,
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    for key in ("network_id", "device_id", "device_type"):
        if key in config:
            cg.add(getattr(var, "set_" + key)(config[key]))
    cg.add(var.set_stale_after(config["stale_after"].total_milliseconds))
