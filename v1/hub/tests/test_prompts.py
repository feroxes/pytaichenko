import pytest

from voice_hub.prompts import LANGUAGES, default_instructions, end_tool_description

NAMES = ("jarvis", "джарвіс")


def test_languages():
    assert LANGUAGES == ("uk", "en")


@pytest.mark.parametrize("language", LANGUAGES)
def test_default_instructions_keep_the_behaviour_rules(language):
    text = default_instructions(language)
    assert isinstance(text, str) and text.strip()
    assert "end_conversation" in text


def test_ukrainian_instructions_carry_every_rule():
    text = default_instructions("uk")
    for rule in (
        "Відповідай українською одразу по суті",
        "одне-два коротких речення",
        "спитай, чи розповісти детальніше",
        "нічого не коментуй до виконання",
        "«дякую, все»",
        "люди говорять між собою",
        "виклич end_conversation і нічого не кажи",
        "мікрофон після неї ще кілька секунд слухає продовження",
        "«якщо хочеш, можу…»",
        "питань у відповідь",
    ):
        assert rule in text


def test_english_instructions_carry_every_rule():
    text = default_instructions("en")
    for rule in (
        "Answer in English",
        "one or two short sentences",
        "ask whether to go into detail",
        "say nothing before doing it",
        "thanks, that's all",
        "people talking to each other",
        "call end_conversation and say nothing",
        "the microphone keeps listening",
        "if you want, I can",
        "no questions back",
    ):
        assert rule in text


@pytest.mark.parametrize("language", LANGUAGES)
def test_default_texts_have_no_assistant_name_or_wake_phrase(language):
    for text in (default_instructions(language), end_tool_description(language)):
        lowered = text.lower()
        assert not any(name in lowered for name in NAMES)


@pytest.mark.parametrize("language", LANGUAGES)
def test_end_tool_description(language):
    text = end_tool_description(language)
    assert isinstance(text, str) and text.strip()


def test_languages_have_their_own_texts():
    assert default_instructions("uk") != default_instructions("en")
    assert end_tool_description("uk") != end_tool_description("en")


@pytest.mark.parametrize("language", ["de", "", "UK", None])
def test_unknown_language_is_rejected(language):
    with pytest.raises(ValueError):
        default_instructions(language)
    with pytest.raises(ValueError):
        end_tool_description(language)
