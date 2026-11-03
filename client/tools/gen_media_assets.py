#!/usr/bin/env python3
"""Generates the bundled sticker packs, GIF catalog and emoji catalog.

Run from the client directory:

    py -3 tools/gen_media_assets.py

The script only needs Pillow. Output:
    resources/stickers/<pack>/manifest.json + *.png
    resources/gifs/*.gif + manifest.json
    resources/emoji/emoji.json
"""

from __future__ import annotations

import json
import math
import os
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
STICKER_ROOT = ROOT / "resources" / "stickers"
GIF_ROOT = ROOT / "resources" / "gifs"
EMOJI_ROOT = ROOT / "resources" / "emoji"

SIZE = 160
EMOTIONS = [
    ("happy", "Радость"),
    ("love", "Любовь"),
    ("laugh", "Смех"),
    ("sad", "Грусть"),
    ("angry", "Злость"),
    ("wink", "Подмигивание"),
    ("surprised", "Удивление"),
    ("cool", "Круто"),
]

PACKS = [
    {
        "id": "mini.cats",
        "title": "Кошки",
        "author": "MINI",
        "fur": (246, 160, 77, 255),
        "dark": (216, 126, 48, 255),
        "kind": "cat",
    },
    {
        "id": "mini.dogs",
        "title": "Собаки",
        "author": "MINI",
        "fur": (201, 138, 91, 255),
        "dark": (166, 106, 64, 255),
        "kind": "dog",
    },
    {
        "id": "mini.pandas",
        "title": "Панды",
        "author": "MINI",
        "fur": (245, 245, 245, 255),
        "dark": (45, 45, 50, 255),
        "kind": "panda",
    },
    {
        "id": "mini.smiles",
        "title": "Смайлики",
        "author": "MINI",
        "fur": (255, 213, 79, 255),
        "dark": (222, 176, 35, 255),
        "kind": "face",
    },
]


def ellipse(draw: ImageDraw.ImageDraw, box, fill, outline=None, width=2):
    draw.ellipse(box, fill=fill, outline=outline, width=width)


def draw_cat_ears(draw, fur, dark):
    draw.polygon([(36, 62), (52, 18), (78, 46)], fill=fur, outline=dark)
    draw.polygon([(124, 62), (108, 18), (82, 46)], fill=fur, outline=dark)


def draw_dog_ears(draw, fur, dark):
    ellipse(draw, (18, 52, 60, 116), dark)
    ellipse(draw, (100, 52, 142, 116), dark)
    ellipse(draw, (24, 58, 54, 110), fur)
    ellipse(draw, (106, 58, 136, 110), fur)


def draw_panda_ears(draw, fur, dark):
    ellipse(draw, (28, 30, 70, 72), dark)
    ellipse(draw, (90, 30, 132, 72), dark)


def draw_eyes(draw, emotion, dark, blush):
    left = (56, 74, 70, 88)
    right = (90, 74, 104, 88)
    if emotion in ("happy", "laugh", "wink"):
        if emotion == "wink":
            draw.arc((54, 72, 72, 92), 200, 340, fill=dark, width=5)
        else:
            draw.arc((54, 72, 72, 92), 200, 340, fill=dark, width=5)
        draw.arc((88, 72, 106, 92), 200, 340, fill=dark, width=5)
    elif emotion == "love":
        for box in (left, right):
            x0, y0, x1, y1 = box
            cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
            draw.ellipse((cx - 8, cy - 8, cx, cy), fill=(230, 70, 90, 255))
            draw.ellipse((cx, cy - 8, cx + 8, cy), fill=(230, 70, 90, 255))
            draw.polygon(
                [(cx - 8, cy - 2), (cx + 8, cy - 2), (cx, cy + 9)],
                fill=(230, 70, 90, 255),
            )
    elif emotion == "angry":
        draw.line((54, 68, 72, 74), fill=dark, width=5)
        draw.line((106, 68, 88, 74), fill=dark, width=5)
        ellipse(draw, left, dark)
        ellipse(draw, right, dark)
    elif emotion == "surprised":
        ellipse(draw, (54, 70, 72, 92), dark)
        ellipse(draw, (88, 70, 106, 92), dark)
    else:
        ellipse(draw, left, dark)
        ellipse(draw, right, dark)
    if emotion in ("happy", "love", "laugh", "wink", "cool"):
        ellipse(draw, (42, 96, 56, 106), blush, outline=None)
        ellipse(draw, (104, 96, 118, 106), blush, outline=None)


