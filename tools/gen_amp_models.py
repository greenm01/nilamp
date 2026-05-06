#!/usr/bin/env python3
"""Generate native C amp model data from nilamp KDL2 model specs."""

from __future__ import annotations

import argparse
import math
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


TOKEN_RE = re.compile(
    r"""
    (?P<ws>[ \t\r]+)
  | (?P<comment>//[^\n]*)
  | (?P<newline>\n)
  | (?P<brace>[{}])
  | (?P<equals>=)
  | (?P<string>"(?:\\.|[^"\\])*")
  | (?P<bare>[^\s{}=";]+)
  | (?P<semi>;)
    """,
    re.VERBOSE,
)

STAGE_ORDER = ("t1", "t2", "t3", "t4", "t5")
KNOWN_TOPOLOGIES = {"tweed_5e3_cathodyne_pp"}
KNOWN_METHODS = {"keller_glf_adaa", "keller_cd_adaa", "dempwolf_zolzer_adaa", "hegglun_blocking_w", "keller_pss"}
KNOWN_KINDS = {"triode_ck", "cathodyne", "power_ck"}
KNOWN_TABLES = {
    "nilamp_t1_12ax7_table",
    "nilamp_t2_12ax7_table",
    "nilamp_t3_cd_table",
    "nilamp_t4_6v6_table",
    "nilamp_t5_6v6_table",
}
STAGE_FIELDS = (
    "kpre",
    "isat",
    "rl",
    "rkl",
    "kspre",
    "kspost",
    "ksva",
    "ksvk",
    "ksib",
    "kfb",
    "kpk",
    "pk_xth",
    "pk_xdiode",
    "pk_attack",
    "pk_release",
    "avg_tau",
)
PROCESS_FIELDS = (
    "input_feed_gain",
    "input_keller_gain_sq",
    "pss1_r",
    "pss1_tau",
    "pss2_r",
    "pss2_tau",
    "pss3_r_at_full_sag",
    "pss3_tau",
    "phase_t4_gain",
    "phase_t5_gain",
    "screen_current_feedback",
)
CONTROL_FIELDS = (
    "id",
    "name",
    "module",
    "unit",
    "min",
    "max",
    "default",
    "step",
    "display",
)
KNOWN_CONTROL_DISPLAYS = {
    "linear": "NILAMP_CONTROL_DISPLAY_LINEAR",
    "iso266": "NILAMP_CONTROL_DISPLAY_ISO266",
    "enum": "NILAMP_CONTROL_DISPLAY_ENUM",
}
CLAP_FILENAME_RE = re.compile(r"[A-Za-z0-9._-]+\.clap")
KNOWN_GUI_SCREENS = {
    "main": "NILAMP_GUI_SCREEN_ID_MAIN",
    "options": "NILAMP_GUI_SCREEN_ID_OPTIONS",
    "about": "NILAMP_GUI_SCREEN_ID_ABOUT",
}
KNOWN_GUI_WIDGET_TYPES = {
    "text": "NILAMP_GUI_WIDGET_TEXT",
    "button": "NILAMP_GUI_WIDGET_BUTTON",
    "panel": "NILAMP_GUI_WIDGET_PANEL",
    "knob": "NILAMP_GUI_WIDGET_KNOB",
    "enum": "NILAMP_GUI_WIDGET_ENUM",
}
KNOWN_GUI_TEXT_STYLES = {
    "normal": "NILAMP_GUI_TEXT_NORMAL",
    "title": "NILAMP_GUI_TEXT_TITLE",
    "subtitle": "NILAMP_GUI_TEXT_SUBTITLE",
    "about": "NILAMP_GUI_TEXT_ABOUT",
}
KNOWN_GUI_KNOB_STYLES = {
    "percent": "NILAMP_GUI_KNOB_DISPLAY_PERCENT",
    "gain_unipolar": "NILAMP_GUI_KNOB_DISPLAY_GAIN_UNIPOLAR",
    "gain_bipolar": "NILAMP_GUI_KNOB_DISPLAY_GAIN_BIPOLAR",
}
GUI_THEME_FIELDS = (
    "background",
    "header",
    "header_rule",
    "panel",
    "border",
    "text",
)


@dataclass
class Token:
    kind: str
    value: str
    line: int
    col: int


@dataclass
class Node:
    name: str
    args: list[Any] = field(default_factory=list)
    props: dict[str, Any] = field(default_factory=dict)
    children: list["Node"] = field(default_factory=list)
    line: int = 0


