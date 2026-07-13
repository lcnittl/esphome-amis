import logging
import re

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID

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


def obis_code(value):
    """Normalize an OBIS code to the strict A.B.C.D.E.F format.

    Accepts flexible notations like "1.8.0", "1-0:1.8.0" or "1.0.1.8.0.255".
    A 3-part code (C.D.E) is expanded with A=1 (electricity), B=0 and F=255.
    """
    value = cv.string(value)
    normalized = re.sub(r"[\-\:\*]", ".", value)
    parts = normalized.split(".")
    if len(parts) == 3:
        parts = ["1", "0", *parts, "255"]
    elif len(parts) == 5:
        parts.append("255")
    elif len(parts) != 6:
        raise cv.Invalid("OBIS code must have 3, 5 or 6 parts")
    try:
        bytes_list = [int(p) for p in parts]
    except ValueError as exc:
        raise cv.Invalid("OBIS code parts must be integers") from exc
    for b in bytes_list:
        if b < 0 or b > 255:
            raise cv.Invalid("OBIS code parts must be between 0 and 255")
    return ".".join(str(b) for b in bytes_list)


def _decryption_key(value):
    value = cv.string(value)
    if not re.fullmatch(r"[0-9a-fA-F]{32}", value):
        raise cv.Invalid(
            "Decryption key must be a 32 character hexadecimal string. "
            "Make sure to wrap the key in quotes."
        )
    return value.upper()


# mark the key as sensitive so frontends mask it (not available in older ESPHome)
decryption_key = (
    cv.sensitive(_decryption_key) if hasattr(cv, "sensitive") else _decryption_key
)


def _migrate_power_grid_key(config):
    if CONF_POWER_GRID_KEY in config:
        _LOGGER.warning(
            "The 'power_grid_key' option is deprecated, please use 'decryption_key' instead."
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
    "amis_meter", require_tx=True, require_rx=True
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    cg.add(var.set_decryption_key(config[CONF_DECRYPTION_KEY]))
    cg.add(var.set_receive_timeout(config[CONF_RECEIVE_TIMEOUT]))
