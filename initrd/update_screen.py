"""The update screen pc-init shows while it puts a downloaded update in place (pc_init.c, show_update_screen).

pc-init has no image decoder and no fonts, so the build renders everything into raw images in the initrd:
update/art.raw (the installer's capybara) and update/title-LANG.raw / update/note-LANG.raw for each language.
A raw image is "PCIM", width and height (little-endian u32), then straight RGBA, 8 bits each. pc-init
blends them over BACKGROUND, centered.
"""
import struct
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
TOP = HERE.parents[3]
ART = TOP / 'packages/apps/PcInstaller/art/installer-capybara.png'
FONT = TOP / 'external/roboto-fonts/font/3.015/Roboto-Regular.ttf'
ART_SIZE = 280
TITLE_PX = 40
NOTE_PX = 22
TITLE_RGB = (255, 255, 255)
NOTE_RGB = (196, 188, 214)
# pc-init's background, keep in sync with UPDATE_BACKGROUND in pc_init.c.
BACKGROUND = (0x17, 0x12, 0x1f)

# Language (the first part of the locale the app saved, update-locale) -> title and note. pc-init falls back
# to English.
TEXTS = {
    'en': ('Updating the system', "This takes a moment. Don't turn off the computer."),
    'pt': ('Atualizando o sistema', 'Isso leva só um instante. Não desligue o computador.'),
    'es': ('Actualizando el sistema', 'Tardará solo un momento. No apagues el equipo.'),
}


def pack(image):
    rgba = image.convert('RGBA')
    return b'PCIM' + struct.pack('<II', rgba.width, rgba.height) + rgba.tobytes()


def font(size, weight):
    face = ImageFont.truetype(str(FONT), size)
    try:
        face.set_variation_by_axes([weight])
    except (OSError, AttributeError):
        pass
    return face


def render_text(text, size, weight, rgb):
    face = font(size, weight)
    left, top, right, bottom = face.getbbox(text)
    image = Image.new('RGBA', (right - left + 4, bottom - top + 8), rgb + (0,))
    ImageDraw.Draw(image).text((2 - left, 4 - top), text, font=face, fill=rgb + (255,))
    return image


def render_art():
    art = Image.open(ART).convert('RGBA')
    return art.resize((ART_SIZE, ART_SIZE), Image.LANCZOS)


def write_assets(folder):
    """Writes the update screen's images into folder; returns their names, relative to its parent."""
    folder.mkdir(parents=True, exist_ok=True)
    files = {'art.raw': render_art()}
    for lang, (title, note) in TEXTS.items():
        files[f'title-{lang}.raw'] = render_text(title, TITLE_PX, 600, TITLE_RGB)
        files[f'note-{lang}.raw'] = render_text(note, NOTE_PX, 400, NOTE_RGB)
    for name, image in files.items():
        (folder / name).write_bytes(pack(image))
    return [f'{folder.name}/{name}' for name in files]
