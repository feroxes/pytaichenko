import json
import logging

from voice_hub.realtime import ToolCall
from voice_hub.tools import END_TOOL_NAME, VOLUME_TOOL_NAME, next_volume
from voice_hub.voice_events import DevicePort

log = logging.getLogger(__name__)


def ends_conversation(calls: tuple[ToolCall, ...]) -> bool:
    return any(call.name == END_TOOL_NAME for call in calls)


def run_tools(device: DevicePort, calls: tuple[ToolCall, ...]) -> list[tuple[str, str]]:
    outputs = []
    volume = device.volume
    for call in calls:
        if call.name == END_TOOL_NAME:
            outputs.append((call.call_id, json.dumps({"ok": True})))
            continue
        if call.name != VOLUME_TOOL_NAME:
            outputs.append((call.call_id, json.dumps({"error": f"unknown tool {call.name}"})))
            continue
        try:
            volume = next_volume(volume, call.arguments)
        except ValueError as e:
            outputs.append((call.call_id, json.dumps({"error": str(e)})))
            continue
        device.set_volume(volume)
        log.info("volume on %s set to %.2f", device.name, volume)
        outputs.append((call.call_id, json.dumps({"volume": round(volume * 100)})))
    return outputs
