import esphome.codegen as cg
from esphome.components import binary_sensor, button, text_sensor
from esphome.components.micronet import Micronet
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.const import CONF_ID

DEPENDENCIES = ["micronet", "esp32"]
AUTO_LOAD = ["button", "binary_sensor", "text_sensor"]
ns = cg.esphome_ns.namespace("micronet_master")
Master = ns.class_("Master", cg.Component)
PairButton = ns.class_("PairButton", button.Button)
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(Master),
    cv.GenerateID("micronet_id"): cv.use_id(Micronet),
    cv.Optional("network_id"): cv.hex_uint32_t,
    cv.Optional("pair", default={"name": "Pair wind sensor"}): button.button_schema(PairButton),
    cv.Optional("paired", default={"name": "Wind sensor paired"}): binary_sensor.binary_sensor_schema(),
    cv.Optional("online", default={"name": "Wind sensor online"}): binary_sensor.binary_sensor_schema(device_class="connectivity"),
    cv.Optional("pairing", default={"name": "Wind sensor pairing"}): binary_sensor.binary_sensor_schema(),
    cv.Optional("status", default={"name": "Micronet status"}): text_sensor.text_sensor_schema(),
    cv.Optional("wind_id", default={"name": "Wind sensor ID"}): text_sensor.text_sensor_schema(),
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_hub(await cg.get_variable(config["micronet_id"])))
    if "network_id" in config:
        cg.add(var.set_network_id(config["network_id"]))
    pair = await button.new_button(config["pair"])
    cg.add(pair.set_parent(var))
    for key in ("paired", "online", "pairing"):
        entity = await binary_sensor.new_binary_sensor(config[key])
        cg.add(getattr(var, "set_" + key)(entity))
    for key in ("status", "wind_id"):
        entity = await text_sensor.new_text_sensor(config[key])
        cg.add(getattr(var, "set_" + key)(entity))


def final_validate(config):
    radio = fv.full_config.get().get("micronet_cc1101")
    if radio is not None and radio.get("micronet_id") == config["micronet_id"] and "gdo0_pin" not in radio:
        raise cv.Invalid("Standalone CC1101 master requires gdo0_pin for hardware packet timing")
    return config


FINAL_VALIDATE_SCHEMA = final_validate
