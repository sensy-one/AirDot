#!/usr/bin/env python3
"""Generate firmware translation headers from i18n/*.yaml."""

from __future__ import annotations

from pathlib import Path
import re
import sys

sys.dont_write_bytecode = True


ROOT = Path(__file__).resolve().parents[1]
I18N_DIR = ROOT / "i18n"
OUTPUT = ROOT / "includes" / "airdot" / "generated" / "i18n_texts.h"
DEFAULT_LANGUAGE_CODE = "en"

SCREEN_IDS = {
    "setup_connect": 0,
    "setup_applying": 1,
    "setup_complete": 2,
    "setup_language": 3,
}

LANGUAGE_KEYS = ("id", "code", "label")
CONNECTION_KEYS = ("indoor", "outdoor")
WEATHER_CODE_KEYS = (
    "clear",
    "cloudy",
    "fog",
    "drizzle",
    "freezing_drizzle",
    "rain",
    "freezing_rain",
    "snow",
    "snow_grains",
    "rain_showers",
    "snow_showers",
    "thunderstorm",
    "thunderstorm_hail",
)
WEATHER_KEYS = WEATHER_CODE_KEYS + ("high_label", "low_label")
NOTIFICATION_KEYS = ("fallback", "dismiss")
DATE_KEYS = ("format",)
DATE_WEEKDAY_KEYS = (
    "sunday",
    "monday",
    "tuesday",
    "wednesday",
    "thursday",
    "friday",
    "saturday",
)
DATE_MONTH_KEYS = (
    "january",
    "february",
    "march",
    "april",
    "may",
    "june",
    "july",
    "august",
    "september",
    "october",
    "november",
    "december",
)

AIR_QUALITY_KEYS = (
    "balanced",
    "moderate",
    "poor",
    "unhealthy",
    "hazardous",
)

FACTORY_RESET_KEYS = (
    "factory_reset_countdown",
)

AQ_LEVELS = [
    ("moderate", "level == 1"),
    ("poor", "level == 2"),
    ("unhealthy", "level == 3"),
    ("hazardous", "level >= 4"),
]

HISTORY_KEYS = (
    "pm1_title",
    "pm25_title",
    "pm4_title",
    "pm10_title",
    "co2_title",
    "voc_title",
    "nox_title",
    "temperature_title",
    "humidity_title",
    "pressure_title",
    "light_title",
    "min_pattern",
    "max_pattern",
)

SETUP_PAGE_KEYS = (
    "html_title",
    "hero_title",
    "hero_body",
    "saved",
    "network_title",
    "network_note",
    "wifi_ssid_label",
    "wifi_password_label",
    "wifi_password_placeholder",
    "time_title",
    "time_note",
    "time_server_title",
    "time_server_description",
    "time_source_label",
    "time_source_network_label",
    "time_source_manual_label",
    "manual_time_label",
    "ha_discovery_title",
    "ha_discovery_description",
    "mqtt_title",
    "mqtt_description",
    "mqtt_broker_label",
    "mqtt_broker_placeholder",
    "mqtt_port_label",
    "mqtt_username_label",
    "mqtt_password_label",
    "mqtt_topic_prefix_label",
    "optional_placeholder",
    "air_quality_title",
    "air_quality_note",
    "air_quality_profile_label",
    "sensor_calibration_title",
    "sensor_calibration_note",
    "sen66_temperature_offset_label",
    "sen66_co2_reference_label",
    "sen66_co2_calibration_title",
    "sen66_co2_calibration_description",
    "alerts_title",
    "alerts_note",
    "display_alert_wake_screen_title",
    "display_alert_wake_screen_description",
    "integrations_title",
    "integrations_note",
    "publishing_interval_label",
    "audio_alerts_title",
    "audio_alerts_description",
    "weather_title",
    "weather_note",
    "weather_sync_title",
    "weather_sync_description",
    "weather_location_mode_label",
    "weather_location_ip_label",
    "weather_location_manual_label",
    "weather_location_city_label",
    "weather_location_city_placeholder",
    "display_title",
    "display_note",
    "language_label",
    "units_label",
    "metric_label",
    "imperial_label",
    "time_format_label",
    "time_24h_label",
    "time_12h_label",
    "brightness_label",
    "brightness_low",
    "brightness_medium",
    "brightness_high",
    "dark_mode_title",
    "dark_mode_description",
    "adaptive_brightness_title",
    "adaptive_brightness_description",
    "night_screen_off_title",
    "night_screen_off_description",
    "hazard_focus_title",
    "hazard_focus_description",
    "firmware_title",
    "firmware_note",
    "firmware_file_label",
    "firmware_select_button",
    "firmware_upload_button",
    "firmware_success",
    "firmware_no_file",
    "firmware_failed",
    "save_button",
    "footer_note",
    "offline_option",
    "no_networks",
)