def fail(msg: str) -> None:
    raise SystemExit(f"gen_amp_models.py: {msg}")


def tokenize(text: str) -> list[Token]:
    tokens: list[Token] = []
    pos = 0
    line = 1
    col = 1
    while pos < len(text):
        match = TOKEN_RE.match(text, pos)
        if match is None:
            fail(f"unexpected character at {line}:{col}")
        kind = match.lastgroup or ""
        value = match.group(kind)
        if kind in {"ws", "comment"}:
            pass
        elif kind == "newline":
            tokens.append(Token(kind, value, line, col))
        elif kind != "semi":
            tokens.append(Token(kind, value, line, col))
        line_breaks = value.count("\n")
        if line_breaks:
            line += line_breaks
            col = len(value.rsplit("\n", 1)[-1]) + 1
        else:
            col += len(value)
        pos = match.end()
    tokens.append(Token("eof", "", line, col))
    return tokens


class Parser:
    def __init__(self, tokens: list[Token]) -> None:
        self.tokens = tokens
        self.pos = 0

    def peek(self) -> Token:
        return self.tokens[self.pos]

    def pop(self) -> Token:
        token = self.peek()
        self.pos += 1
        return token

    def parse(self) -> list[Node]:
        nodes = self.parse_nodes(until_brace=False)
        if self.peek().kind != "eof":
            token = self.peek()
            fail(f"unexpected token {token.value!r} at {token.line}:{token.col}")
        return nodes

    def parse_nodes(self, *, until_brace: bool) -> list[Node]:
        nodes: list[Node] = []
        while True:
            while self.peek().kind == "newline":
                self.pop()
            token = self.peek()
            if token.kind == "eof":
                if until_brace:
                    fail("unterminated child block")
                return nodes
            if token.kind == "brace" and token.value == "}":
                if not until_brace:
                    fail(f"unexpected closing brace at {token.line}:{token.col}")
                self.pop()
                return nodes
            nodes.append(self.parse_node())

    def parse_node(self) -> Node:
        name_token = self.pop()
        if name_token.kind not in {"bare", "string"}:
            fail(f"expected node name at {name_token.line}:{name_token.col}")
        node = Node(name=parse_value(name_token), line=name_token.line)
        while True:
            token = self.peek()
            if token.kind in {"newline", "eof"}:
                if token.kind == "newline":
                    self.pop()
                return node
            if token.kind == "brace" and token.value == "}":
                return node
            if token.kind == "brace" and token.value == "{":
                self.pop()
                node.children = self.parse_nodes(until_brace=True)
                if self.peek().kind == "newline":
                    self.pop()
                return node
            if token.kind not in {"bare", "string"}:
                fail(f"unexpected token {token.value!r} at {token.line}:{token.col}")
            item = self.pop()
            if self.peek().kind == "equals":
                key = parse_value(item)
                if not isinstance(key, str):
                    fail(f"property key must be a string at {item.line}:{item.col}")
                self.pop()
                value_token = self.pop()
                if value_token.kind not in {"bare", "string"}:
                    fail(f"expected property value at {value_token.line}:{value_token.col}")
                if key in node.props:
                    fail(f"duplicate property {key!r} on line {item.line}")
                node.props[key] = parse_value(value_token)
            else:
                node.args.append(parse_value(item))


def parse_value(token: Token) -> Any:
    if token.kind == "string":
        try:
            return bytes(token.value[1:-1], "utf-8").decode("unicode_escape")
        except UnicodeDecodeError as exc:
            fail(f"bad string escape at {token.line}:{token.col}: {exc}")
    value = token.value
    if value == "#true":
        return True
    if value == "#false":
        return False
    if value == "#null":
        return None
    if re.fullmatch(r"[+-]?(?:\d+\.\d*|\d*\.\d+|\d+)(?:[eE][+-]?\d+)?", value):
        if "." in value or "e" in value.lower():
            return float(value)
        return int(value)
    return value


def node_child(node: Node, name: str) -> Node:
    matches = [child for child in node.children if child.name == name]
    if len(matches) != 1:
        fail(f"{node.name} on line {node.line} must contain exactly one {name!r} node")
    return matches[0]


def child_nodes(node: Node, name: str) -> list[Node]:
    return [child for child in node.children if child.name == name]


