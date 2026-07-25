"""Build the fixed held-out test set for the router (eval/testset.jsonl).

Hand-authored, never generated. Every case is phrased *differently* from the
training templates in router/seeds_*.py — a test set drawn from the same
template bank would only measure slot-pool memorisation. generate.py refuses to
emit any training row whose text appears here, so the split stays honest.

Cases lean deliberately on the boundaries a coarse router has to get right:
content-only vs content-and-place (MUSIC/BOTH), one-shot value vs curve
(COMMAND/AUTOMATION), DSL edit vs pattern authoring (COMMAND/DRUM).

Each case is tagged **core** or **fuzzy**, because they measure different
things and only one of them is this model's job:

  core  — the request names a concrete operation or object that lands on one
          agent's documented surface (create a track, add an FX, automate a
          named parameter, write a beat, analyse the mix, launch a scene).
          The label is recoverable from the words present. This is the fast
          inference surface, and **core accuracy is the metric**.
  fuzzy — identifying the agent means interpreting a subjective or aesthetic
          judgement with no named operation ("too stiff", "feel like sunday
          morning", "breathe in and out"). Deterministic classification over a
          fixed label set cannot do this by construction; it is the LLM path's
          job. Reported separately, never mixed into the headline.

    python -m eval.make_testset        # writes eval/testset.jsonl (committed)
"""
from __future__ import annotations

import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))

# (text, label, lang)
CASES = [
    # ---- COMMAND ----------------------------------------------------------
    ("spin up a track for the sub bass", "COMMAND", "en"),
    ("get rid of track 3", "COMMAND", "en"),
    ("chuck an eq on the vocal chain", "COMMAND", "en"),
    ("bring the guitar down 4 db", "COMMAND", "en"),
    ("silence everything except the kick", "COMMAND", "en"),
    ("stick the drums in a group", "COMMAND", "en"),
    ("call this track Wide Pad instead", "COMMAND", "en"),
    ("snap the notes to the grid", "COMMAND", "en"),
    ("tint the vocal track orange", "COMMAND", "en"),
    ("nudge the lead track above the pads", "COMMAND", "en"),
    ("ボーカルトラックにコンプを挿して", "COMMAND", "ja"),
    ("パッドのトラックを消して", "COMMAND", "ja"),
    ("キーボードのトラックを緑にして", "COMMAND", "ja"),
    ("убери дорожку с гитарой", "COMMAND", "ru"),
    ("накинь эквалайзер на вокал", "COMMAND", "ru"),
    ("сделай звук баса тише на 6 дБ", "COMMAND", "ru"),
    ("把人声轨道的音量调低一点", "COMMAND", "zh"),
    ("删掉第三条轨道", "COMMAND", "zh"),
    ("给吉他轨挂一个均衡器", "COMMAND", "zh"),
    # ---- MUSIC ------------------------------------------------------------
    ("what would sound good under this melody", "MUSIC", "en"),
    ("i need four chords that feel like sunday morning", "MUSIC", "en"),
    ("throw some voicings at me for a slow ballad", "MUSIC", "en"),
    ("turn these chords into something more open", "MUSIC", "en"),
    ("a progression that resolves to the relative minor", "MUSIC", "en"),
    ("ideas for the pre chorus harmony", "MUSIC", "en"),
    ("このコードの続きを考えて", "MUSIC", "ja"),
    ("もっと切ない響きのコードない", "MUSIC", "ja"),
    ("подкинь пару аккордов под этот мотив", "MUSIC", "ru"),
    ("какая гармония тут подойдёт", "MUSIC", "ru"),
    ("这段旋律配什么和弦好听", "MUSIC", "zh"),
    ("有没有更开放一点的和弦排列", "MUSIC", "zh"),
    # ---- BOTH -------------------------------------------------------------
    ("spin up a rhodes track and drop a soulful loop on it", "BOTH", "en"),
    ("i want a new pad track playing something dreamy", "BOTH", "en"),
    ("lay a walking bass on the bass track", "BOTH", "en"),
    ("make a track for the strings and write a sad progression there", "BOTH", "en"),
    ("stick eight bars of gospel chords on the keys", "BOTH", "en"),
    ("新しいピアノトラックにしっとりした進行を置いて", "BOTH", "ja"),
    ("ベースのトラックにグルーヴィーなラインを書いて", "BOTH", "ja"),
    ("сделай трек для родеса и напиши туда что-нибудь тёплое", "BOTH", "ru"),
    ("положи на клавиши восемь тактов аккордов", "BOTH", "ru"),
    ("新建一条电钢轨道并写一段温暖的进行", "BOTH", "zh"),
    ("在贝斯轨上写一条走动的贝斯线", "BOTH", "zh"),
    # ---- AUTOMATION -------------------------------------------------------
    ("open the filter up slowly across the intro", "AUTOMATION", "en"),
    ("i want the reverb to breathe in and out", "AUTOMATION", "en"),
    ("pull the volume down to nothing by the last bar", "AUTOMATION", "en"),
    ("wobble the cutoff twice per bar", "AUTOMATION", "en"),
    ("wipe the curves off the pad track", "AUTOMATION", "en"),
    ("stretch that automation clip out to sixteen bars", "AUTOMATION", "en"),
    ("カットオフをゆっくり開いていって", "AUTOMATION", "ja"),
    ("リバーブの量を波打たせて", "AUTOMATION", "ja"),
    ("плавно открой фильтр к концу интро", "AUTOMATION", "ru"),
    ("убери все кривые автоматизации с пэдов", "AUTOMATION", "ru"),
    ("让截止频率在前奏里慢慢打开", "AUTOMATION", "zh"),
    ("把那个自动化片段拉长到十六小节", "AUTOMATION", "zh"),
    # ---- DRUM -------------------------------------------------------------
    ("give the groove more shuffle", "DRUM", "en"),
    ("the beat is too stiff, loosen it up", "DRUM", "en"),
    ("put a crash on the downbeat of bar five", "DRUM", "en"),
    ("i want a half time feel on the snare", "DRUM", "en"),
    ("thin out the percussion in the verse", "DRUM", "en"),
    ("write me something in the style of a boom bap kit", "DRUM", "en"),
    ("ビートをもう少し跳ねさせて", "DRUM", "ja"),
    ("スネアをハーフタイムにして", "DRUM", "ja"),
    ("сделай грув более расхлябанным", "DRUM", "ru"),
    ("добавь тарелку на первую долю пятого такта", "DRUM", "ru"),
    ("让节奏更有摇摆感", "DRUM", "zh"),
    ("军鼓改成半速的感觉", "DRUM", "zh"),
    # ---- MIXING -----------------------------------------------------------
    ("something is eating all the space around 200 hz", "MIXING", "en"),
    ("does this sit right next to a commercial reference", "MIXING", "en"),
    ("the vocal keeps disappearing behind the synths", "MIXING", "en"),
    ("am i pushing the master too hard", "MIXING", "en"),
    ("tell me what is wrong with the balance", "MIXING", "en"),
    ("how wide is this actually sitting", "MIXING", "en"),
    ("ボーカルがシンセに埋もれてしまう", "MIXING", "ja"),
    ("マスターを突っ込みすぎてない", "MIXING", "ja"),
    ("вокал тонет за синтами", "MIXING", "ru"),
    ("не перегружаю ли я мастер", "MIXING", "ru"),
    ("人声总是被合成器盖住", "MIXING", "zh"),
    ("我是不是把总线推得太狠了", "MIXING", "zh"),
    # ---- SESSION ----------------------------------------------------------
    ("kick off the second row of clips", "SESSION", "en"),
    ("kill everything that is playing", "SESSION", "en"),
    ("get the take i just played into a scene", "SESSION", "en"),
    ("put the drums clip in the launch queue", "SESSION", "en"),
    ("set the bass track ready to record", "SESSION", "en"),
    ("二番目のシーンを立ち上げて", "SESSION", "ja"),
    ("鳴っているクリップを全部止めて", "SESSION", "ja"),
    ("запусти второй ряд клипов", "SESSION", "ru"),
    ("вырубить всё, что играет", "SESSION", "ru"),
    ("启动第二排的片段", "SESSION", "zh"),
    ("把正在播放的都停掉", "SESSION", "zh"),
]


