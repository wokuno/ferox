#!/usr/bin/env python3

from __future__ import annotations

import argparse
import importlib
import shutil
import subprocess
import tempfile
from pathlib import Path


def run(cmd: list[str], cwd: Path) -> None:
    proc = subprocess.run(cmd, cwd=str(cwd), text=True, capture_output=True)
    if proc.returncode != 0:
        raise RuntimeError(
            "Command failed:\n"
            + " ".join(cmd)
            + "\n\nstdout:\n"
            + proc.stdout
            + "\n\nstderr:\n"
            + proc.stderr
        )


def build_capture_tool(repo_root: Path, build_dir: Path) -> Path:
    run(
        [
            "cmake",
            "-S",
            str(repo_root),
            "-B",
            str(build_dir),
            "-DCMAKE_BUILD_TYPE=Release",
            "-DBUILD_TESTS=OFF",
            "-DBUILD_TOOLS=ON",
        ],
        repo_root,
    )
    run(
        [
            "cmake",
            "--build",
            str(build_dir),
            "--target",
            "ferox_preview_capture",
            "--parallel",
        ],
        repo_root,
    )
    return build_dir / "tools" / "ferox_preview_capture"


def render_frames(
    capture_bin: Path,
    repo_root: Path,
    frame_dir: Path,
    frames: int,
    width: int,
    height: int,
    colonies: int,
    scale: int,
) -> None:
    run(
        [
            str(capture_bin),
            str(frame_dir),
            str(frames),
            str(width),
            str(height),
            str(colonies),
            str(scale),
        ],
        repo_root,
    )


def create_gif(frame_dir: Path, output: Path, fps: int) -> None:
    image_module = importlib.import_module("PIL.Image")

    frame_paths = sorted(frame_dir.glob("frame_*.ppm"))
    if not frame_paths:
        raise RuntimeError("No frames were generated")

    duration_ms = max(1, int(1000 / fps))
    images = []
    for path in frame_paths:
        img = image_module.open(path).convert(
            "P", palette=image_module.Palette.ADAPTIVE, colors=128
        )
        images.append(img)

    output.parent.mkdir(parents=True, exist_ok=True)
    first, rest = images[0], images[1:]
    first.save(
        output,
        save_all=True,
        append_images=rest,
        optimize=False,
        duration=duration_ms,
        loop=0,
        disposal=2,
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate README preview GIF from simulation frames."
    )
    parser.add_argument("--output", default="assets/preview.gif")
    parser.add_argument("--frames", type=int, default=90)
    parser.add_argument("--width", type=int, default=180)
    parser.add_argument("--height", type=int, default=100)
    parser.add_argument("--colonies", type=int, default=24)
    parser.add_argument("--scale", type=int, default=4)
    parser.add_argument("--fps", type=int, default=15)
    parser.add_argument("--build-dir", default="build-preview")
    parser.add_argument("--keep-frames", action="store_true")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[1]
    build_dir = repo_root / args.build_dir
    output = repo_root / args.output

    frame_dir: Path
    tmp_dir: tempfile.TemporaryDirectory[str] | None = None
    if args.keep_frames:
        frame_dir = repo_root / "artifacts" / "preview-frames"
        if frame_dir.exists():
            shutil.rmtree(frame_dir)
        frame_dir.mkdir(parents=True)
    else:
        tmp_dir = tempfile.TemporaryDirectory(prefix="ferox-preview-")
        frame_dir = Path(tmp_dir.name)

    capture_bin = build_capture_tool(repo_root, build_dir)
    render_frames(
        capture_bin=capture_bin,
        repo_root=repo_root,
        frame_dir=frame_dir,
        frames=args.frames,
        width=args.width,
        height=args.height,
        colonies=args.colonies,
        scale=args.scale,
    )
    create_gif(frame_dir=frame_dir, output=output, fps=args.fps)

    if tmp_dir is not None:
        tmp_dir.cleanup()

    print(f"Wrote {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
