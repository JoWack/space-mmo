#!/usr/bin/env python3
"""
The Humanoid man (Terra), built from task 174's final turnaround. Tasks 174/175.

    blender --factory-startup --background --python-exit-code 1 \
        --python tools/characters/humanoid_male.py -- [--stage cage|fit|full] [--check-only] \
        [--out DIR] [--engine DIR]

or exec'd inside a running Blender (the MCP session does this), where it builds into the open file.

SOURCES
  * D:/Documents/SpaceMMOAssets/CharacterImages/Humanoid-Male/humanoid-male-{front,left,back,right}.png,
    Joe's approved views (3 October): a plain navy skin-tight suit, A-pose, plain grey background.
  * Height 1.80 m: the Humanoid board's figure, and the pawn's CharacterHeightCentimetres (task 125).
  * Everything else -- every cross-section, the arm and leg axes, the hand's breadth -- is measured
    off those four images by sheet_measure.py at build time, so the images are the source of truth
    and the numbers here are only the choices the images cannot make (topology, smoothness).
"""

import os
import sys

_HERE = os.path.dirname(os.path.abspath(globals().get("__file__", "tools/characters/humanoid_male.py")))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

import importlib  # noqa: E402

import sheet_measure  # noqa: E402
import character_build  # noqa: E402

importlib.reload(sheet_measure)
importlib.reload(character_build)

# Set aside on 4 October for the Tripo model (task 180), which now owns HumanoidMale.blend and the
# HumanoidMale export. Joe retires old work as <name>_Old, so this one writes under that name instead.
NAME = "HumanoidMale_Old"
HEIGHT = 1.80
SHEETS = "D:/Documents/SpaceMMOAssets/CharacterImages/Humanoid-Male/humanoid-male-%s.png"
ROOT = os.path.dirname(os.path.dirname(_HERE))
DEFAULT_OUT = "D:/Documents/SpaceMMOAssets/Blender/Characters"
DEFAULT_ENGINE = os.path.join(ROOT, "client", "RawContent", "Characters", NAME)

CONFIG = {
    # superellipse exponents: 2 is an ellipse, higher is boxier
    "torso_n": 2.15,
    "head_n": 2.05,
    "leg_n": 2.1,
    "boot_n": 2.6,
    # arm depth / width along the arm (shoulder, elbow, forearm, wrist): the palms face the thighs,
    # so the wrist is deeper front-to-back than it is wide in the front view
    "arm_depth_ratio": [1.0, 0.95, 1.05, 1.25],
    # hand (fractions of the measured wrist-to-fingertip length unless metres)
    "palm_half_width": 0.041,          # metres; fallback if the side views cannot measure it
    "palm_half_thickness": 0.0135,     # metres
    "palm_fraction": 0.52,
    "finger_lengths": [0.43, 0.47, 0.44, 0.36],   # index, middle, ring, pinky
    "thumb_length": 0.36,
    "thumb_radius": 0.0105,            # metres
    "finger_curl": [4.0, 10.0, 8.0, 3.0],          # degrees at each station toward the palm
    # the palm turns this far toward the back, ramped in along the forearm (pronation). The side
    # views show the back of the hand nearly square on, so these hands face the thighs: 0.
    "hand_roll": 0.0,
    # passes of neighbour-averaging over the fit's corrections (removes ring-to-ring ridges)
    "fit_smooth_iterations": 3,
}


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []

    def flag(name, default=None):
        return argv[argv.index(name) + 1] if name in argv else default

    # Absolute, because Blender saves a relative image or file path under the root of C:, not the
    # working directory (compare_to_sheets.py found it), and reads renders back from there unnoticed.
    return {"stage": flag("--stage", "full"), "out": os.path.abspath(flag("--out", DEFAULT_OUT)),
            "engine": os.path.abspath(flag("--engine", DEFAULT_ENGINE)), "check_only": "--check-only" in argv}


def main():
    args = parse_args()
    character_build.run(NAME, HEIGHT, SHEETS, CONFIG, args)


if __name__ == "__main__" or "bpy" in sys.modules:
    main()
