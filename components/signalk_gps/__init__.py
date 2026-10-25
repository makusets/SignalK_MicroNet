import esphome.codegen as cg
from esphome.components.gps import GPS
from esphome.components.signalk import signalk
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["gps", "signalk"]
ns = cg.esphome_ns.namespace("signalk_gps")
SignalKGPS = ns.class_("SignalKGPS", cg.PollingComponent)
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(SignalKGPS),
    cv.GenerateID("gps_id"): cv.use_id(GPS),
    cv.GenerateID("signalk_id"): cv.use_id(signalk),
    cv.Optional("stale_after", default="5s"): cv.positive_time_period_milliseconds,
}).extend(cv.polling_component_schema("1s"))

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_gps(await cg.get_variable(config["gps_id"])))
    cg.add(var.set_signalk(await cg.get_variable(config["signalk_id"])))
    cg.add(var.set_stale_after(config["stale_after"].total_milliseconds))