def draw_mouth(draw, emotion, dark, tongue):
    if emotion == "happy":
        draw.arc((62, 92, 98, 122), 10, 170, fill=dark, width=5)
    elif emotion == "laugh":
        draw.pieslice((56, 88, 104, 126), 0, 180, fill=(120, 40, 40, 255))
        draw.ellipse((70, 106, 90, 122), fill=tongue)
    elif emotion == "love":
        draw.arc((64, 94, 96, 118), 20, 160, fill=dark, width=5)
    elif emotion == "sad":
        draw.arc((62, 108, 98, 132), 190, 350, fill=dark, width=5)
    elif emotion == "angry":
        draw.arc((64, 108, 96, 128), 190, 350, fill=dark, width=5)
    elif emotion == "wink":
        draw.arc((62, 94, 98, 118), 20, 160, fill=dark, width=5)
    elif emotion == "surprised":
        ellipse(draw, (72, 104, 88, 120), (120, 40, 40, 255))
    elif emotion == "cool":
        draw.line((64, 108, 96, 108), fill=dark, width=5)
    else:
        draw.arc((62, 94, 98, 118), 20, 160, fill=dark, width=5)


def make_sticker(pack, emotion) -> Image.Image:
    image = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    fur, dark = pack["fur"], pack["dark"]
    blush = (240, 128, 128, 160)
    tongue = (230, 90, 110, 255)

    if pack["kind"] == "cat":
        draw_cat_ears(draw, fur, dark)
    elif pack["kind"] == "dog":
        draw_dog_ears(draw, fur, dark)
    elif pack["kind"] == "panda":
        draw_panda_ears(draw, fur, dark)

    ellipse(draw, (30, 38, 130, 138), fur, dark, 3)

    if pack["kind"] == "panda":
        ellipse(draw, (52, 70, 76, 94), dark)
        ellipse(draw, (84, 70, 108, 94), dark)
        ellipse(draw, (58, 76, 68, 86), (245, 245, 245, 255))
        ellipse(draw, (92, 76, 102, 86), (245, 245, 245, 255))
        draw_eyes(draw, emotion, dark, blush)
        if emotion == "laugh":
            draw.pieslice((62, 96, 98, 124), 0, 180, fill=(120, 40, 40, 255))
            draw.ellipse((74, 110, 86, 120), fill=tongue)
        else:
            draw_mouth(draw, emotion, dark, tongue)
    else:
        draw_eyes(draw, emotion, dark, blush)
        draw_mouth(draw, emotion, dark, tongue)

    if pack["kind"] == "cat":
        for y in (104, 114):
            draw.line((46, y, 66, y + 10), fill=dark, width=2)
            draw.line((114, y, 94, y + 10), fill=dark, width=2)
    if pack["kind"] == "dog" and emotion in ("happy", "laugh"):
        draw.ellipse((72, 96, 88, 106), fill=dark)

    if emotion == "cool":
        draw.rounded_rectangle((46, 70, 114, 88), radius=6, fill=(30, 30, 36, 255))
        draw.rectangle((78, 70, 84, 88), fill=(30, 30, 36, 255))

    return image