def parse_scalar(value: str) -> str:
    if len(value) >= 2 and value[0] == value[-1] and value[0] in {"'", '"'}:
        return value[1:-1]
    return value


def parse_yaml(path: Path) -> dict:
    lines = path.read_text(encoding="utf-8").splitlines()
    root: dict = {}
    stack: list[tuple[int, dict]] = [(-1, root)]
    index = 0

    while index < len(lines):
        raw = lines[index]
        index += 1
        if not raw.strip() or raw.lstrip().startswith("#"):
            continue

        indent = len(raw) - len(raw.lstrip(" "))
        stripped = raw.strip()
        key, separator, value = stripped.partition(":")
        if separator != ":":
            raise ValueError(f"{path}:{index}: expected 'key: value'")

        while indent <= stack[-1][0]:
            stack.pop()
        parent = stack[-1][1]
        key = key.strip()
        value = value.strip()

        if value in {"|", "|-", ">"}:
            block_lines: list[str] = []
            block_indent: int | None = None
            while index < len(lines):
                candidate = lines[index]
                candidate_indent = len(candidate) - len(candidate.lstrip(" "))
                if candidate.strip() and candidate_indent <= indent:
                    break
                index += 1
                if not candidate.strip():
                    block_lines.append("")
                    continue
                if block_indent is None:
                    block_indent = candidate_indent
                block_lines.append(candidate[block_indent:])
            parent[key] = "\n".join(block_lines)
            continue

        if value == "":
            child: dict = {}
            parent[key] = child
            stack.append((indent, child))
            continue

        parent[key] = parse_scalar(value)

    return root


def flatten(data: dict, prefix: str = "") -> dict[str, str]:
    flattened: dict[str, str] = {}
    for key, value in data.items():
        path = f"{prefix}.{key}" if prefix else key
        if isinstance(value, dict):
            flattened.update(flatten(value, path))
        else:
            flattened[path] = str(value)
    return flattened


def require_same_keys(reference: dict, candidate: dict, path: Path) -> None:
    reference_keys = set(flatten(reference))
    candidate_keys = set(flatten(candidate))
    missing = sorted(reference_keys - candidate_keys)
    extra = sorted(candidate_keys - reference_keys)
    if missing or extra:
        details = []
        if missing:
            details.append(f"missing: {', '.join(missing)}")
        if extra:
            details.append(f"extra: {', '.join(extra)}")
        raise ValueError(f"{path}: translation keys differ from en.yaml ({'; '.join(details)})")


def require_section_keys(data: dict, section: str, expected_keys: tuple[str, ...], path: Path) -> None:
    value = data.get(section)
    if not isinstance(value, dict):
        raise ValueError(f"{path}: missing section '{section}'")

    actual_keys = set(value)
    expected_key_set = set(expected_keys)
    missing = [key for key in expected_keys if key not in actual_keys]
    extra = sorted(actual_keys - expected_key_set)
    if missing or extra:
        details = []
        if missing:
            details.append(f"missing: {', '.join(missing)}")
        if extra:
            details.append(f"extra: {', '.join(extra)}")
        raise ValueError(f"{path}: {section} keys are invalid ({'; '.join(details)})")


