import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor

from . import (
    CONF_AMIS_METER_ID,
    CONF_OBIS_CODE,
    AmisMeterComponent,
    obis_string_to_byte_list,
    register_text_sensor,
)

DEPENDENCIES = ["amis_meter"]

CONFIG_SCHEMA = text_sensor.text_sensor_schema().extend(
    {
        cv.GenerateID(CONF_AMIS_METER_ID): cv.use_id(AmisMeterComponent),
        cv.Required(CONF_OBIS_CODE): obis_string_to_byte_list,
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_AMIS_METER_ID])
    var = await text_sensor.new_text_sensor(config)
    register_text_sensor(hub, config[CONF_OBIS_CODE], var)