def build_stickers():
    index = {"packs": []}
    for pack in PACKS:
        folder = STICKER_ROOT / pack["id"].split(".")[-1]
        folder.mkdir(parents=True, exist_ok=True)
        stickers = []
        for emotion, title in EMOTIONS:
            name = f"{pack['id'].split('.')[-1]}-{emotion}.png"
            make_sticker(pack, emotion).save(folder / name)
            stickers.append({"id": name[:-4], "title": title, "file": name})
        manifest = {
            "id": pack["id"],
            "title": pack["title"],
            "author": pack["author"],
            "cover": stickers[0]["file"],
            "stickers": stickers,
        }
        (folder / "manifest.json").write_text(
            json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
        )
        index["packs"].append(pack["id"])
    (STICKER_ROOT / "index.json").write_text(
        json.dumps(index, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )


GIFS = [
    ("bounce", "Прыжок", "Реакции", "bounce"),
    ("heartbeat", "Сердце", "Реакции", "heart"),
    ("spin", "Звезда", "Реакции", "spin"),
    ("fire", "Огонь", "Реакции", "fire"),
    ("thumbs", "Класс", "Реакции", "thumb"),
    ("confetti", "Конфетти", "Праздник", "confetti"),
    ("gift", "Подарок", "Праздник", "gift"),
    ("sparkle", "Искры", "Праздник", "sparkle"),
    ("rain", "Дождь", "Природа", "rain"),
    ("snow", "Снег", "Природа", "snow"),
    ("sun", "Солнце", "Природа", "sun"),
    ("waves", "Волны", "Природа", "waves"),
]


def gif_frame(kind: str, phase: float) -> Image.Image:
    image = Image.new("RGBA", (160, 96), (18, 20, 26, 255))
    draw = ImageDraw.Draw(image)
    t = phase * 2 * math.pi

    if kind == "bounce":
        y = 60 - abs(math.sin(t)) * 34
        draw.ellipse((62, y, 98, y + 36), fill=(120, 220, 140, 255))
        draw.ellipse((74, y + 8, 82, y + 18), fill=(30, 30, 40, 255))
    elif kind == "heart":
        scale = 0.8 + 0.2 * math.sin(t)
        color = (235, 70, 95, 255)
        cx, cy = 80, 46
        r = 22 * scale
        draw.ellipse((cx - r, cy - r * 0.6, cx, cy + r * 0.4), fill=color)
        draw.ellipse((cx, cy - r * 0.6, cx + r, cy + r * 0.4), fill=color)
        draw.polygon([(cx - r, cy), (cx + r, cy), (cx, cy + r * 1.4)], fill=color)
    elif kind == "spin":
        angle = t
        points = []
        for i in range(10):
            radius = 34 if i % 2 == 0 else 15
            a = angle + i * math.pi / 5
            points.append((80 + math.cos(a) * radius, 48 + math.sin(a) * radius))
        draw.polygon(points, fill=(255, 210, 80, 255))
    elif kind == "fire":
        for i in range(3):
            sway = math.sin(t + i) * 4
            h = 30 + 12 * math.sin(t * 2 + i)
            draw.ellipse((60 + i * 16 + sway, 58 - h, 82 + i * 16 + sway, 76),
                         fill=(250, 120 + i * 30, 40, 220))
    elif kind == "thumb":
        lift = math.sin(t) * 6
        draw.ellipse((46, 34 + lift, 62, 62 + lift), fill=(255, 210, 80, 255))
        draw.rounded_rectangle((60, 30 + lift, 74, 70 + lift), radius=7,
                               fill=(255, 210, 80, 255))
        draw.rectangle((74, 44 + lift, 96, 58 + lift), fill=(255, 210, 80, 255))
    elif kind == "confetti":
        colors = [(235, 70, 95), (120, 220, 140), (90, 170, 255), (255, 210, 80)]
        for i in range(18):
            x = (i * 37 + phase * 160) % 160
            y = (i * 23 + phase * 96) % 96
            draw.rectangle((x, y, x + 6, y + 6), fill=colors[i % len(colors)] + (255,))
    elif kind == "gift":
        bob = math.sin(t) * 3
        draw.rectangle((54, 44 + bob, 106, 78 + bob), fill=(220, 80, 90, 255))
        draw.rectangle((48, 34 + bob, 112, 48 + bob), fill=(240, 120, 120, 255))
        draw.rectangle((76, 34 + bob, 84, 78 + bob), fill=(255, 230, 150, 255))
        draw.ellipse((68, 26 + bob, 80, 38 + bob), fill=(255, 230, 150, 255))
        draw.ellipse((80, 26 + bob, 92, 38 + bob), fill=(255, 230, 150, 255))
    elif kind == "sparkle":
        for i in range(5):
            a = t + i * 2 * math.pi / 5
            x = 80 + math.cos(a) * 44
            y = 48 + math.sin(a) * 26
            r = 4 + 3 * math.sin(t * 2 + i)
            draw.line((x - r, y, x + r, y), fill=(255, 240, 180, 255), width=2)
            draw.line((x, y - r, x, y + r), fill=(255, 240, 180, 255), width=2)
    elif kind == "rain":
        for i in range(10):
            x = (i * 31 + 7) % 160
            y = (i * 17 + phase * 96) % 96
            draw.line((x, y, x - 4, y + 12), fill=(120, 180, 255, 255), width=3)
    elif kind == "snow":
        for i in range(14):
            x = (i * 29 + phase * 12) % 160
            y = (i * 13 + phase * 96) % 96
            r = 2 + (i % 3)
            draw.ellipse((x - r, y - r, x + r, y + r), fill=(240, 245, 255, 255))
    elif kind == "sun":
        pulse = 0.7 + 0.3 * math.sin(t)
        draw.ellipse((58, 26, 102, 70), fill=(255, 210, 80, 255))
        for i in range(8):
            a = t + i * math.pi / 4
            x1 = 80 + math.cos(a) * 28
            y1 = 48 + math.sin(a) * 28
            x2 = 80 + math.cos(a) * (38 * pulse)
            y2 = 48 + math.sin(a) * (38 * pulse)
            draw.line((x1, y1, x2, y2), fill=(255, 220, 120, 255), width=4)
    elif kind == "waves":
        for row in range(3):
            points = []
            for x in range(0, 161, 8):
                y = 30 + row * 20 + math.sin((x / 24.0) + t + row) * 5
                points.append((x, y))
            draw.line(points, fill=(90, 170, 255, 255), width=4)
    else:
        draw.ellipse((70, 38, 90, 58), fill=(200, 200, 200, 255))
    return image


def build_sample_package():
    """A tiny .ministicker used by the install test (store-only zip)."""
    import zipfile

    source = STICKER_ROOT / "cats"
    manifest = {
        "id": "mini.sample",
        "title": "Пример",
        "author": "MINI",
        "cover": "sample-happy.png",
        "stickers": [
            {"id": "sample-happy", "title": "Радость", "file": "sample-happy.png"},
            {"id": "sample-love", "title": "Любовь", "file": "sample-love.png"},
        ],
    }
    with zipfile.ZipFile(STICKER_ROOT / "sample.ministicker", "w") as archive:
        archive.writestr("manifest.json", json.dumps(manifest, ensure_ascii=False))
        archive.write(source / "cats-happy.png", "sample-happy.png")
        archive.write(source / "cats-love.png", "sample-love.png")


def build_gifs():
    GIF_ROOT.mkdir(parents=True, exist_ok=True)
    items = []
    frames = 10
    for gid, title, category, kind in GIFS:
        images = [gif_frame(kind, i / frames) for i in range(frames)]
        path = GIF_ROOT / f"{gid}.gif"
        images[0].save(
            path,
            save_all=True,
            append_images=images[1:],
            duration=80,
            loop=0,
            disposal=2,
        )
        items.append({"id": gid, "title": title, "category": category, "file": path.name})
    manifest = {"id": "mini.gifs", "title": "GIF", "items": items}
    (GIF_ROOT / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )


EMOJI = {
    "smileys": (
        "Смайлы",
        [
            ("😀", "улыбка радость", "grin smile happy"),
            ("😃", "смех радость", "smile happy"),
            ("😄", "смех веселье", "laugh happy"),
            ("😁", "ухмылка", "grin"),
            ("😆", "хохот", "laugh"),
            ("😅", "пот смех", "sweat smile"),
            ("🤣", "катаюсь со смеху", "rofl"),
            ("😂", "слёзы смех", "joy tears"),
            ("🙂", "легкая улыбка", "slight smile"),
            ("🙃", "перевёрнутый смайл", "upside down"),
            ("😉", "подмигивание", "wink"),
            ("😊", "улыбка румянец", "blush"),
            ("😇", "ангел", "angel"),
            ("🥰", "влюблённость", "love hearts"),
            ("😍", "влюблённые глаза", "heart eyes"),
            ("🤩", "звёздные глаза", "star struck"),
            ("😘", "поцелуй", "kiss"),
            ("😗", "поцелуй губы", "kiss"),
            ("😋", "вкусно", "yum"),
            ("😛", "язык", "tongue"),
            ("🤪", "сумасшествие", "crazy"),
            ("🤨", "бровь", "raised eyebrow"),
            ("🧐", "монокль", "monocle"),
            ("🤓", "ботаник", "nerd"),
        ],
    ),
    "gestures": (
        "Жесты",
        [
            ("👋", "привет рука", "wave hello"),
            ("🤚", "ладонь", "hand"),
            ("✋", "стоп", "stop hand"),
            ("🖖", "вулканский салют", "vulcan"),
            ("👌", "окей", "ok"),
            ("🤌", "щепоть", "pinch"),
            ("🤏", "чуть-чуть", "pinch"),
            ("✌️", "victory мир", "victory peace"),
            ("🤞", "скрещённые пальцы", "fingers crossed"),
            ("🤟", "люблю", "love you"),
            ("🤘", "рок", "rock"),
            ("🤙", "позвони", "call me"),
            ("👈", "влево", "left"),
            ("👉", "вправо", "right"),
            ("👆", "вверх", "up"),
            ("👇", "вниз", "down"),
            ("👍", "палец вверх", "thumbs up"),
            ("👎", "палец вниз", "thumbs down"),
            ("👏", "аплодисменты", "clap"),
            ("🙌", "ура", "raise hands"),
            ("🤝", "рукопожатие", "handshake"),
            ("🙏", "спасибо", "pray thanks"),
        ],
    ),
    "animals": (
        "Животные",
        [
            ("🐶", "собака", "dog"),
            ("🐱", "кошка", "cat"),
            ("🐭", "мышь", "mouse"),
            ("🐹", "хомяк", "hamster"),
            ("🐰", "кролик", "rabbit"),
            ("🦊", "лиса", "fox"),
            ("🐻", "медведь", "bear"),
            ("🐼", "панда", "panda"),
            ("🐨", "коала", "koala"),
            ("🐯", "тигр", "tiger"),
            ("🦁", "лев", "lion"),
            ("🐮", "корова", "cow"),
            ("🐷", "свинья", "pig"),
            ("🐸", "лягушка", "frog"),
            ("🐵", "обезьяна", "monkey"),
            ("🐔", "курица", "chicken"),
            ("🐧", "пингвин", "penguin"),
            ("🐦", "птица", "bird"),
            ("🦄", "единорог", "unicorn"),
            ("🐝", "пчела", "bee"),
            ("🦋", "бабочка", "butterfly"),
            ("🐢", "черепаха", "turtle"),
        ],
    ),
    "food": (
        "Еда",
        [
            ("🍎", "яблоко", "apple"),
            ("🍐", "груша", "pear"),
            ("🍊", "апельсин", "orange"),
            ("🍋", "лимон", "lemon"),
            ("🍌", "банан", "banana"),
            ("🍉", "арбуз", "watermelon"),
            ("🍇", "виноград", "grapes"),
            ("🍓", "клубника", "strawberry"),
            ("🍒", "вишня", "cherry"),
            ("🍑", "персик", "peach"),
            ("🥑", "авокадо", "avocado"),
            ("🍅", "помидор", "tomato"),
            ("🌽", "кукуруза", "corn"),
            ("🍞", "хлеб", "bread"),
            ("🧀", "сыр", "cheese"),
            ("🍕", "пицца", "pizza"),
            ("🍔", "бургер", "burger"),
            ("🍟", "картошка", "fries"),
            ("🍣", "суши", "sushi"),
            ("🍦", "мороженое", "ice cream"),
            ("🍩", "пончик", "donut"),
            ("☕", "кофе", "coffee"),
        ],
    ),
    "travel": (
        "Места",
        [
            ("🚗", "машина", "car"),
            ("🚕", "такси", "taxi"),
            ("🚌", "автобус", "bus"),
            ("🚎", "троллейбус", "trolleybus"),
            ("🚓", "полиция", "police"),
            ("🚑", "скорая", "ambulance"),
            ("🚒", "пожарная", "fire truck"),
            ("🚲", "велосипед", "bike"),
            ("🛴", "самокат", "scooter"),
            ("✈️", "самолёт", "plane"),
            ("🚀", "ракета", "rocket"),
            ("🚁", "вертолёт", "helicopter"),
            ("🚂", "поезд", "train"),
            ("🚢", "корабль", "ship"),
            ("🏠", "дом", "house"),
            ("🏢", "здание", "building"),
            ("🏫", "школа", "school"),
            ("🏥", "больница", "hospital"),
            ("🌍", "земля", "earth"),
            ("🌙", "луна", "moon"),
            ("⭐", "звезда", "star"),
            ("🌈", "радуга", "rainbow"),
        ],
    ),
    "objects": (
        "Предметы",
        [
            ("📱", "телефон", "phone"),
            ("💻", "ноутбук", "laptop"),
            ("⌨️", "клавиатура", "keyboard"),
            ("🖥️", "компьютер", "desktop"),
            ("🖱️", "мышь", "mouse"),
            ("🎮", "геймпад", "gamepad"),
            ("🎧", "наушники", "headphones"),
            ("📷", "камера", "camera"),
            ("📺", "телевизор", "tv"),
            ("💡", "идея лампа", "idea bulb"),
            ("🔋", "батарея", "battery"),
            ("💎", "алмаз", "diamond"),
            ("🔑", "ключ", "key"),
            ("🔒", "замок", "lock"),
            ("📚", "книги", "books"),
            ("✏️", "карандаш", "pencil"),
            ("📝", "заметка", "note"),
            ("📅", "календарь", "calendar"),
            ("⏰", "будильник", "alarm"),
            ("🎁", "подарок", "gift"),
            ("🎈", "шарик", "balloon"),
            ("🎉", "праздник", "party"),
        ],
    ),
    "symbols": (
        "Символы",
        [
            ("❤️", "сердце любовь", "heart love"),
            ("🧡", "оранжевое сердце", "orange heart"),
            ("💛", "жёлтое сердце", "yellow heart"),
            ("💚", "зелёное сердце", "green heart"),
            ("💙", "синее сердце", "blue heart"),
            ("💜", "фиолетовое сердце", "purple heart"),
            ("🖤", "чёрное сердце", "black heart"),
            ("🤍", "белое сердце", "white heart"),
            ("💔", "разбитое сердце", "broken heart"),
            ("💯", "сто", "hundred"),
            ("✅", "галочка", "check"),
            ("❌", "крестик", "cross"),
            ("❗", "восклицание", "exclamation"),
            ("❓", "вопрос", "question"),
            ("⚠️", "внимание", "warning"),
            ("🔥", "огонь", "fire"),
            ("✨", "искры", "sparkles"),
            ("💥", "взрыв", "boom"),
            ("💤", "сон", "sleep"),
            ("🎵", "музыка", "music"),
            ("⚡", "молния", "lightning"),
            ("♻️", "переработка", "recycle"),
        ],
    ),
    "flags": (
        "Флаги",
        [
            ("🏳️", "белый флаг", "white flag"),
            ("🏴", "чёрный флаг", "black flag"),
            ("🏁", "финиш", "finish"),
            ("🚩", "красный флаг", "red flag"),
            ("🇷🇺", "россия", "russia"),
            ("🇰🇿", "казахстан", "kazakhstan"),
            ("🇧🇾", "беларусь", "belarus"),
            ("🇺🇸", "сша", "usa"),
            ("🇬🇧", "великобритания", "uk"),
            ("🇩🇪", "германия", "germany"),
            ("🇫🇷", "франция", "france"),
            ("🇯🇵", "япония", "japan"),
        ],
    ),
}


def build_emoji():
    EMOJI_ROOT.mkdir(parents=True, exist_ok=True)
    payload = {"categories": []}
    for cid, (title, entries) in EMOJI.items():
        payload["categories"].append(
            {
                "id": cid,
                "title": title,
                "emojis": [
                    {"char": char, "name": name, "keywords": keywords}
                    for char, name, keywords in entries
                ],
            }
        )
    (EMOJI_ROOT / "emoji.json").write_text(
        json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )


def main():
    os.makedirs(STICKER_ROOT, exist_ok=True)
    build_stickers()
    build_sample_package()
    build_gifs()
    build_emoji()
    print("Generated stickers, GIFs and emoji catalog.")


if __name__ == "__main__":
    main()
