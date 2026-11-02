import pytest

from voice_hub.prompts import LANGUAGES, end_tool_description
from voice_hub.tools import END_TOOL_NAME, VOLUME_TOOL, VOLUME_TOOL_NAME, end_tool, next_volume, session_tools


def test_volume_tool_schema():
    assert VOLUME_TOOL["type"] == "function" and VOLUME_TOOL["name"] == VOLUME_TOOL_NAME == "set_volume"
    assert VOLUME_TOOL["description"]
    params = VOLUME_TOOL["parameters"]
    assert params["type"] == "object" and params["required"] == ["action"]
    assert params["properties"]["action"]["enum"] == ["up", "down", "set"]
    level = params["properties"]["level"]
    assert level["type"] == "integer" and level["minimum"] == 0 and level["maximum"] == 100


@pytest.mark.parametrize("language", LANGUAGES)
def test_end_tool_schema(language):
    tool = end_tool(language)
    assert tool["type"] == "function" and tool["name"] == END_TOOL_NAME == "end_conversation"
    assert tool["description"] == end_tool_description(language)
    assert tool["parameters"] == {"type": "object", "properties": {}, "required": []}


@pytest.mark.parametrize("language", LANGUAGES)
def test_session_tools_follow_the_language(language):
    tools = session_tools(language)
    assert [tool["name"] for tool in tools] == ["set_volume", "end_conversation"]
    assert tools[0] == VOLUME_TOOL and tools[1] == end_tool(language)


def test_end_tool_rejects_unknown_language():
    with pytest.raises(ValueError):
        end_tool("de")


def test_next_volume():
    assert next_volume(0.7, '{"action":"down"}') == 0.55
    assert next_volume(0.95, '{"action":"up"}') == 1.0
    assert next_volume(0.7, '{"action":"set","level":50}') == 0.5
    assert next_volume(0.7, '{"action":"set","level":0}') == 0.1
    with pytest.raises(ValueError):
        next_volume(0.7, '{"action":"louder"}')


@pytest.mark.parametrize(
    ("current", "arguments", "expected"),
    [
        (0.4, '{"action":"up"}', 0.55),
        (0.1, '{"action":"down"}', 0.1),
        (0.2, '{"action":"down"}', 0.1),
        (0.5, '{"action":"set","level":100}', 1.0),
        (0.5, '{"action":"set","level":250}', 1.0),
        (0.5, '{"action":"set","level":-5}', 0.1),
        (0.5, '{"action":"set","level":33}', 0.33),
        (0.5, '{"action":"set","level":42.4}', 0.42),
        (0.5, '{"action":"up","level":10}', 0.65),
    ],
)
def test_next_volume_clamps_and_rounds(current, arguments, expected):
    assert next_volume(current, arguments) == expected


@pytest.mark.parametrize(
    "arguments",
    [
        "",
        "not json",
        "[]",
        '"up"',
        "{}",
        '{"action":null}',
        '{"action":"UP"}',
        '{"action":"set"}',
        '{"action":"set","level":null}',
        '{"action":"set","level":"50"}',
        '{"action":"set","level":true}',
        '{"action":"set","level":NaN}',
        '{"action":"set","level":1e999}',
    ],
)
def test_next_volume_rejects_bad_arguments(arguments):
    with pytest.raises(ValueError):
        next_volume(0.5, arguments)
