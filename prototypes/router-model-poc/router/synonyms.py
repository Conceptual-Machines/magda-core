"""Synonym banks — the cheap half of phrasing diversity.

Templates alone teach the model a closed vocabulary: on the first pass the
router scored 100% on a val split drawn from the same templates and 59.6% on
the hand-authored held-out set, because unseen wordings ("chuck an eq on...",
"wipe the curves off...") arrive as a wall of <UNK>. Word dropout makes the
model robust to *some* unknown tokens; it cannot invent the ones it never saw.

So each language gets a bank of domain synonyms, applied to the template text
before slot substitution (placeholders are protected, pools stay canonical).
One template becomes dozens of phrasings, and the vocabulary grows to cover the
words producers actually type rather than the ones the template author reached
for first.

Keys are matched whole-word for ASCII and as plain substrings for CJK, which has
no word boundaries. Entries are ordinary domain vocabulary, not a list tuned
against the test set — see findings.md on why that distinction matters here.
"""
from __future__ import annotations

EN = {
    "create": ["create", "make", "add", "build", "start", "set up", "spin up",
               "give me", "open up", "new"],
    "add": ["add", "put", "insert", "drop", "throw", "stick", "load", "chuck",
            "slap", "bring in"],
    "delete": ["delete", "remove", "get rid of", "kill", "trash", "drop",
               "clear out", "wipe", "take out"],
    "track": ["track", "channel"],
    "clip": ["clip", "region", "part", "section"],
    "the": ["the", "my", "that", "this"],
    "mute": ["mute", "silence", "kill the sound on"],
    "write": ["write", "compose", "generate", "come up with", "sketch out",
              "lay down", "put together"],
    "suggest": ["suggest", "recommend", "propose", "throw me", "give me",
                "i need", "show me"],
    "chord progression": ["chord progression", "progression", "chord sequence",
                          "set of chords", "chords", "changes"],
    "melody": ["melody", "line", "tune", "hook", "topline"],
    "bassline": ["bassline", "bass line", "bass part", "low end line"],
    "automate": ["automate", "draw automation on", "modulate", "animate",
                 "move", "ride"],
    "lfo": ["lfo", "modulation", "wobble", "movement"],
    "beat": ["beat", "groove", "pattern", "rhythm", "drum part"],
    "drums": ["drums", "kit", "drum pattern", "drum track"],
    "fill": ["fill", "roll", "turnaround", "break"],
    "mix": ["mix", "balance", "whole thing", "track"],
    "muddy": ["muddy", "cloudy", "boxy", "woolly", "congested"],
    "harsh": ["harsh", "brittle", "piercing", "fatiguing", "spiky"],
    "scene": ["scene", "row", "row of clips"],
    "launch": ["launch", "fire", "trigger", "start", "play", "kick off"],
    "stop": ["stop", "kill", "halt", "cut", "shut off"],
    "volume": ["volume", "level", "gain"],
    "analyze": ["analyze", "analyse", "check", "look at", "review", "assess"],
    "how is": ["how is", "how's", "what about", "what do you think of"],
    "bars": ["bars", "measures"],
    "busier": ["busier", "denser", "more active", "more intricate"],
    "sparser": ["sparser", "simpler", "more spacious", "thinner"],
    "loop": ["loop", "cycle", "repeat"],
    "rename": ["rename", "call", "retitle"],
    "set": ["set", "put", "change", "make"],
    "quantize": ["quantize", "snap", "tighten", "lock"],
}

JA = {
    "作成": ["作成", "作って", "追加", "新規作成", "用意して"],
    "追加": ["追加", "入れて", "足して", "挿して", "加えて"],
    "削除": ["削除", "消して", "外して", "取り除いて"],
    "トラック": ["トラック", "チャンネル"],
    "クリップ": ["クリップ", "パート", "区間"],
    "書いて": ["書いて", "作って", "考えて", "組み立てて"],
    "提案して": ["提案して", "教えて", "ちょうだい", "出して"],
    "コード進行": ["コード進行", "進行", "コードの流れ", "和音進行"],
    "メロディー": ["メロディー", "メロディ", "旋律", "ライン"],
    "ビート": ["ビート", "グルーヴ", "リズム", "パターン"],
    "止めて": ["止めて", "停止", "ストップ"],
    "ボリューム": ["ボリューム", "音量", "レベル"],
    "分析して": ["分析して", "見て", "チェックして", "確認して"],
    "どう": ["どう", "どうかな", "どんな感じ", "いい感じ"],
    "ミックス": ["ミックス", "音のバランス", "全体の音"],
    "オートメーション": ["オートメーション", "自動化", "オートメ"],
}

RU = {
    "создай": ["создай", "сделай", "добавь", "заведи", "открой"],
    "добавь": ["добавь", "поставь", "воткни", "закинь", "накинь"],
    "удали": ["удали", "убери", "снеси", "выкинь"],
    "трек": ["трек", "дорожку", "канал"],
    "клип": ["клип", "регион", "кусок"],
    "напиши": ["напиши", "сочини", "придумай", "набросай"],
    "предложи": ["предложи", "покажи", "дай", "подкинь"],
    "аккордовую последовательность": ["аккордовую последовательность",
                                      "последовательность аккордов",
                                      "прогрессию", "гармонию"],
    "мелодию": ["мелодию", "линию", "тему"],
    "бит": ["бит", "грув", "ритм", "паттерн"],
    "останови": ["останови", "выключи", "вырубить", "стопни"],
    "громкость": ["громкость", "уровень", "гейн"],
    "проанализируй": ["проанализируй", "посмотри", "проверь", "оцени"],
    "микс": ["микс", "сведение", "баланс"],
    "автоматизируй": ["автоматизируй", "проведи", "промодулируй"],
}

ZH = {
    "创建": ["创建", "新建", "做一个", "开一条", "加一条"],
    "加": ["加", "放", "挂", "插", "添加"],
    "删除": ["删除", "删掉", "去掉", "移除"],
    "轨道": ["轨道", "音轨", "通道"],
    "片段": ["片段", "区块", "段落"],
    "写": ["写", "编", "做", "想"],
    "给我": ["给我", "来", "推荐", "出"],
    "和弦进行": ["和弦进行", "进行", "和声进行", "和弦走向"],
    "旋律": ["旋律", "曲调", "主题"],
    "鼓点": ["鼓点", "律动", "节奏", "鼓组"],
    "停止": ["停止", "停掉", "关掉", "切断"],
    "音量": ["音量", "电平", "增益"],
    "分析": ["分析", "看看", "检查", "评估"],
    "混音": ["混音", "混缩", "整体声音"],
    "自动化": ["自动化", "自动控制", "包络"],
}

BY_LANG = {"en": EN, "ja": JA, "ru": RU, "zh": ZH}