def validate_language(data: dict, path: Path) -> None:
    require_section_keys(data, "language", LANGUAGE_KEYS, path)
    require_section_keys(data, "setup_page", SETUP_PAGE_KEYS, path)
    require_section_keys(data, "history", HISTORY_KEYS, path)
    require_section_keys(data, "air_quality", AIR_QUALITY_KEYS, path)
    require_section_keys(data, "factory_reset", FACTORY_RESET_KEYS, path)
    require_section_keys(data, "connection", CONNECTION_KEYS, path)
    require_section_keys(data, "weather", WEATHER_KEYS, path)
    require_section_keys(data, "notification", NOTIFICATION_KEYS, path)
    require_section_keys(data, "date", DATE_KEYS + ("weekdays", "months"), path)
    require_section_keys(data["date"], "weekdays", DATE_WEEKDAY_KEYS, path)
    require_section_keys(data["date"], "months", DATE_MONTH_KEYS, path)
    require_section_keys(data, "onboarding", tuple(SCREEN_IDS), path)

    for level in AIR_QUALITY_KEYS:
        require_section_keys(data["air_quality"], level, ("status", "action"), path)

    for reset_key in FACTORY_RESET_KEYS:
        require_section_keys(data["factory_reset"], reset_key, ("title", "body"), path)

    if "{seconds}" not in get(data, "factory_reset.factory_reset_countdown.body"):
        raise ValueError(f"{path}: factory_reset.factory_reset_countdown.body must contain '{{seconds}}'")

    onboarding_screen_keys = {
        "setup_language": ("title", "body"),
        "setup_connect": ("title", "body", "button_0", "button_0_reopen"),
        "setup_applying": ("title", "body", "button_0"),
        "setup_complete": ("title", "body", "countdown"),
    }

    for screen_key, screen in data["onboarding"].items():
        if not isinstance(screen, dict):
            raise ValueError(f"{path}: onboarding.{screen_key} must be a mapping")
        required = onboarding_screen_keys[screen_key]
        missing = [key for key in required if key not in screen]
        extra = sorted(set(screen) - set(required))
        if missing or extra:
            details = []
            if missing:
                details.append(f"missing: {', '.join(missing)}")
            if extra:
                details.append(f"extra: {', '.join(extra)}")
            raise ValueError(f"{path}: onboarding.{screen_key} keys are invalid ({'; '.join(details)})")

    if "{seconds}" not in get(data, "onboarding.setup_complete.countdown"):
        raise ValueError(f"{path}: onboarding.setup_complete.countdown must contain '{{seconds}}'")

    for key in ("min_pattern", "max_pattern"):
        if "{value}" not in get(data, f"history.{key}"):
            raise ValueError(f"{path}: history.{key} must contain '{{value}}'")

    for placeholder in ("{weekday}", "{day}", "{month}"):
        if placeholder not in get(data, "date.format"):
            raise ValueError(f"{path}: date.format must contain '{placeholder}'")


def c_string(value: str | None) -> str:
    if value is None:
        return "nullptr"
    escaped = (
        value.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\n", "\\n")
        .replace("\r", "")
    )
    return f'"{escaped}"'


def enum_name(code: str) -> str:
    name = re.sub(r"[^A-Za-z0-9]+", "_", code).strip("_").upper()
    if not name or name[0].isdigit():
        name = f"LANG_{name}"
    return name


def get(data: dict, path: str) -> str:
    current = data
    for part in path.split("."):
        current = current[part]
    return str(current)


def load_languages() -> list[dict]:
    files = sorted(I18N_DIR.glob("*.yaml"))
    if not files:
        raise ValueError("No i18n/*.yaml files found")

    loaded = []
    for path in files:
        data = parse_yaml(path)
        validate_language(data, path)
        code = get(data, "language.code")
        if path.stem != code:
            raise ValueError(f"{path}: language.code must match filename stem")
        language_id = int(get(data, "language.id"))
        if language_id < 0 or language_id > 255:
            raise ValueError(f"{path}: language.id must fit in uint8_t")
        loaded.append({"path": path, "data": data, "code": code, "id": language_id})

    ids = [entry["id"] for entry in loaded]
    if len(ids) != len(set(ids)):
        raise ValueError("Language ids must be unique")
    codes = [entry["code"] for entry in loaded]
    if len(codes) != len(set(codes)):
        raise ValueError("Language codes must be unique")

    reference = next((entry["data"] for entry in loaded if entry["code"] == DEFAULT_LANGUAGE_CODE), None)
    if reference is None:
        raise ValueError(f"i18n/{DEFAULT_LANGUAGE_CODE}.yaml is required as the reference language")
    for entry in loaded:
        require_same_keys(reference, entry["data"], entry["path"])

    return sorted(loaded, key=lambda entry: entry["id"])


