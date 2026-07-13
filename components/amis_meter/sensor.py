import logging

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    DEVICE_CLASS_ENERGY,
    DEVICE_CLASS_POWER,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_VOLT_AMPS_REACTIVE,
    UNIT_VOLT_AMPS_REACTIVE_HOURS,
    UNIT_WATT,
    UNIT_WATT_HOURS,
)

from . import CONF_AMIS_METER_ID, CONF_OBIS_CODE, AmisMeterComponent, obis_code

_LOGGER = logging.getLogger(__name__)

DEPENDENCIES = ["amis_meter"]

# Mapping of the deprecated named sensor keys to their OBIS codes
NUMERIC_KEYS = {
    "energy_a_positive": "1.0.1.8.0.255",
    "energy_a_negative": "1.0.2.8.0.255",
    "reactive_energy_a_positive": "1.0.3.8.1.255",
    "reactive_energy_a_negative": "1.0.4.8.1.255",
    "instantaneous_power_a_positive": "1.0.1.7.0.255",
    "instantaneous_power_a_negative": "1.0.2.7.0.255",
    "reactive_instantaneous_power_a_positive": "1.0.3.7.0.255",
    "reactive_instantaneous_power_a_negative": "1.0.4.7.0.255",
}

DYNAMIC_SCHEMA = sensor.sensor_schema().extend(
    {
        cv.GenerateID(CONF_AMIS_METER_ID): cv.use_id(AmisMeterComponent),
        cv.Required(CONF_OBIS_CODE): obis_code,
    }
)


def deprecation_warning(config):
    _LOGGER.warning(
        "The amis_meter sensor schema using predefined keys (e.g., 'energy_a_positive') is deprecated. "
        "Please update your configuration to use the new schema with 'obis_code'."
    )
    return config


OLD_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(CONF_AMIS_METER_ID): cv.use_id(AmisMeterComponent),
            cv.Optional("energy_a_positive"): sensor.sensor_schema(
                unit_of_measurement=UNIT_WATT_HOURS,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_ENERGY,
                state_class=STATE_CLASS_TOTAL_INCREASING,
            ),
            cv.Optional("energy_a_negative"): sensor.sensor_schema(
                unit_of_measurement=UNIT_WATT_HOURS,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_ENERGY,
                state_class=STATE_CLASS_TOTAL_INCREASING,
            ),
            cv.Optional("reactive_energy_a_positive"): sensor.sensor_schema(
                unit_of_measurement=UNIT_VOLT_AMPS_REACTIVE_HOURS,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_ENERGY,
                state_class=STATE_CLASS_TOTAL_INCREASING,
            ),
            cv.Optional("reactive_energy_a_negative"): sensor.sensor_schema(
                unit_of_measurement=UNIT_VOLT_AMPS_REACTIVE_HOURS,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_ENERGY,
                state_class=STATE_CLASS_TOTAL_INCREASING,
            ),
            cv.Optional("instantaneous_power_a_positive"): sensor.sensor_schema(
                unit_of_measurement=UNIT_WATT,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_POWER,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional("instantaneous_power_a_negative"): sensor.sensor_schema(
                unit_of_measurement=UNIT_WATT,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_POWER,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional("reactive_instantaneous_power_a_positive"): sensor.sensor_schema(
                unit_of_measurement=UNIT_VOLT_AMPS_REACTIVE,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_POWER,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional("reactive_instantaneous_power_a_negative"): sensor.sensor_schema(
                unit_of_measurement=UNIT_VOLT_AMPS_REACTIVE,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_POWER,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    deprecation_warning,
)

CONFIG_SCHEMA = cv.Any(DYNAMIC_SCHEMA, OLD_SCHEMA)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_AMIS_METER_ID])

    if obis := config.get(CONF_OBIS_CODE):
        var = await sensor.new_sensor(config)
        cg.add(hub.register_sensor(obis, var))
    else:
        for key, obis_val in NUMERIC_KEYS.items():
            if sensor_config := config.get(key):
                sens = await sensor.new_sensor(sensor_config)
                cg.add(hub.register_sensor(obis_val, sens))
