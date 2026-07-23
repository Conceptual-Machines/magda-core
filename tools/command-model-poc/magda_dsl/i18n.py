"""Multilingual seed phrase banks for the command-model POC.

Locale set mirrors MAGDA's shipped translations: en, ja, ru, zh.

Design decisions (see README "Multilingual"):
  - The DSL OUTPUT is language-invariant. Every entry maps a localized request
    to the SAME canonical plan it would have in English (English track names,
    <alias> plugin tokens, #hex colours). This is the whole reason one
    multilingual model beats per-language models: the expensive half (valid
    DSL structure) is shared.
  - English is generated procedurally (dataset/generate.py) for volume.
    Non-English locales use these CURATED seed banks: small, grammatically
    hand-checked, and meant to be EXPANDED by the paraphrase teacher in Phase 3
    rather than scaled by hand (consistent with the Crowdin "humans/LLM do
    translations, I seed" rule).

Each entry is (natural_language, actions). `actions` uses the same schema as
dataset/generate.py and is rendered by magda_dsl.dsl.render.
"""
from __future__ import annotations

from . import vocab

T = vocab.resolve_plugin
C = vocab.COLORS

LANGS = ["en", "ja", "ru", "zh"]

# ---------------------------------------------------------------------------
# Japanese seeds
# ---------------------------------------------------------------------------
JA = [
    ("ベーストラックを作成", [{"type": "create_track", "name": "Bass"}]),
    ("新しいドラムトラックを追加", [{"type": "create_track", "name": "Drums"}]),
    ("リードトラックを作って", [{"type": "create_track", "name": "Lead"}]),
    ("SerumとOTTを入れたベーストラックを作成",
     [{"type": "create_track", "name": "Bass", "plugins": [T("serum"), T("ott")]}]),
    ("ベーストラックにSerumを追加", [{"type": "add_plugin", "name": "Bass", "plugin": T("serum")}]),
    ("ドラムにOTTを入れて", [{"type": "add_plugin", "name": "Drums", "plugin": T("ott")}]),
    ("ボーカルにPro-Q 3を追加", [{"type": "add_plugin", "name": "Vocals", "plugin": T("pro-q 3")}]),
    ("ドラムをミュート", [{"type": "mute_track", "name": "Drums"}]),
    ("ベーストラックをミュート", [{"type": "mute_track", "name": "Bass"}]),
    ("ドラムをソロ", [{"type": "solo_track", "name": "Drums"}]),
    ("ベーストラックを削除", [{"type": "delete_track", "name": "Bass"}]),
    ("Padsを消して", [{"type": "delete_track", "name": "Pads"}]),
    ("ドラムを赤にする", [{"type": "set_track_color", "name": "Drums", "colour": C["red"]}]),
    ("ベーストラックを青にして", [{"type": "set_track_color", "name": "Bass", "colour": C["blue"]}]),
    ("BassトラックをReese Bassに名前変更",
     [{"type": "rename_track", "name": "Bass", "new_name": "Reese Bass"}]),
]

# ---------------------------------------------------------------------------
# Russian seeds
# ---------------------------------------------------------------------------
RU = [
    ("создай басовую дорожку", [{"type": "create_track", "name": "Bass"}]),
    ("добавь дорожку барабанов", [{"type": "create_track", "name": "Drums"}]),
    ("новая дорожка Lead", [{"type": "create_track", "name": "Lead"}]),
    ("создай басовую дорожку с Serum и OTT",
     [{"type": "create_track", "name": "Bass", "plugins": [T("serum"), T("ott")]}]),
    ("добавь Serum на басовую дорожку", [{"type": "add_plugin", "name": "Bass", "plugin": T("serum")}]),
    ("поставь OTT на барабаны", [{"type": "add_plugin", "name": "Drums", "plugin": T("ott")}]),
    ("добавь Pro-Q 3 на вокал", [{"type": "add_plugin", "name": "Vocals", "plugin": T("pro-q 3")}]),
    ("заглуши барабаны", [{"type": "mute_track", "name": "Drums"}]),
    ("замьютить басовую дорожку", [{"type": "mute_track", "name": "Bass"}]),
    ("соло барабаны", [{"type": "solo_track", "name": "Drums"}]),
    ("удали басовую дорожку", [{"type": "delete_track", "name": "Bass"}]),
    ("удали дорожку Pads", [{"type": "delete_track", "name": "Pads"}]),
    ("сделай барабаны красными", [{"type": "set_track_color", "name": "Drums", "colour": C["red"]}]),
    ("покрась басовую дорожку в синий", [{"type": "set_track_color", "name": "Bass", "colour": C["blue"]}]),
    ("переименуй дорожку Bass в Reese Bass",
     [{"type": "rename_track", "name": "Bass", "new_name": "Reese Bass"}]),
]

# ---------------------------------------------------------------------------
# Chinese (Simplified) seeds
# ---------------------------------------------------------------------------
ZH = [
    ("创建一个贝斯轨道", [{"type": "create_track", "name": "Bass"}]),
    ("添加一个鼓轨道", [{"type": "create_track", "name": "Drums"}]),
    ("新建一个Lead轨道", [{"type": "create_track", "name": "Lead"}]),
    ("创建一个带Serum和OTT的贝斯轨道",
     [{"type": "create_track", "name": "Bass", "plugins": [T("serum"), T("ott")]}]),
    ("在贝斯轨道上添加Serum", [{"type": "add_plugin", "name": "Bass", "plugin": T("serum")}]),
    ("给鼓轨道加上OTT", [{"type": "add_plugin", "name": "Drums", "plugin": T("ott")}]),
    ("在人声轨道添加Pro-Q 3", [{"type": "add_plugin", "name": "Vocals", "plugin": T("pro-q 3")}]),
    ("静音鼓", [{"type": "mute_track", "name": "Drums"}]),
    ("把贝斯轨道静音", [{"type": "mute_track", "name": "Bass"}]),
    ("独奏鼓", [{"type": "solo_track", "name": "Drums"}]),
    ("删除贝斯轨道", [{"type": "delete_track", "name": "Bass"}]),
    ("删除Pads轨道", [{"type": "delete_track", "name": "Pads"}]),
    ("把鼓设为红色", [{"type": "set_track_color", "name": "Drums", "colour": C["red"]}]),
    ("将贝斯轨道设为蓝色", [{"type": "set_track_color", "name": "Bass", "colour": C["blue"]}]),
    ("把Bass轨道重命名为Reese Bass",
     [{"type": "rename_track", "name": "Bass", "new_name": "Reese Bass"}]),
]

CURATED = {"ja": JA, "ru": RU, "zh": ZH}


def curated(lang: str):
    return CURATED.get(lang, [])