def required_prop(node: Node, key: str, typ: type | tuple[type, ...]) -> Any:
    if key not in node.props:
        fail(f"{node.name} on line {node.line} missing property {key!r}")
    value = node.props[key]
    if not isinstance(value, typ):
        fail(f"{node.name}.{key} on line {node.line} has wrong type")
    return value


def optional_prop(node: Node, key: str, typ: type | tuple[type, ...], default: Any) -> Any:
    if key not in node.props:
        return default
    value = node.props[key]
    if not isinstance(value, typ):
        fail(f"{node.name}.{key} on line {node.line} has wrong type")
    return value


def required_number(node: Node, key: str) -> float:
    value = required_prop(node, key, (int, float))
    value = float(value)
    if not math.isfinite(value):
        fail(f"{node.name}.{key} on line {node.line} must be finite")
    return value


def required_arg(node: Node, index: int, typ: type | tuple[type, ...]) -> Any:
    if len(node.args) <= index:
        fail(f"{node.name} on line {node.line} missing argument {index}")
    value = node.args[index]
    if not isinstance(value, typ):
        fail(f"{node.name} argument {index} on line {node.line} has wrong type")
    return value


def c_string(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def c_float(value: float) -> str:
    text = f"{value:.10g}"
    if "." not in text and "e" not in text.lower():
        text += ".0"
    return text + "f"


def c_uint(value: int) -> str:
    return f"0x{value:08x}u"


def parse_color(node: Node, key: str) -> int:
    value = required_prop(node, key, str)
    if not re.fullmatch(r"#[0-9a-fA-F]{6}", value):
        fail(f"{node.name}.{key} on line {node.line} must be #rrggbb")
    return int(value[1:], 16)


def validate_gui_rect(node: Node) -> dict[str, float]:
    return {
        "x": required_number(node, "x"),
        "y": required_number(node, "y"),
        "w": required_number(node, "w"),
        "h": required_number(node, "h"),
    }


def resolve_metadata_label(amp_slug: str, label: str, metadata: dict[str, str]) -> str:
    if not label.startswith("$"):
        return label
    key = label[1:]
    if key not in metadata:
        fail(f"{amp_slug} gui label {label!r} has no matching metadata field")
    return metadata[key]


def validate_gui(amp_slug: str, gui_node: Node,
                 metadata: dict[str, str],
                 controls_by_key: dict[str, dict[str, Any]]) -> dict[str, Any]:
    width = int(required_number(gui_node, "width"))
    height = int(required_number(gui_node, "height"))
    if width <= 0 or height <= 0:
        fail(f"{amp_slug} gui width/height must be positive")
    default_screen = required_prop(gui_node, "default_screen", str)
    if default_screen not in KNOWN_GUI_SCREENS:
        fail(f"{amp_slug} gui has unknown default_screen {default_screen!r}")

    theme_node = node_child(gui_node, "theme")
    theme = {field: parse_color(theme_node, field) for field in GUI_THEME_FIELDS}

    screens: list[dict[str, Any]] = []
    seen_screens: set[str] = set()
    for screen in child_nodes(gui_node, "screen"):
        key = required_arg(screen, 0, str)
        if key in seen_screens:
            fail(f"{amp_slug} duplicate gui screen {key!r}")
        if key not in KNOWN_GUI_SCREENS:
            fail(f"{amp_slug} unknown gui screen {key!r}")
        seen_screens.add(key)
        screen_id = required_prop(screen, "id", str)
        if screen_id != KNOWN_GUI_SCREENS[key]:
            fail(f"{amp_slug} gui screen {key!r} id must be {KNOWN_GUI_SCREENS[key]}")

        widgets: list[dict[str, Any]] = []
        for widget in screen.children:
            if widget.name not in KNOWN_GUI_WIDGET_TYPES:
                fail(f"{amp_slug} gui screen {key!r} has unknown widget {widget.name!r}")
            label = ""
            param_id = "NILAMP_PARAM_COUNT"
            target = KNOWN_GUI_SCREENS[key]
            text_style = "NILAMP_GUI_TEXT_NORMAL"
            knob_style = "NILAMP_GUI_KNOB_DISPLAY_PERCENT"
            radius = 0.0
            rect = validate_gui_rect(widget)

            if widget.name in {"text", "button", "panel"}:
                label = resolve_metadata_label(
                    amp_slug, required_arg(widget, 0, str), metadata)
            if widget.name in {"knob", "enum"}:
                bind = required_prop(widget, "bind", str)
                if bind not in controls_by_key:
                    fail(f"{amp_slug} gui widget bind={bind!r} has no matching control")
                param_id = controls_by_key[bind]["id"]
            if widget.name == "text":
                style = required_prop(widget, "style", str)
                if style not in KNOWN_GUI_TEXT_STYLES:
                    fail(f"{amp_slug} text widget has unknown style {style!r}")
                text_style = KNOWN_GUI_TEXT_STYLES[style]
            if widget.name == "button":
                target_key = required_prop(widget, "target", str)
                if target_key not in KNOWN_GUI_SCREENS:
                    fail(f"{amp_slug} button target {target_key!r} is unknown")
                target = KNOWN_GUI_SCREENS[target_key]
            if widget.name == "knob":
                radius = required_number(widget, "radius")
                style = required_prop(widget, "style", str)
                if style not in KNOWN_GUI_KNOB_STYLES:
                    fail(f"{amp_slug} knob widget has unknown style {style!r}")
                knob_style = KNOWN_GUI_KNOB_STYLES[style]

            widgets.append({
                "type": KNOWN_GUI_WIDGET_TYPES[widget.name],
                "screen": KNOWN_GUI_SCREENS[key],
                "label": label,
                "param_id": param_id,
                "rect": rect,
                "text_style": text_style,
                "knob_style": knob_style,
                "radius": radius,
                "target": target,
            })

        screens.append({
            "key": key,
            "id": KNOWN_GUI_SCREENS[key],
            "title": required_prop(screen, "title", str),
            "widgets": widgets,
        })

    if default_screen not in seen_screens:
        fail(f"{amp_slug} gui default_screen {default_screen!r} has no screen")

    return {
        "width": width,
        "height": height,
        "default_screen": KNOWN_GUI_SCREENS[default_screen],
        "theme": theme,
        "screens": screens,
    }


def validate_amp(node: Node) -> dict[str, Any]:
    if node.name != "amp":
        fail(f"top-level node on line {node.line} must be 'amp'")
    slug = required_arg(node, 0, str)
    metadata = node_child(node, "metadata")
    dsp = node_child(node, "dsp")
    model_id = required_prop(metadata, "id", str)
    name = required_prop(metadata, "name", str)
    family = required_prop(metadata, "family", str)
    brand = required_prop(metadata, "brand", str)
    topology = required_prop(dsp, "topology", str)
    clap_name = required_prop(metadata, "clap_name", str)
    clap_filename = required_prop(metadata, "clap_filename", str)
    metadata_values = {
        "id": model_id,
        "name": name,
        "family": family,
        "brand": brand,
        "clap_name": clap_name,
        "clap_filename": clap_filename,
        "about_title": required_prop(metadata, "about_title", str),
        "version": required_prop(metadata, "version", str),
        "author": required_prop(metadata, "author", str),
        "port": required_prop(metadata, "port", str),
    }
    if topology not in KNOWN_TOPOLOGIES:
        fail(f"unknown topology {topology!r} for {slug}")
    if not clap_name:
        fail(f"{slug} clap_name must not be empty")
    if not CLAP_FILENAME_RE.fullmatch(clap_filename):
        fail(f"{slug} clap_filename must be a bare .clap filename")

    speaker = node_child(dsp, "speaker")
    speaker_source = required_number(speaker, "source_ohms")
    speaker_nominal = required_number(speaker, "nominal_ohms")

    stages: dict[str, dict[str, Any]] = {}
    for stage in child_nodes(dsp, "stage"):
        stage_name = required_arg(stage, 0, str)
        if stage_name not in STAGE_ORDER:
            fail(f"unknown stage {stage_name!r}")
        if stage_name in stages:
            fail(f"duplicate stage {stage_name!r}")
        kind = required_prop(stage, "kind", str)
        method = required_prop(stage, "method", str)
        table = required_prop(stage, "table", str)
        if kind not in KNOWN_KINDS:
            fail(f"unknown stage kind {kind!r}")
        if method not in KNOWN_METHODS:
            fail(f"unknown stage method {method!r}")
        if table not in KNOWN_TABLES:
            fail(f"unknown ADNL table {table!r}")

        coeffs = node_child(stage, "coeffs")
        nonlinear = node_child(stage, "nonlinear")
        peak = node_child(stage, "peak")
        stages[stage_name] = {
            "symbol": required_prop(stage, "symbol", str),
            "table": table,
            "len": int(required_number(stage, "len")),
            "kpre": required_number(coeffs, "kpre"),
            "isat": required_number(coeffs, "isat"),
            "rl": required_number(coeffs, "rl"),
            "rkl": required_number(coeffs, "rkl"),
            "kspre": required_number(nonlinear, "kspre"),
            "kspost": required_number(nonlinear, "kspost"),
            "ksva": required_number(nonlinear, "ksva"),
            "ksvk": required_number(nonlinear, "ksvk"),
            "ksib": required_number(nonlinear, "ksib"),
            "kfb": required_number(peak, "kfb"),
            "kpk": required_number(peak, "kpk"),
            "pk_xth": required_number(peak, "xth"),
            "pk_xdiode": required_number(peak, "xdiode"),
            "pk_attack": required_number(peak, "attack"),
            "pk_release": required_number(peak, "release"),
            "avg_tau": required_number(peak, "avg_tau"),
        }

    for stage_name in STAGE_ORDER:
        if stage_name not in stages:
            fail(f"{slug} missing stage {stage_name}")

    supply = node_child(dsp, "supply")
    supply_method = required_prop(supply, "method", str)
    if supply_method not in KNOWN_METHODS:
        fail(f"unknown supply method {supply_method!r}")
    supply_nodes = {required_arg(n, 0, str): n for n in child_nodes(supply, "node")}
    for supply_name in ("p1", "p2", "p3"):
        if supply_name not in supply_nodes:
            fail(f"{slug} missing supply node {supply_name}")

    process = node_child(dsp, "process")
    process_values = {
        "input_feed_gain": required_number(process, "input_feed_gain"),
        "input_keller_gain_sq": required_number(process, "input_keller_gain_sq"),
        "pss1_r": required_number(supply_nodes["p1"], "r"),
        "pss1_tau": required_number(supply_nodes["p1"], "tau"),
        "pss2_r": required_number(supply_nodes["p2"], "r"),
        "pss2_tau": required_number(supply_nodes["p2"], "tau"),
        "pss3_r_at_full_sag": required_number(supply_nodes["p3"], "r_at_full_sag"),
        "pss3_tau": required_number(supply_nodes["p3"], "tau"),
        "phase_t4_gain": required_number(process, "phase_t4_gain"),
        "phase_t5_gain": required_number(process, "phase_t5_gain"),
        "screen_current_feedback": required_number(process, "screen_current_feedback"),
    }

    controls_node = node_child(dsp, "controls")
    controls: list[dict[str, Any]] = []
    controls_by_key: dict[str, dict[str, Any]] = {}
    seen_ids: set[str] = set()
    for control in child_nodes(controls_node, "control"):
        key = required_arg(control, 0, str)
        if key in controls_by_key:
            fail(f"{slug} duplicate control key {key!r}")
        control_id = required_prop(control, "id", str)
        if control_id in seen_ids:
            fail(f"{slug} duplicate control id {control_id!r}")
        seen_ids.add(control_id)
        display = required_prop(control, "display", str)
        if display not in KNOWN_CONTROL_DISPLAYS:
            fail(f"{slug} control {key!r} has unknown display {display!r}")
        options = [required_arg(option, 0, str) for option in child_nodes(control, "option")]
        minimum = required_number(control, "min")
        maximum = required_number(control, "max")
        default = required_number(control, "default")
        if minimum > maximum:
            fail(f"{slug} control {key!r} min exceeds max")
        if default < minimum or default > maximum:
            fail(f"{slug} control {key!r} default outside range")
        if display == "enum":
            if not options:
                fail(f"{slug} enum control {key!r} must contain option children")
            if minimum != 0.0 or maximum != float(len(options) - 1):
                fail(f"{slug} enum control {key!r} range must be 0..option_count-1")
            if default != float(int(default)) or required_number(control, "step") != 1.0:
                fail(f"{slug} enum control {key!r} default/step must be integer and step=1")
        elif options:
            fail(f"{slug} non-enum control {key!r} must not contain options")
        control_data = {
            "key": key,
            "id": control_id,
            "name": required_prop(control, "name", str),
            "module": required_prop(control, "module", str),
            "unit": required_prop(control, "unit", str),
            "min": minimum,
            "max": maximum,
            "default": default,
            "step": required_number(control, "step"),
            "display": KNOWN_CONTROL_DISPLAYS[display],
            "options": options,
        }
        controls.append(control_data)
        controls_by_key[key] = control_data

    gui = validate_gui(slug, node_child(node, "gui"), metadata_values, controls_by_key)

    return {
        "slug": slug,
        "id": model_id,
        "name": name,
        "family": family,
        "topology": topology,
        "clap_name": clap_name,
        "clap_filename": clap_filename,
        "speaker_source_ohms": speaker_source,
        "speaker_nominal_ohms": speaker_nominal,
        "stages": stages,
        "process": process_values,
        "controls": controls,
        "gui": gui,
    }


def parse_model(path: Path) -> dict[str, Any]:
    nodes = Parser(tokenize(path.read_text())).parse()
    if len(nodes) != 1:
        fail(f"{path} must contain exactly one top-level amp node")
    return validate_amp(nodes[0])


def render(models: list[dict[str, Any]]) -> str:
    lines: list[str] = [
        "/* SPDX-License-Identifier: MIT */",
        "/* Generated by tools/gen_amp_models.py; do not hand-edit. */",
        "",
    ]
    for model in models:
        lines.append(f"/* {model['name']} */")
        for stage_name in STAGE_ORDER:
            stage = model["stages"][stage_name]
            lines.append(f"static const StageCfg {stage['symbol']} = {{")
            lines.append(
                f"    {stage['table']}, {stage['len']}, {c_float(stage['kpre'])}, "
                f"{c_float(stage['isat'])}, {c_float(stage['rl'])}, {c_float(stage['rkl'])},"
            )
            lines.append(
                f"    {c_float(stage['kspre'])}, {c_float(stage['kspost'])}, "
                f"{c_float(stage['ksva'])}, {c_float(stage['ksvk'])}, {c_float(stage['ksib'])},"
            )
            lines.append(
                f"    {c_float(stage['kfb'])}, {c_float(stage['kpk'])}, "
                f"{c_float(stage['pk_xth'])}, {c_float(stage['pk_xdiode'])}, "
                f"{c_float(stage['pk_attack'])}, {c_float(stage['pk_release'])}, "
                f"{c_float(stage['avg_tau'])},"
            )
            lines.append("};")
        lines.append("")
        lines.append("static const NilampTwdDlxIiData TWD_DLX_II_DATA = {")
        for stage_name in STAGE_ORDER:
            lines.append(f"    .{stage_name} = &{model['stages'][stage_name]['symbol']},")
        for field_name in PROCESS_FIELDS:
            lines.append(f"    .{field_name} = {c_float(model['process'][field_name])},")
        lines.append("};")
        lines.append("")
        for control in model["controls"]:
            if not control["options"]:
                continue
            lines.append(
                f"static const char *const {model['slug'].upper()}_{control['key'].upper()}_OPTIONS[] = {{"
            )
            for option in control["options"]:
                lines.append(f"    {c_string(option)},")
            lines.append("};")
            lines.append("")
        lines.append(f"static const NilampControlSpec {model['slug'].upper()}_CONTROLS[] = {{")
        for control in model["controls"]:
            lines.append("    {")
            lines.append(f"        .id = {control['id']},")
            lines.append(f"        .name = {c_string(control['name'])},")
            lines.append(f"        .module = {c_string(control['module'])},")
            lines.append(f"        .unit = {c_string(control['unit'])},")
            lines.append(f"        .min_value = {c_float(control['min'])},")
            lines.append(f"        .max_value = {c_float(control['max'])},")
            lines.append(f"        .default_value = {c_float(control['default'])},")
            lines.append(f"        .step = {c_float(control['step'])},")
            lines.append(f"        .display = {control['display']},")
            if control["options"]:
                symbol = f"{model['slug'].upper()}_{control['key'].upper()}_OPTIONS"
                lines.append(f"        .enum_names = {symbol},")
                lines.append(f"        .enum_count = (uint32_t)(sizeof({symbol}) / sizeof({symbol}[0])),")
            else:
                lines.append("        .enum_names = NULL,")
                lines.append("        .enum_count = 0u,")
            lines.append("    },")
        lines.append("};")
        lines.append("")
        for screen in model["gui"]["screens"]:
            symbol = f"{model['slug'].upper()}_{screen['key'].upper()}_GUI_WIDGETS"
            lines.append(f"static const NilampGuiWidgetSpec {symbol}[] = {{")
            for widget in screen["widgets"]:
                rect = widget["rect"]
                lines.append("    {")
                lines.append(f"        .type = {widget['type']},")
                lines.append(f"        .screen = {widget['screen']},")
                lines.append(f"        .label = {c_string(widget['label'])},")
                lines.append(f"        .param_id = {widget['param_id']},")
                lines.append(
                    "        .bounds = {"
                    f"{c_float(rect['x'])}, {c_float(rect['y'])}, "
                    f"{c_float(rect['w'])}, {c_float(rect['h'])}" + "},"
                )
                lines.append(f"        .text_style = {widget['text_style']},")
                lines.append(f"        .knob_display = {widget['knob_style']},")
                lines.append(f"        .radius = {c_float(widget['radius'])},")
                lines.append(f"        .target_screen = {widget['target']},")
                lines.append("    },")
            lines.append("};")
            lines.append("")
        screens_symbol = f"{model['slug'].upper()}_GUI_SCREENS"
        lines.append(f"static const NilampGuiScreenSpec {screens_symbol}[] = {{")
        for screen in model["gui"]["screens"]:
            widget_symbol = f"{model['slug'].upper()}_{screen['key'].upper()}_GUI_WIDGETS"
            lines.append("    {")
            lines.append(f"        .id = {screen['id']},")
            lines.append(f"        .title = {c_string(screen['title'])},")
            lines.append(f"        .widgets = {widget_symbol},")
            lines.append(
                f"        .widget_count = (uint32_t)(sizeof({widget_symbol}) / sizeof({widget_symbol}[0])),"
            )
            lines.append("    },")
        lines.append("};")
        lines.append("")
        layout_symbol = f"{model['slug'].upper()}_GUI_LAYOUT"
        theme = model["gui"]["theme"]
        lines.append(f"static const NilampGuiLayoutSpec {layout_symbol} = {{")
        lines.append(f"    .design_width = {model['gui']['width']}u,")
        lines.append(f"    .design_height = {model['gui']['height']}u,")
        lines.append(f"    .default_screen = {model['gui']['default_screen']},")
        lines.append("    .theme = {")
        for field in GUI_THEME_FIELDS:
            lines.append(f"        .{field} = {c_uint(theme[field])},")
        lines.append("    },")
        lines.append(f"    .screens = {screens_symbol},")
        lines.append(
            f"    .screen_count = (uint32_t)(sizeof({screens_symbol}) / sizeof({screens_symbol}[0])),"
        )
        lines.append("};")
        lines.append("")

    lines.append("static const NilampModelSpec NILAMP_MODELS[] = {")
    for model in models:
        layout_symbol = f"{model['slug'].upper()}_GUI_LAYOUT"
        lines.append("    {")
        lines.append(f"        .id = {model['id']},")
        lines.append(f"        .name = {c_string(model['name'])},")
        lines.append(f"        .family = {c_string(model['family'])},")
        lines.append(f"        .clap_name = {c_string(model['clap_name'])},")
        lines.append(f"        .clap_filename = {c_string(model['clap_filename'])},")
        lines.append(f"        .speaker_source_ohms = {c_float(model['speaker_source_ohms'])},")
        lines.append(f"        .speaker_nominal_ohms = {c_float(model['speaker_nominal_ohms'])},")
        lines.append(f"        .gui_layout = &{layout_symbol},")
        lines.append("    },")
    lines.append("};")
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--print-clap-filename", action="store_true",
                        help="print the configured CLAP bundle filename for one model")
    parser.add_argument("--print-clap-name-c", action="store_true",
                        help="print the configured CLAP descriptor name as a C string")
    parser.add_argument("paths", type=Path, nargs="+")
    args = parser.parse_args()

    if args.print_clap_filename or args.print_clap_name_c:
        if len(args.paths) != 1:
            fail("metadata print mode expects exactly one model path")
        model = parse_model(args.paths[0])
        print(c_string(model["clap_name"]) if args.print_clap_name_c else model["clap_filename"])
        return 0

    if len(args.paths) < 2:
        fail("missing model path")

    output = args.paths[0]
    models = [parse_model(path) for path in args.paths[1:]]
    models.sort(key=lambda model: model["id"])
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(render(models))
    return 0


if __name__ == "__main__":
    sys.exit(main())
