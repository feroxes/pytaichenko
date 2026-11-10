import json
import math

from voice_hub.prompts import end_tool_description

VOLUME_TOOL_NAME = "set_volume"
END_TOOL_NAME = "end_conversation"

VOLUME_STEP = 0.15
MIN_VOLUME = 0.1
MAX_VOLUME = 1.0

VOLUME_TOOL: dict = {
    "type": "function",
    "name": VOLUME_TOOL_NAME,
    "description": (
        "Change the speaker volume of this device. Call it when the user asks to make the sound "
        "louder or quieter, or to set a specific volume level."
    ),
    "parameters": {
        "type": "object",
        "properties": {
            "action": {
                "type": "string",
                "enum": ["up", "down", "set"],
                "description": "up = a bit louder, down = a bit quieter, set = exact level.",
            },
            "level": {
                "type": "integer",
                "minimum": 0,
                "maximum": 100,
                "description": "Target volume in percent. Required when action is set.",
            },
        },
        "required": ["action"],
    },
}


def end_tool(language: str) -> dict:
    return {
        "type": "function",
        "name": END_TOOL_NAME,
        "description": end_tool_description(language),
        "parameters": {"type": "object", "properties": {}, "required": []},
    }


def session_tools(language: str) -> list[dict]:
    return [VOLUME_TOOL, end_tool(language)]


def next_volume(current: float, arguments: str) -> float:
    try:
        args = json.loads(arguments)
    except json.JSONDecodeError as e:
        raise ValueError(f"set_volume arguments are not valid JSON: {e}") from e
    if not isinstance(args, dict):
        raise ValueError("set_volume arguments must be a JSON object")
    action = args.get("action")
    if action == "up":
        target = current + VOLUME_STEP
    elif action == "down":
        target = current - VOLUME_STEP
    elif action == "set":
        level = args.get("level")
        if isinstance(level, bool) or not isinstance(level, (int, float)) or not math.isfinite(level):
            raise ValueError("set_volume needs a numeric level for action set")
        target = level / 100
    else:
        raise ValueError(f"unknown set_volume action: {action!r}")
    return round(min(MAX_VOLUME, max(MIN_VOLUME, target)), 2)
