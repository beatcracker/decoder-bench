"""FFmpeg command construction for generated bench media."""

from __future__ import annotations

import shlex
from typing import Any

from benchplan_compile import (
    MediaSpec,
    TitleSpec,
    fail,
    preset_template,
)


TITLE_FOOTER = "Press Back/Exit to stop"


def drawtext_escape(value: str) -> str:
    escaped = value.replace("\\", "\\\\")
    escaped = escaped.replace(":", "\\:")
    escaped = escaped.replace("'", "\\'")
    escaped = escaped.replace(",", "\\,")
    escaped = escaped.replace("%", "\\%")
    return escaped


def drawtext(
    text: str,
    fontsize: int,
    color: str,
    x: str,
    y: str,
    *,
    dynamic: bool = False,
    extra: str = "",
) -> str:
    text_value = text if dynamic else drawtext_escape(text)
    return f"drawtext=text='{text_value}':fontcolor={color}:fontsize={fontsize}:x={x}:y={y}{extra}"


def sample_text_filter(media: MediaSpec) -> str:
    large = max(media.h // 18, 16)
    medium = max(media.h // 28, 12)
    y2 = 10 + large + 8
    y3 = y2 + medium + 6
    return ",".join(
        [
            drawtext("%{n}", large, "white", "10", "10", dynamic=True),
            drawtext(media.filename, medium, "white", "10", str(y2)),
            drawtext("%{pts\\:hms}", medium, "white", "10", str(y3), dynamic=True),
        ]
    )


def sample_filtergraph(media: MediaSpec, presets: dict[str, Any]) -> str:
    template = preset_template(presets, media.preset, f"preset {media.preset}")
    try:
        rendered = template.format_map(vars(media))
    except (KeyError, ValueError) as e:
        fail(f"preset {media.preset} has invalid template: {e}")
    return f"{rendered},{sample_text_filter(media)}"


TITLE_FILTER = """
color=c=0x0d1b2a:s={w}x{h}:r={fps}:d={duration},
drawbox=x={panel_x}:y={panel_y}:w={panel_w}:h={panel_h}:color=0x000000@0.50:t=fill,
drawbox=x={panel_x}:y={accent_y}:w={panel_w}:h={accent_h}:color=0xe0b24f@0.95:t=fill,
drawtext=text='{title}':fontcolor=white:fontsize={title_font}:x=(w-text_w)/2:y={title_y}:borderw=3:bordercolor=0x000000@0.85:box=1:boxcolor=0x000000@0.20:boxborderw=18,
drawtext=text='{description}':fontcolor=0xf0f4f8:fontsize={summary_font}:x=(w-text_w)/2:y={summary_y}:borderw=2:bordercolor=0x000000@0.85:box=1:boxcolor=0x000000@0.16:boxborderw=12,
drawtext=text='{detail}':fontcolor=0xd9e2ec:fontsize={detail_font}:x=(w-text_w)/2:y={detail_y}:borderw=2:bordercolor=0x000000@0.85:box=1:boxcolor=0x000000@0.16:boxborderw=10,
drawtext=text='{footer}':fontcolor=0xe7b95b:fontsize={footer_font}:x=(w-text_w)/2:y={footer_y}:borderw=2:bordercolor=0x000000@0.85:box=1:boxcolor=0x000000@0.20:boxborderw=10
""".strip()


def title_layout(title: TitleSpec) -> dict[str, int | str]:
    panel_x = title.w // 10
    panel_y = title.h // 5
    return {
        "w": title.w,
        "h": title.h,
        "fps": title.fps,
        "duration": title.run_s,
        "panel_x": panel_x,
        "panel_y": panel_y,
        "panel_w": title.w - panel_x * 2,
        "panel_h": title.h - panel_y * 2,
        "accent_y": panel_y + 20,
        "accent_h": max(title.h // 180, 4),
        "title_font": max(title.h // 9, 30),
        "summary_font": max(title.h // 24, 18),
        "detail_font": max(title.h // 30, 16),
        "footer_font": max(title.h // 34, 14),
        "title_y": title.h * 28 // 100,
        "summary_y": title.h * 47 // 100,
        "detail_y": title.h * 58 // 100,
        "footer_y": title.h * 76 // 100,
        "title": drawtext_escape(title.title),
        "description": drawtext_escape(title.description),
        "detail": drawtext_escape(f"{title.test_count} tests - {title.runtime_s}s runtime"),
        "footer": drawtext_escape(TITLE_FOOTER),
    }


def title_filtergraph(title: TitleSpec) -> str:
    rendered = TITLE_FILTER.format_map(title_layout(title))
    return "".join(line.strip() for line in rendered.splitlines())


def encoder_args(codec: str, w: int, kbps: int, frames: int, peak_kbps: int | None = None) -> list[str]:
    vbv_maxrate = peak_kbps if peak_kbps is not None else kbps
    vbv_buf = vbv_maxrate * 2
    common = (
        f"keyint={frames}:min-keyint={frames}:scenecut=0:repeat-headers=1:"
        f"open-gop=0:bframes=0:vbv-maxrate={vbv_maxrate}:vbv-bufsize={vbv_buf}"
    )
    if codec == "hevc":
        x265_params = f"log-level=0:{common}:hrd=1"
        if kbps > 60000:
            x265_params = f"{x265_params}:high-tier=1"
        profile = "main10" if kbps >= 60000 or w >= 3840 else "main"
        pix_fmt = "yuv420p10le" if profile == "main10" else "yuv420p"
        return [
            "-c:v",
            "libx265",
            "-b:v",
            f"{kbps}k",
            "-profile:v",
            profile,
            "-preset",
            "medium",
            "-x265-params",
            x265_params,
            "-pix_fmt",
            pix_fmt,
        ]
    if codec == "h264":
        return [
            "-c:v",
            "libx264",
            "-b:v",
            f"{kbps}k",
            "-profile:v",
            "high",
            "-preset",
            "medium",
            "-x264-params",
            f"{common}:nal-hrd=cbr",
            "-pix_fmt",
            "yuv420p",
        ]
    fail(f"unsupported codec '{codec}'")


def ffmpeg_lavfi_args(
    *,
    filtergraph: str,
    frames: int,
    codec: str,
    w: int,
    fps: int,
    kbps: int,
    peak_kbps: int | None,
    output: str,
) -> list[str]:
    return [
        "ffmpeg",
        "-y",
        "-nostdin",
        "-hide_banner",
        "-loglevel",
        "warning",
        "-stats",
        "-stats_period",
        "5",
        "-f",
        "lavfi",
        "-i",
        filtergraph,
        "-frames:v",
        str(frames),
        "-an",
        "-r",
        str(fps),
        *encoder_args(codec, w, kbps, frames, peak_kbps),
        "-f",
        codec,
        output,
    ]


def sample_ffmpeg_args(media: MediaSpec, output: str, presets: dict[str, Any]) -> list[str]:
    return ffmpeg_lavfi_args(
        filtergraph=sample_filtergraph(media, presets),
        frames=media.fps * media.sample_s,
        codec=media.codec,
        w=media.w,
        fps=media.fps,
        kbps=media.kbps,
        peak_kbps=media.peak_kbps,
        output=output,
    )


def title_ffmpeg_args(title: TitleSpec, output: str) -> list[str]:
    return ffmpeg_lavfi_args(
        filtergraph=title_filtergraph(title),
        frames=title.fps * title.run_s,
        codec=title.codec,
        w=title.w,
        fps=title.fps,
        kbps=title.kbps,
        peak_kbps=None,
        output=output,
    )


def command_line(argv: list[str]) -> str:
    return " ".join(shlex.quote(arg) for arg in argv)
