from esphome import pins
import esphome.codegen as cg
from esphome.components import display
from esphome.components.esp32 import VARIANT_ESP32P4, VARIANT_ESP32S3, only_on_variant
import esphome.config_validation as cv
from esphome.const import (
    CONF_BLUE,
    CONF_CS_PIN,
    CONF_DATA_PINS,
    CONF_DIMENSIONS,
    CONF_GREEN,
    CONF_HEIGHT,
    CONF_HSYNC_PIN,
    CONF_ID,
    CONF_IGNORE_STRAPPING_WARNING,
    CONF_LAMBDA,
    CONF_NUMBER,
    CONF_RED,
    CONF_RESET_PIN,
    CONF_VSYNC_PIN,
    CONF_WIDTH,
)

CONF_DE_PIN = "de_pin"
CONF_HSYNC_BACK_PORCH = "hsync_back_porch"
CONF_HSYNC_FRONT_PORCH = "hsync_front_porch"
CONF_HSYNC_PULSE_WIDTH = "hsync_pulse_width"
CONF_INIT_SCL_PIN = "init_scl_pin"
CONF_INIT_SDA_PIN = "init_sda_pin"
CONF_PCLK_FREQUENCY = "pclk_frequency"
CONF_PCLK_INVERTED = "pclk_inverted"
CONF_PCLK_PIN = "pclk_pin"
CONF_VSYNC_BACK_PORCH = "vsync_back_porch"
CONF_VSYNC_FRONT_PORCH = "vsync_front_porch"
CONF_VSYNC_PULSE_WIDTH = "vsync_pulse_width"
CONF_DRIVE_STRENGTH = "drive_strength"

DEPENDENCIES = ["esp32", "psram"]

nhd_ns = cg.esphome_ns.namespace("nhd_2_1_480480af_asxp")
nhd_display = nhd_ns.class_("Nhd21480480AfAsxp", display.Display, cg.Component)

DATA_PIN_SCHEMA = pins.internal_gpio_output_pin_schema


def data_pin_validate(value):
    if not isinstance(value, dict):
        try:
            return DATA_PIN_SCHEMA(
                {CONF_NUMBER: value, CONF_IGNORE_STRAPPING_WARNING: True}
            )
        except cv.Invalid:
            pass
    return DATA_PIN_SCHEMA(value)


def data_pin_set(length):
    return cv.All(
        [data_pin_validate],
        cv.Length(min=length, max=length, msg=f"Exactly {length} data pins required"),
    )


def output_pin(number, ignore_strapping_warning=False):
    return {
        CONF_NUMBER: number,
        CONF_DRIVE_STRENGTH: "40mA",
        CONF_IGNORE_STRAPPING_WARNING: ignore_strapping_warning,
    }


DEFAULT_DATA_PINS = {
    CONF_RED: [
        output_pin(21),
        output_pin(14),
        output_pin(13),
        output_pin(12),
        output_pin(11),
    ],
    CONF_GREEN: [
        output_pin(17),
        output_pin(18),
        output_pin(8),
        output_pin(45, True),
        output_pin(48),
        output_pin(47),
    ],
    CONF_BLUE: [
        output_pin(40),
        output_pin(39),
        output_pin(38),
        output_pin(15),
        output_pin(16),
    ],
}

DIMENSIONS_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_WIDTH): cv.int_,
        cv.Required(CONF_HEIGHT): cv.int_,
    }
)

