from esphome import pins
import esphome.codegen as cg
from esphome.components import spi
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.components.micronet import Micronet

DEPENDENCIES = ["spi", "micronet"]
ns = cg.esphome_ns.namespace("micronet_cc1101")
CC1101 = ns.class_("MicronetCC1101", cg.Component, spi.SPIDevice)
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(CC1101),
    cv.GenerateID("micronet_id"): cv.use_id(Micronet),
    cv.Optional("gdo0_pin"): pins.internal_gpio_input_pin_schema,
    cv.Required("frequency"): cv.one_of(869.84, 915.915, float=True),
}).extend(cv.COMPONENT_SCHEMA).extend(spi.spi_device_schema(cs_pin_required=True))

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await spi.register_spi_device(var, config)
    parent = await cg.get_variable(config["micronet_id"])
    cg.add(var.set_parent(parent))
    cg.add(var.set_frequency(config["frequency"]))
    if "gdo0_pin" in config:
        cg.add(var.set_gdo0_pin(await cg.gpio_pin_expression(config["gdo0_pin"])))
        cg.add(parent.set_transport(var))