def default_entry(languages: list[dict]) -> dict:
    for entry in languages:
        if entry["code"] == DEFAULT_LANGUAGE_CODE:
            return entry
    raise ValueError(f"Missing default language '{DEFAULT_LANGUAGE_CODE}'")


def emit_language_switch(languages: list[dict], expression: str, value_path: str) -> list[str]:
    default = default_entry(languages)
    lines = [f"  switch ({expression}) {{"]
    for entry in languages:
        lines.extend(
            [
                f"    case UiLanguage::{enum_name(entry['code'])}:",
                f"      return {c_string(get(entry['data'], value_path))};",
            ]
        )
    lines.extend(["    default:", f"      return {c_string(get(default['data'], value_path))};", "  }"])
    return lines


def generate(languages: list[dict]) -> str:
    default = default_entry(languages)
    default_enum = enum_name(default["code"])
    lines: list[str] = [
        "#pragma once",
        "",
        "#include <cstdio>",
        "#include <cstdint>",
        "#include <string>",
        "",
        "// Generated by scripts/generate_i18n.py from i18n/*.yaml. Do not edit manually.",
        "",
        "namespace AirDot {",
        "",
        "enum class UiLanguage : uint8_t {",
    ]
    for entry in languages:
        lines.append(f"  {enum_name(entry['code'])} = {entry['id']},")
    lines.extend(
        [
            "};",
            "",
            "struct AirQualityText {",
            "  const char *status;",
            "  const char *action;",
            "};",
            "",
            "struct TitleBodyText {",
            "  const char *title;",
            "  const char *body;",
            "};",
            "",
            "struct HistoryText {",
        ]
    )
    for key in HISTORY_KEYS:
        lines.append(f"  const char *{key};")
    lines.extend(
        [
            "};",
            "",
            "inline UiLanguage normalize_ui_language(uint8_t value) {",
            "  switch (static_cast<UiLanguage>(value)) {",
        ]
    )
    for entry in languages:
        lines.extend(
            [
                f"    case UiLanguage::{enum_name(entry['code'])}:",
                f"      return UiLanguage::{enum_name(entry['code'])};",
            ]
        )
    lines.extend(
        [
            "    default:",
            f"      return UiLanguage::{default_enum};",
            "  }",
            "}",
            "",
            "inline AirQualityText air_quality_text(int level, UiLanguage language) {",
            "  switch (language) {",
        ]
    )
    for entry in languages:
        data = entry["data"]
        lines.append(f"    case UiLanguage::{enum_name(entry['code'])}:")
        for key, condition in AQ_LEVELS:
            lines.append(f"      if ({condition})")
            lines.append(
                "        return {"
                f"{c_string(get(data, f'air_quality.{key}.status'))}, "
                f"{c_string(get(data, f'air_quality.{key}.action'))}"
                "};"
            )
        lines.append(
            "      return {"
            f"{c_string(get(data, 'air_quality.balanced.status'))}, "
            f"{c_string(get(data, 'air_quality.balanced.action'))}"
            "};"
        )
    lines.extend(
        [
            "    default:",
            f"      return air_quality_text(level, UiLanguage::{default_enum});",
            "  }",
            "}",
            "",
            "inline AirQualityText air_quality_text(int level) {",
            f"  return air_quality_text(level, UiLanguage::{default_enum});",
            "}",
            "",
        ]
    )
    for key in FACTORY_RESET_KEYS:
        function_name = f"{key}_text"
        lines.extend(
            [
                f"inline TitleBodyText {function_name}(UiLanguage language) {{",
                "  switch (language) {",
            ]
        )
        for entry in languages:
            data = entry["data"]
            lines.extend(
                [
                    f"    case UiLanguage::{enum_name(entry['code'])}:",
                    "      return {"
                    f"{c_string(get(data, f'factory_reset.{key}.title'))}, "
                    f"{c_string(get(data, f'factory_reset.{key}.body'))}"
                    "};",
                ]
            )
        lines.extend(
            [
                "    default:",
                f"      return {function_name}(UiLanguage::{default_enum});",
                "  }",
                "}",
                "",
            ]
        )
    lines.extend(
        [
            "inline int weather_code_group(int code) {",
            "  switch (code) {",
            "    case 0:",
            "      return 0;",
            "    case 1:",
            "    case 2:",
            "    case 3:",
            "      return 1;",
            "    case 45:",
            "    case 48:",
            "      return 2;",
            "    case 51:",
            "    case 53:",
            "    case 55:",
            "      return 3;",
            "    case 56:",
            "    case 57:",
            "      return 4;",
            "    case 61:",
            "    case 63:",
            "    case 65:",
            "      return 5;",
            "    case 66:",
            "    case 67:",
            "      return 6;",
            "    case 71:",
            "    case 73:",
            "    case 75:",
            "      return 7;",
            "    case 77:",
            "      return 8;",
            "    case 80:",
            "    case 81:",
            "    case 82:",
            "      return 9;",
            "    case 85:",
            "    case 86:",
            "      return 10;",
            "    case 95:",
            "      return 11;",
            "    case 96:",
            "    case 99:",
            "      return 12;",
            "    default:",
            "      return -1;",
            "  }",
            "}",
            "",
            "inline const char *weather_text(int code, UiLanguage language) {",
            "  switch (weather_code_group(code)) {",
        ]
    )
    for index, key in enumerate(WEATHER_CODE_KEYS):
        lines.append(f"    case {index}:")
        lines.extend(["      " + line for line in emit_language_switch(languages, "language", f"weather.{key}")])
    lines.extend(
        [
            "    default:",
            "      return \"\";",
        ]
    )
    lines.extend(
        [
            "  }",
            "}",
            "",
            "inline const char *weather_high_temperature_label(UiLanguage language) {",
        ]
    )
    lines.extend(emit_language_switch(languages, "language", "weather.high_label"))
    lines.extend(
        [
            "}",
            "",
            "inline const char *weather_low_temperature_label(UiLanguage language) {",
        ]
    )
    lines.extend(emit_language_switch(languages, "language", "weather.low_label"))
    lines.extend(
        [
            "}",
            "",
            "inline const char *connection_location_text(bool outdoor, UiLanguage language) {",
            "  if (outdoor) {",
        ]
    )
    lines.extend(["  " + line for line in emit_language_switch(languages, "language", "connection.outdoor")])
    lines.extend(["  }", ""])
    lines.extend(["  " + line for line in emit_language_switch(languages, "language", "connection.indoor")])
    lines.extend(
        [
            "}",
            "",
            "inline const char *date_format_text(UiLanguage language) {",
        ]
    )
    lines.extend(emit_language_switch(languages, "language", "date.format"))
    lines.extend(["}", "", "inline const char *weekday_text(uint8_t day_of_week, UiLanguage language) {"])
    lines.append("  switch (language) {")
    for entry in languages:
        data = entry["data"]
        lines.append(f"    case UiLanguage::{enum_name(entry['code'])}:")
        lines.append("      switch (day_of_week) {")
        for index, key in enumerate(DATE_WEEKDAY_KEYS, start=1):
            lines.append(f"        case {index}:")
            lines.append(f"          return {c_string(get(data, f'date.weekdays.{key}'))};")
        lines.append("      }")
        lines.append(f"      return {c_string(get(data, 'date.weekdays.monday'))};")
    lines.extend(
        [
            "    default:",
            f"      return weekday_text(day_of_week, UiLanguage::{default_enum});",
            "  }",
            "}",
            "",
            "inline const char *month_text(uint8_t month, UiLanguage language) {",
            "  switch (language) {",
        ]
    )
    for entry in languages:
        data = entry["data"]
        lines.append(f"    case UiLanguage::{enum_name(entry['code'])}:")
        lines.append("      switch (month) {")
        for index, key in enumerate(DATE_MONTH_KEYS, start=1):
            lines.append(f"        case {index}:")
            lines.append(f"          return {c_string(get(data, f'date.months.{key}'))};")
        lines.append("      }")
        lines.append(f"      return {c_string(get(data, 'date.months.january'))};")
    lines.extend(
        [
            "    default:",
            f"      return month_text(month, UiLanguage::{default_enum});",
            "  }",
            "}",
            "",
            "inline void replace_date_token_(std::string &text, const char *token, const char *value) {",
            "  const std::string token_text(token);",
            "  const std::string value_text(value == nullptr ? \"\" : value);",
            "  size_t position = 0;",
            "  while ((position = text.find(token_text, position)) != std::string::npos) {",
            "    text.replace(position, token_text.size(), value_text);",
            "    position += value_text.size();",
            "  }",
            "}",
            "",
            "inline std::string date_text(uint8_t day_of_week, uint8_t day, uint8_t month, uint16_t year, UiLanguage language) {",
            "  char day_text[4];",
            "  char year_text[5];",
            "  std::snprintf(day_text, sizeof(day_text), \"%u\", static_cast<unsigned>(day));",
            "  std::snprintf(year_text, sizeof(year_text), \"%04u\", static_cast<unsigned>(year));",
            "  std::string text = date_format_text(language);",
            "  replace_date_token_(text, \"{weekday}\", weekday_text(day_of_week, language));",
            "  replace_date_token_(text, \"{day}\", day_text);",
            "  replace_date_token_(text, \"{month}\", month_text(month, language));",
            "  replace_date_token_(text, \"{year}\", year_text);",
            "  return text;",
            "}",
            "",
            "inline HistoryText history_text(UiLanguage language) {",
            "  switch (language) {",
        ]
    )
    for entry in languages:
        data = entry["data"]
        lines.append(f"    case UiLanguage::{enum_name(entry['code'])}:")
        lines.append("      return {")
        for key in HISTORY_KEYS:
            lines.append(f"        {c_string(get(data, f'history.{key}'))},")
        lines.append("      };")
    lines.extend(
        [
            "    default:",
            f"      return history_text(UiLanguage::{default_enum});",
            "  }",
            "}",
            "",
            "}  // namespace AirDot",
            "",
            "namespace AirDot::onboarding {",
            "",
        ]
    )

    lines.extend(
        [
            "struct SetupPageText {",
        ]
    )
    for key in SETUP_PAGE_KEYS:
        lines.append(f"  const char *{key};")
    lines.extend(
        [
            "};",
            "",
            "struct ScreenContent {",
            "  const char *title;",
            "  const char *body;",
            "  const char *button_0;",
            "  const char *button_0_reopen;",
            "  const char *countdown;",
            "};",
            "",
            "inline const char *ui_language_value(AirDot::UiLanguage language) {",
            "  switch (language) {",
        ]
    )
    for entry in languages:
        lines.extend(
            [
                f"    case AirDot::UiLanguage::{enum_name(entry['code'])}:",
                f"      return {c_string(entry['code'])};",
            ]
        )
    lines.extend(
        [
            "    default:",
            f"      return {c_string(default['code'])};",
            "  }",
            "}",
            "",
            "inline const char *ui_language_label(AirDot::UiLanguage language) {",
        ]
    )
    lines.extend(emit_language_switch(languages, "language", "language.label"))
    lines.extend(
        [
            "}",
            "",
            f"inline uint8_t ui_language_count() {{ return {len(languages)}; }}",
            "",
            "inline AirDot::UiLanguage ui_language_at(uint8_t index) {",
            "  switch (index) {",
        ]
    )
    for index, entry in enumerate(languages):
        lines.extend(
            [
                f"    case {index}:",
                f"      return AirDot::UiLanguage::{enum_name(entry['code'])};",
            ]
        )
    lines.extend(
        [
            "    default:",
            f"      return AirDot::UiLanguage::{default_enum};",
            "  }",
            "}",
            "",
            "inline AirDot::UiLanguage ui_language_from_value(const std::string &value) {",
        ]
    )
    for entry in languages:
        lines.extend(
            [
                f"  if (value == {c_string(entry['code'])})",
                f"    return AirDot::UiLanguage::{enum_name(entry['code'])};",
            ]
        )
    lines.extend(
        [
            f"  return AirDot::UiLanguage::{default_enum};",
            "}",
            "",
            "inline SetupPageText setup_page_text(AirDot::UiLanguage language) {",
            "  switch (language) {",
        ]
    )
    for entry in languages:
        data = entry["data"]
        lines.append(f"    case AirDot::UiLanguage::{enum_name(entry['code'])}:")
        lines.append("      return {")
        for key in SETUP_PAGE_KEYS:
            lines.append(f"        {c_string(get(data, f'setup_page.{key}'))},")
        lines.append("      };")
    lines.extend(
        [
            "    default:",
            f"      return setup_page_text(AirDot::UiLanguage::{default_enum});",
            "  }",
            "}",
            "",
            "inline void append_json_string_(std::string &html, const char *value) {",
            "  html += '\"';",
            "  if (value != nullptr) {",
            "    for (const char *cursor = value; *cursor != '\\0'; cursor++) {",
            "      switch (*cursor) {",
            "        case '\"':",
            "          html += \"\\\\\\\"\";",
            "          break;",
            "        case '\\\\':",
            "          html += \"\\\\\\\\\";",
            "          break;",
            "        case '\\n':",
            "          html += \"\\\\n\";",
            "          break;",
            "        case '\\r':",
            "          break;",
            "        case '\\t':",
            "          html += \"\\\\t\";",
            "          break;",
            "        default:",
            "          html += *cursor;",
            "          break;",
            "      }",
            "    }",
            "  }",
            "  html += '\"';",
            "}",
            "",
            "inline void append_json_field_(std::string &html, const char *key, const char *value, bool &first) {",
            "  if (!first)",
            "    html += ',';",
            "  first = false;",
            "  append_json_string_(html, key);",
            "  html += ':';",
            "  append_json_string_(html, value);",
            "}",
            "",
            "inline void append_setup_page_translation_json(std::string &html, AirDot::UiLanguage language) {",
            "  const auto text = setup_page_text(language);",
            "  bool first = true;",
            "  html += \"{\";",
        ]
    )
    for key in SETUP_PAGE_KEYS:
        lines.append(f"  append_json_field_(html, {c_string(key)}, text.{key}, first);")
    lines.extend(
        [
            "  html += \"}\";",
            "}",
            "",
            "inline ScreenContent screen_content(int screen, AirDot::UiLanguage language) {",
            "  switch (language) {",
        ]
    )
    for entry in languages:
        data = entry["data"]
        lines.append(f"    case AirDot::UiLanguage::{enum_name(entry['code'])}:")
        lines.append("      switch (screen) {")
        for screen_key, screen_id in SCREEN_IDS.items():
            lines.append(f"        case {screen_id}:")
            lines.append("          return {")
            lines.append(f"            {c_string(get(data, f'onboarding.{screen_key}.title'))},")
            lines.append(f"            {c_string(get(data, f'onboarding.{screen_key}.body'))},")
            button = data["onboarding"][screen_key].get("button_0")
            lines.append(f"            {c_string(button)},")
            reopen_button = data["onboarding"][screen_key].get("button_0_reopen")
            lines.append(f"            {c_string(reopen_button)},")
            countdown = data["onboarding"][screen_key].get("countdown")
            lines.append(f"            {c_string(countdown)},")
            lines.append("          };")
        lines.append("      }")
        lines.append("      break;")
    lines.extend(
        [
            "    default:",
            f"      return screen_content(screen, AirDot::UiLanguage::{default_enum});",
            "  }",
            "  return screen_content(0, language);",
            "}",
            "",
            "inline const char *notification_title_text(AirDot::UiLanguage language) {",
        ]
    )
    lines.extend(emit_language_switch(languages, "language", "notification.fallback"))
    lines.extend(["}", "", "inline const char *dismiss_button_text(AirDot::UiLanguage language) {"])
    lines.extend(emit_language_switch(languages, "language", "notification.dismiss"))
    lines.extend(["}", "", "}  // namespace AirDot::onboarding", ""])

    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    args = sys.argv[1:] if argv is None else argv
    if args:
        print("Usage: generate_i18n.py", file=sys.stderr)
        return 2

    try:
        languages = load_languages()
        generated = generate(languages)
    except Exception as exc:
        print(f"generate_i18n.py: {exc}", file=sys.stderr)
        return 1

    current = OUTPUT.read_text(encoding="utf-8") if OUTPUT.exists() else ""
    if current == generated:
        return 0

    OUTPUT.write_text(generated, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