# Cases whose label needs an aesthetic judgement rather than a named operation.
# Kept in the set (they are realistic messages, and the C++ parity fixture still
# locks their behaviour) but scored separately — see the module docstring.
FUZZY = {
    "what would sound good under this melody",
    "i need four chords that feel like sunday morning",
    "turn these chords into something more open",
    "もっと切ない響きのコードない",
    "有没有更开放一点的和弦排列",
    "i want the reverb to breathe in and out",
    "the beat is too stiff, loosen it up",
    "сделай грув более расхлябанным",
}


def build():
    rows = [{"lang": lang, "input": text, "label": label,
             "kind": "fuzzy" if text in FUZZY else "core"}
            for text, label, lang in CASES]
    seen = set()
    for r in rows:
        assert r["input"] not in seen, f"duplicate test case: {r['input']}"
        seen.add(r["input"])
    unknown = FUZZY - seen
    assert not unknown, f"FUZZY lists inputs that are not test cases: {unknown}"
    out = os.path.join(HERE, "testset.jsonl")
    with open(out, "w", encoding="utf-8") as f:
        for r in rows:
            f.write(json.dumps(r, ensure_ascii=False) + "\n")

    from collections import Counter
    print(f"wrote {len(rows)} cases -> {out}")
    print("  by label:", dict(Counter(r["label"] for r in rows)))
    print("  by lang: ", dict(Counter(r["lang"] for r in rows)))


if __name__ == "__main__":
    build()
