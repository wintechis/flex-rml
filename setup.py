from pathlib import Path
from setuptools import setup
from setuptools.command.build_py import build_py as _build_py
from setuptools.command.develop import develop as _develop
from setuptools.command.egg_info import egg_info as _egg_info
import subprocess
import os


ROOT = Path(__file__).resolve().parent


def build_native():
    script = ROOT / "build.sh"
    if not script.exists():
        raise FileNotFoundError(f"Missing build script: {script}")

    env = os.environ.copy()
    subprocess.check_call(["bash", str(script)], cwd=str(ROOT), env=env)


class build_py(_build_py):
    def run(self):
        build_native()
        super().run()


class develop(_develop):
    def run(self):
        build_native()
        super().run()


class egg_info(_egg_info):
    def run(self):
        build_native()
        super().run()


setup(
    cmdclass={
        "build_py": build_py,
        "develop": develop,
        "egg_info": egg_info,
    },
)