import logging
import re

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.components.dlms_meter import obis_string_to_byte_list
from esphome.const import CONF_ID
from esphome.cpp_generator import MockObj

_LOGGER = logging.getLogger(__name__)

CODEOWNERS = ["@lcnittl"]
DEPENDENCIES = ["uart"]
MULTI_CONF = True

CONF_AMIS_METER_ID = "amis_meter_id"
CONF_DECRYPTION_KEY = "decryption_key"
CONF_POWER_GRID_KEY = "power_grid_key"  # deprecated alias for decryption_key
CONF_OBIS_CODE = "obis_code"
CONF_RECEIVE_TIMEOUT = "receive_timeout"

amis_meter_ns = cg.esphome_ns.namespace("amis_meter")
AmisMeterComponent = amis_meter_ns.class_(
    "AmisMeterComponent", cg.Component, uart.UARTDevice
)


def register_sensor(hub: MockObj, obis: list[int], var: MockObj) -> None:
    cg.add(hub.register_sensor(".".join(str(part) for part in obis), var))


def register_text_sensor(hub: MockObj, obis: list[int], var: MockObj) -> None:
    cg.add(hub.register_text_sensor(".".join(str(part) for part in obis), var))


def _decryption_key(value):
    value = cv.string(value)
    if not re.fullmatch(r"[0-9a-fA-F]{32}", value):
        raise cv.Invalid(
            "Decryption key must be a 32 character hexadecimal string. "
            "Make sure to wrap the key in quotes."
        )
    return value.upper()


# mark the key as sensitive so frontends mask it
# (cv.sensitive is not available in older ESPHome versions)
if hasattr(cv, "sensitive"):
    decryption_key = cv.sensitive(_decryption_key)
else:
    decryption_key = _decryption_key


def _migrate_power_grid_key(config):
    if CONF_POWER_GRID_KEY in config:
        _LOGGER.warning(
            "The 'power_grid_key' option is deprecated, "
            "please use 'decryption_key' instead."
        )
        config = config.copy()
        config[CONF_DECRYPTION_KEY] = config.pop(CONF_POWER_GRID_KEY)
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(AmisMeterComponent),
            cv.Optional(CONF_DECRYPTION_KEY): decryption_key,
            cv.Optional(CONF_POWER_GRID_KEY): decryption_key,
            cv.Optional(
                CONF_RECEIVE_TIMEOUT, default="1000ms"
            ): cv.positive_time_period_milliseconds,
        }
    )
    .extend(uart.UART_DEVICE_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA),
    cv.has_exactly_one_key(CONF_DECRYPTION_KEY, CONF_POWER_GRID_KEY),
    _migrate_power_grid_key,
)

FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "amis_meter",
    baud_rate=9600,
    require_tx=True,
    require_rx=True,
    data_bits=8,
    parity="EVEN",
    stop_bits=1,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    cg.add(var.set_decryption_key(config[CONF_DECRYPTION_KEY]))
    cg.add(var.set_receive_timeout(config[CONF_RECEIVE_TIMEOUT]))