CONFIG_SCHEMA = cv.All(
    display.FULL_DISPLAY_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(nhd_display),
            cv.Optional(
                CONF_DIMENSIONS,
                default={CONF_WIDTH: 480, CONF_HEIGHT: 480},
            ): DIMENSIONS_SCHEMA,
            cv.Optional(CONF_CS_PIN, default="GPIO9"): pins.gpio_output_pin_schema,
            cv.Optional(CONF_INIT_SCL_PIN, default={
                CONF_NUMBER: 46,
                CONF_IGNORE_STRAPPING_WARNING: True,
            }): pins.gpio_output_pin_schema,
            cv.Optional(CONF_INIT_SDA_PIN, default={
                CONF_NUMBER: 3,
                CONF_IGNORE_STRAPPING_WARNING: True,
            }): pins.gpio_output_pin_schema,
            cv.Optional(CONF_RESET_PIN, default="GPIO10"): pins.gpio_output_pin_schema,
            cv.Optional(CONF_DE_PIN, default=output_pin(41)): pins.internal_gpio_output_pin_schema,
            cv.Optional(CONF_PCLK_PIN, default=output_pin(42)): pins.internal_gpio_output_pin_schema,
            cv.Optional(CONF_HSYNC_PIN, default=output_pin(2)): pins.internal_gpio_output_pin_schema,
            cv.Optional(CONF_VSYNC_PIN, default=output_pin(1)): pins.internal_gpio_output_pin_schema,
            cv.Optional(CONF_PCLK_FREQUENCY, default="8MHz"): cv.All(
                cv.frequency, cv.Range(min=4e6, max=100e6)
            ),
            cv.Optional(CONF_PCLK_INVERTED, default=True): cv.boolean,
            cv.Optional(CONF_HSYNC_FRONT_PORCH, default=50): cv.int_,
            cv.Optional(CONF_HSYNC_BACK_PORCH, default=50): cv.int_,
            cv.Optional(CONF_HSYNC_PULSE_WIDTH, default=4): cv.int_,
            cv.Optional(CONF_VSYNC_FRONT_PORCH, default=50): cv.int_,
            cv.Optional(CONF_VSYNC_BACK_PORCH, default=50): cv.int_,
            cv.Optional(CONF_VSYNC_PULSE_WIDTH, default=2): cv.int_,
            cv.Optional(CONF_DATA_PINS, default=DEFAULT_DATA_PINS): cv.Schema(
                {
                    cv.Required(CONF_RED): data_pin_set(5),
                    cv.Required(CONF_GREEN): data_pin_set(6),
                    cv.Required(CONF_BLUE): data_pin_set(5),
                }
            ),
        }
    ),
    cv.only_on_esp32,
    only_on_variant(supported=[VARIANT_ESP32S3, VARIANT_ESP32P4]),
)


async def to_code(config):
    dimensions = config[CONF_DIMENSIONS]
    var = cg.new_Pvariable(config[CONF_ID], dimensions[CONF_WIDTH], dimensions[CONF_HEIGHT])

    cg.add(var.set_hsync_pulse_width(config[CONF_HSYNC_PULSE_WIDTH]))
    cg.add(var.set_hsync_back_porch(config[CONF_HSYNC_BACK_PORCH]))
    cg.add(var.set_hsync_front_porch(config[CONF_HSYNC_FRONT_PORCH]))
    cg.add(var.set_vsync_pulse_width(config[CONF_VSYNC_PULSE_WIDTH]))
    cg.add(var.set_vsync_back_porch(config[CONF_VSYNC_BACK_PORCH]))
    cg.add(var.set_vsync_front_porch(config[CONF_VSYNC_FRONT_PORCH]))
    cg.add(var.set_pclk_inverted(config[CONF_PCLK_INVERTED]))
    cg.add(var.set_pclk_frequency(config[CONF_PCLK_FREQUENCY]))

    data_pins = []
    data_pins.extend(config[CONF_DATA_PINS][CONF_BLUE])
    data_pins.extend(config[CONF_DATA_PINS][CONF_GREEN])
    data_pins.extend(config[CONF_DATA_PINS][CONF_RED])
    for index, pin in enumerate(data_pins):
        data_pin = await cg.gpio_pin_expression(pin)
        cg.add(var.add_data_pin(data_pin, index))

    cs_pin = await cg.gpio_pin_expression(config[CONF_CS_PIN])
    cg.add(var.set_cs_pin(cs_pin))
    init_scl_pin = await cg.gpio_pin_expression(config[CONF_INIT_SCL_PIN])
    cg.add(var.set_init_scl_pin(init_scl_pin))
    init_sda_pin = await cg.gpio_pin_expression(config[CONF_INIT_SDA_PIN])
    cg.add(var.set_init_sda_pin(init_sda_pin))
    reset_pin = await cg.gpio_pin_expression(config[CONF_RESET_PIN])
    cg.add(var.set_reset_pin(reset_pin))

    de_pin = await cg.gpio_pin_expression(config[CONF_DE_PIN])
    cg.add(var.set_de_pin(de_pin))
    pclk_pin = await cg.gpio_pin_expression(config[CONF_PCLK_PIN])
    cg.add(var.set_pclk_pin(pclk_pin))
    hsync_pin = await cg.gpio_pin_expression(config[CONF_HSYNC_PIN])
    cg.add(var.set_hsync_pin(hsync_pin))
    vsync_pin = await cg.gpio_pin_expression(config[CONF_VSYNC_PIN])
    cg.add(var.set_vsync_pin(vsync_pin))

    await display.register_display(var, config)
    if lamb := config.get(CONF_LAMBDA):
        lambda_ = await cg.process_lambda(
            lamb, [(display.DisplayRef, "it")], return_type=cg.void
        )
        cg.add(var.set_writer(lambda_))
