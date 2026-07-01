from __future__ import annotations

import os
import json
import struct
import zlib
from pathlib import Path


def generate_charts(out_dir: Path, chart_data: dict) -> list[str]:
    mpl_config = out_dir / ".mplconfig"
    mpl_config.mkdir(parents=True, exist_ok=True)
    os.environ.setdefault("MPLCONFIGDIR", str(mpl_config))
    try:
        import matplotlib

        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except Exception as exc:
        return _generate_charts_pillow(out_dir, chart_data, [f"matplotlib import failed: {type(exc).__name__}: {exc}"])

    charts_dir = out_dir / "charts"
    charts_dir.mkdir(parents=True, exist_ok=True)
    made: list[str] = []

    def save_line(rows, filename, value_key, title, ylabel):
        plt.figure(figsize=(10, 4))
        for side, color in (("A", "#1f77b4"), ("B", "#d62728")):
            data = [r for r in rows if r["side"] == side]
            plt.plot([r["turn"] for r in data], [r.get(value_key, 0) for r in data], label=f"{side} side", color=color)
        _mark_diagnosis(chart_data)
        plt.title(title)
        plt.xlabel("Turn")
        plt.ylabel(ylabel)
        plt.legend()
        plt.tight_layout()
        plt.savefig(charts_dir / filename, dpi=140)
        plt.close()
        made.append(filename)

    def save_bar(rows, filename, value_key, title, ylabel):
        plt.figure(figsize=(10, 4))
        for side, color, offset in (("A", "#1f77b4", -0.2), ("B", "#d62728", 0.2)):
            data = [r for r in rows if r["side"] == side]
            plt.bar([r["turn"] + offset for r in data], [r.get(value_key, 0) for r in data], width=0.4, label=f"{side} side", color=color)
        _mark_diagnosis(chart_data)
        plt.title(title)
        plt.xlabel("Turn")
        plt.ylabel(ylabel)
        plt.legend()
        plt.tight_layout()
        plt.savefig(charts_dir / filename, dpi=140)
        plt.close()
        made.append(filename)

    errors: list[str] = []
    chart_jobs = [
        lambda: save_line(chart_data["gold_rows"], "gold_income_by_turn.png", "estimated_income", "Estimated Gold Income", "Gold"),
        lambda: save_line(chart_data["gold_rows"], "gold_spend_by_turn.png", "confirmed_spend", "Confirmed Gold Spend", "Gold"),
        lambda: save_line(chart_data["gold_rows"], "gold_balance_by_turn.png", "max_estimated_gold", "Estimated Gold Balance", "Gold"),
        lambda: save_line(chart_data["army_rows"], "alive_warriors_by_turn.png", "alive_warriors", "Alive Warriors", "Warriors"),
        lambda: save_bar(chart_data["army_rows"], "trained_warriors_by_turn.png", "trained_warriors", "Trained Warriors", "Warriors"),
        lambda: save_bar(chart_data["army_rows"], "warrior_loss_by_turn.png", "lost_warriors", "Warrior Losses", "Warriors"),
        lambda: save_line(chart_data["building_rows"], "building_count_by_turn.png", "building_count", "Building Count", "Buildings"),
        lambda: save_bar(chart_data["building_rows"], "building_acquired_by_turn.png", "new_buildings", "New Buildings", "Buildings"),
        lambda: save_line(chart_data["building_rows"], "upgrade_count_by_turn.png", "upgrades", "Upgrades by Turn", "Upgrades"),
        lambda: save_bar(chart_data["damage_rows"], "damage_by_turn.png", "damage_taken", "Damage Taken", "Damage"),
        lambda: save_bar(chart_data["damage_rows"], "siege_by_turn.png", "siege_done", "Siege Done", "Damage"),
        lambda: (_map_combat_zones(plt, charts_dir, chart_data), made.append("map_combat_zones.png")),
        lambda: (_unit_paths(plt, charts_dir, chart_data), made.append("unit_paths.png")),
    ]
    for job in chart_jobs:
        try:
            job()
        except Exception as exc:
            errors.append(f"{type(exc).__name__}: {exc}")
            try:
                plt.close()
            except Exception:
                pass
    _write_status(out_dir, made, errors, "matplotlib")
    return made


def _generate_charts_pillow(out_dir: Path, chart_data: dict, initial_errors: list[str]) -> list[str]:
    try:
        from PIL import Image, ImageDraw, ImageFont
    except Exception as exc:
        return _generate_charts_stdlib(out_dir, chart_data, initial_errors + [f"PIL fallback failed: {type(exc).__name__}: {exc}"])

    charts_dir = out_dir / "charts"
    charts_dir.mkdir(parents=True, exist_ok=True)
    made: list[str] = []
    errors = list(initial_errors)
    font = ImageFont.load_default()

    def save_xy(rows, filename, value_key, title, ylabel, kind="line"):
        width, height = 1400, 560
        margin = 80
        img = Image.new("RGB", (width, height), "white")
        draw = ImageDraw.Draw(img)
        draw.text((margin, 20), title, fill=(20, 20, 20), font=font)
        plot = (margin, 70, width - 35, height - 70)
        _draw_axes(draw, plot)
        turns = [r["turn"] for r in rows]
        values = [float(r.get(value_key, 0) or 0) for r in rows]
        if not turns:
            _draw_axis_labels(draw, plot, font, "Turn", ylabel)
            img.save(charts_dir / filename)
            made.append(filename)
            return
        min_turn, max_turn = min(turns), max(turns)
        min_value = min(0.0, min(values))
        max_value = max(1.0, max(values))
        _draw_axis_labels(draw, plot, font, "Turn", ylabel)
        _draw_tick_labels(draw, plot, font, min_turn, max_turn, min_value, max_value)
        for side, color, offset in (("A", (31, 119, 180), -2), ("B", (214, 39, 40), 2)):
            data = [r for r in rows if r["side"] == side]
            points = [
                _scale_point(r["turn"], float(r.get(value_key, 0) or 0), min_turn, max_turn, min_value, max_value, plot)
                for r in data
            ]
            if kind == "bar":
                bar_width = max(2, int((plot[2] - plot[0]) / max(1, len(data)) * 0.35))
                for x, y in points:
                    draw.rectangle((x + offset - bar_width // 2, y, x + offset + bar_width // 2, plot[3]), fill=color)
            else:
                for a, b in zip(points, points[1:]):
                    draw.line((a, b), fill=color, width=3)
        _draw_turn_marker(draw, chart_data, min_turn, max_turn, plot)
        _draw_legend(draw, plot, font)
        img.save(charts_dir / filename)
        made.append(filename)

    jobs = [
        lambda: save_xy(chart_data["gold_rows"], "gold_income_by_turn.png", "estimated_income", "Estimated Gold Income", "Gold"),
        lambda: save_xy(chart_data["gold_rows"], "gold_spend_by_turn.png", "confirmed_spend", "Confirmed Gold Spend", "Gold"),
        lambda: save_xy(chart_data["gold_rows"], "gold_balance_by_turn.png", "max_estimated_gold", "Estimated Gold Balance", "Gold"),
        lambda: save_xy(chart_data["army_rows"], "alive_warriors_by_turn.png", "alive_warriors", "Alive Warriors", "Warriors"),
        lambda: save_xy(chart_data["army_rows"], "trained_warriors_by_turn.png", "trained_warriors", "Trained Warriors", "Warriors", "bar"),
        lambda: save_xy(chart_data["army_rows"], "warrior_loss_by_turn.png", "lost_warriors", "Warrior Losses", "Warriors", "bar"),
        lambda: save_xy(chart_data["building_rows"], "building_count_by_turn.png", "building_count", "Building Count", "Buildings"),
        lambda: save_xy(chart_data["building_rows"], "building_acquired_by_turn.png", "new_buildings", "New Buildings", "Buildings", "bar"),
        lambda: save_xy(chart_data["building_rows"], "upgrade_count_by_turn.png", "upgrades", "Upgrades", "Upgrades"),
        lambda: save_xy(chart_data["damage_rows"], "damage_by_turn.png", "damage_taken", "Damage Taken", "Damage", "bar"),
        lambda: save_xy(chart_data["damage_rows"], "siege_by_turn.png", "siege_done", "Siege Done", "Damage", "bar"),
        lambda: _save_map_pillow(charts_dir, chart_data, "map_combat_zones.png", font, combat=True, made=made),
        lambda: _save_map_pillow(charts_dir, chart_data, "unit_paths.png", font, combat=False, made=made),
    ]
    for job in jobs:
        try:
            job()
        except Exception as exc:
            errors.append(f"PIL chart failed: {type(exc).__name__}: {exc}")
    _write_status(out_dir, made, errors, "pillow_fallback")
    return made


def _generate_charts_stdlib(out_dir: Path, chart_data: dict, initial_errors: list[str]) -> list[str]:
    charts_dir = out_dir / "charts"
    charts_dir.mkdir(parents=True, exist_ok=True)
    made: list[str] = []
    errors = list(initial_errors)

    def save_xy(rows, filename, value_key, title, ylabel, kind="line"):
        canvas = _Canvas(1400, 560)
        plot = (80, 70, 1365, 490)
        _canvas_axes(canvas, plot)
        canvas.text(80, 20, title, (20, 20, 20), scale=2)
        canvas.text((plot[0] + plot[2]) // 2 - 20, 525, "Turn", (20, 20, 20), scale=2)
        canvas.text(8, (plot[1] + plot[3]) // 2 - 10, ylabel, (20, 20, 20), scale=2)
        turns = [int(r["turn"]) for r in rows]
        values = [float(r.get(value_key, 0) or 0) for r in rows]
        if turns:
            min_turn, max_turn = min(turns), max(turns)
            min_value = min(0.0, min(values))
            max_value = max(1.0, max(values))
            _canvas_tick_labels(canvas, plot, min_turn, max_turn, min_value, max_value)
            for side, color, offset in (("A", (31, 119, 180), -2), ("B", (214, 39, 40), 2)):
                data = [r for r in rows if r["side"] == side]
                points = [
                    _scale_point(int(r["turn"]), float(r.get(value_key, 0) or 0), min_turn, max_turn, min_value, max_value, plot)
                    for r in data
                ]
                if kind == "bar":
                    bar_width = max(2, int((plot[2] - plot[0]) / max(1, len(data)) * 0.35))
                    for x, y in points:
                        canvas.rect(x + offset - bar_width // 2, y, x + offset + bar_width // 2, plot[3], color)
                else:
                    for a, b in zip(points, points[1:]):
                        canvas.line(a[0], a[1], b[0], b[1], color, 3)
            _canvas_turn_marker(canvas, chart_data, min_turn, max_turn, plot)
            _canvas_legend(canvas, plot)
        canvas.save_png(charts_dir / filename)
        made.append(filename)

    jobs = [
        lambda: save_xy(chart_data["gold_rows"], "gold_income_by_turn.png", "estimated_income", "Estimated Gold Income", "Gold"),
        lambda: save_xy(chart_data["gold_rows"], "gold_spend_by_turn.png", "confirmed_spend", "Confirmed Gold Spend", "Gold"),
        lambda: save_xy(chart_data["gold_rows"], "gold_balance_by_turn.png", "max_estimated_gold", "Estimated Gold Balance", "Gold"),
        lambda: save_xy(chart_data["army_rows"], "alive_warriors_by_turn.png", "alive_warriors", "Alive Warriors", "Warriors"),
        lambda: save_xy(chart_data["army_rows"], "trained_warriors_by_turn.png", "trained_warriors", "Trained Warriors", "Warriors", "bar"),
        lambda: save_xy(chart_data["army_rows"], "warrior_loss_by_turn.png", "lost_warriors", "Warrior Losses", "Warriors", "bar"),
        lambda: save_xy(chart_data["building_rows"], "building_count_by_turn.png", "building_count", "Building Count", "Buildings"),
        lambda: save_xy(chart_data["building_rows"], "building_acquired_by_turn.png", "new_buildings", "New Buildings", "Buildings", "bar"),
        lambda: save_xy(chart_data["building_rows"], "upgrade_count_by_turn.png", "upgrades", "Upgrades", "Upgrades"),
        lambda: save_xy(chart_data["damage_rows"], "damage_by_turn.png", "damage_taken", "Damage Taken", "Damage", "bar"),
        lambda: save_xy(chart_data["damage_rows"], "siege_by_turn.png", "siege_done", "Siege Done", "Damage", "bar"),
        lambda: _save_map_stdlib(charts_dir, chart_data, "map_combat_zones.png", True, made),
        lambda: _save_map_stdlib(charts_dir, chart_data, "unit_paths.png", False, made),
    ]
    for job in jobs:
        try:
            job()
        except Exception as exc:
            errors.append(f"stdlib chart failed: {type(exc).__name__}: {exc}")
    _write_status(out_dir, made, errors, "stdlib_png_fallback")
    return made


def _mark_diagnosis(chart_data: dict) -> None:
    import matplotlib.pyplot as plt

    for point in chart_data.get("diagnosis", {}).get("turning_points", []):
        if point.get("turn"):
            plt.axvline(point["turn"], color="#444444", linestyle="--", alpha=0.4)


def _map_combat_zones(plt, charts_dir: Path, chart_data: dict) -> None:
    game_map = chart_data["map"]
    damage_by_region = {}
    for row in chart_data.get("combat_zones", []):
        location = row.get("location")
        if location is not None:
            damage_by_region[location] = damage_by_region.get(location, 0) + (row.get("amount") or 0)
    for event in chart_data.get("events", []):
        if event["type"] == "SIEGE" and event.get("location") is not None:
            damage_by_region[event["location"]] = damage_by_region.get(event["location"], 0) + (event.get("amount") or 0)
    _draw_map_base(plt, game_map)
    for region, amount in damage_by_region.items():
        plt.scatter(game_map["x"][region], game_map["y"][region], s=40 + amount * 5, color="#9467bd", alpha=0.65)
    plt.title("Map Combat/Siege Zones")
    plt.xlabel("Map X")
    plt.ylabel("Map Y")
    plt.tight_layout()
    plt.savefig(charts_dir / "map_combat_zones.png", dpi=140)
    plt.close()


def _unit_paths(plt, charts_dir: Path, chart_data: dict) -> None:
    game_map = chart_data["map"]
    _draw_map_base(plt, game_map)
    for row in chart_data["unit_path_rows"]:
        path = [int(v) for v in str(row.get("path", "")).split() if v.isdigit()]
        if len(path) < 2:
            continue
        color = "#1f77b4" if row["side"] == "A" else "#d62728"
        plt.plot([game_map["x"][p] for p in path], [game_map["y"][p] for p in path], color=color, alpha=0.25)
    plt.title("Unit Movement Paths")
    plt.xlabel("Map X")
    plt.ylabel("Map Y")
    plt.tight_layout()
    plt.savefig(charts_dir / "unit_paths.png", dpi=140)
    plt.close()


def _draw_map_base(plt, game_map: dict) -> None:
    plt.figure(figsize=(8, 8))
    x = game_map["x"]
    y = game_map["y"]
    for i, neighbors in enumerate(game_map["adjacency"]):
        for j in neighbors:
            if i < j:
                plt.plot([x[i], x[j]], [y[i], y[j]], color="#cccccc", linewidth=0.6, zorder=1)
    colors = ["#ffbf00" if i in game_map["strongholds"] else "#777777" for i in range(game_map["n"])]
    plt.scatter(x, y, c=colors, s=18, zorder=2)
    if game_map["n"]:
        plt.scatter([x[0], x[-1]], [y[0], y[-1]], c=["#1f77b4", "#d62728"], s=80, zorder=3)
    plt.axis("equal")


def _draw_axes(draw, plot):
    left, top, right, bottom = plot
    draw.rectangle(plot, outline=(180, 180, 180))
    for i in range(1, 5):
        y = top + (bottom - top) * i // 5
        draw.line((left, y, right, y), fill=(230, 230, 230))


def _draw_axis_labels(draw, plot, font, xlabel: str, ylabel: str) -> None:
    left, top, right, bottom = plot
    draw.text(((left + right) // 2 - 16, bottom + 30), xlabel, fill=(20, 20, 20), font=font)
    draw.text((8, (top + bottom) // 2 - 6), ylabel, fill=(20, 20, 20), font=font)


def _draw_tick_labels(draw, plot, font, min_turn, max_turn, min_value, max_value) -> None:
    left, top, right, bottom = plot
    for i in range(0, 6):
        x = left + (right - left) * i // 5
        turn = min_turn + (max_turn - min_turn) * i / 5
        draw.text((x - 10, bottom + 8), _format_tick(turn), fill=(60, 60, 60), font=font)
        y = bottom - (bottom - top) * i // 5
        value = min_value + (max_value - min_value) * i / 5
        draw.text((left - 55, y - 6), _format_tick(value), fill=(60, 60, 60), font=font)


def _format_tick(value: float) -> str:
    if abs(value - round(value)) < 0.001:
        return str(int(round(value)))
    return f"{value:.1f}"


def _scale_point(turn, value, min_turn, max_turn, min_value, max_value, plot):
    left, top, right, bottom = plot
    x_span = max(1, max_turn - min_turn)
    y_span = max(1.0, max_value - min_value)
    x = left + int((turn - min_turn) / x_span * (right - left))
    y = bottom - int((value - min_value) / y_span * (bottom - top))
    return x, y


def _draw_turn_marker(draw, chart_data, min_turn, max_turn, plot):
    left, top, right, bottom = plot
    x_span = max(1, max_turn - min_turn)
    for point in chart_data.get("diagnosis", {}).get("turning_points", []):
        turn = point.get("turn")
        if turn is not None:
            x = left + int((turn - min_turn) / x_span * (right - left))
            draw.line((x, top, x, bottom), fill=(80, 80, 80), width=2)


def _draw_legend(draw, plot, font):
    left, top, right, _ = plot
    draw.line((right - 150, top + 12, right - 110, top + 12), fill=(31, 119, 180), width=4)
    draw.text((right - 105, top + 5), "A side", fill=(20, 20, 20), font=font)
    draw.line((right - 150, top + 32, right - 110, top + 32), fill=(214, 39, 40), width=4)
    draw.text((right - 105, top + 25), "B side", fill=(20, 20, 20), font=font)


def _save_map_pillow(charts_dir: Path, chart_data: dict, filename: str, font, combat: bool, made: list[str]) -> None:
    from PIL import Image, ImageDraw

    game_map = chart_data["map"]
    width = height = 1120
    margin = 60
    img = Image.new("RGB", (width, height), "white")
    draw = ImageDraw.Draw(img)
    title = "Map Combat/Siege Zones" if combat else "Unit Movement Paths"
    draw.text((margin, 20), title, fill=(20, 20, 20), font=font)
    draw.text((width // 2 - 20, height - 35), "Map X", fill=(20, 20, 20), font=font)
    draw.text((8, height // 2 - 6), "Map Y", fill=(20, 20, 20), font=font)
    xs = game_map["x"]
    ys = game_map["y"]
    if not xs or not ys:
        img.save(charts_dir / filename)
        made.append(filename)
        return
    min_x, max_x = min(xs), max(xs)
    min_y, max_y = min(ys), max(ys)

    def pos(region):
        x = margin + int((xs[region] - min_x) / max(1, max_x - min_x) * (width - margin * 2))
        y = height - margin - int((ys[region] - min_y) / max(1, max_y - min_y) * (height - margin * 2))
        return x, y

    for i, neighbors in enumerate(game_map["adjacency"]):
        for j in neighbors:
            if i < j:
                draw.line((pos(i), pos(j)), fill=(220, 220, 220), width=1)
    if combat:
        totals = {}
        for row in chart_data.get("combat_zones", []):
            location = row.get("location")
            if location is not None:
                totals[location] = totals.get(location, 0) + (row.get("amount") or 0)
        for event in chart_data.get("events", []):
            if event.get("type") == "SIEGE" and event.get("location") is not None:
                totals[event["location"]] = totals.get(event["location"], 0) + (event.get("amount") or 0)
        for region, amount in totals.items():
            x, y = pos(region)
            radius = min(34, 5 + int(amount ** 0.5) * 2)
            draw.ellipse((x - radius, y - radius, x + radius, y + radius), fill=(148, 103, 189), outline=(90, 60, 120))
    else:
        for row in chart_data.get("unit_path_rows", []):
            path = [int(v) for v in str(row.get("path", "")).split() if v.isdigit()]
            if len(path) >= 2:
                color = (31, 119, 180) if row.get("side") == "A" else (214, 39, 40)
                draw.line([pos(p) for p in path if 0 <= p < game_map["n"]], fill=color, width=2)
    for i in range(game_map["n"]):
        x, y = pos(i)
        color = (255, 191, 0) if i in game_map["strongholds"] else (120, 120, 120)
        draw.ellipse((x - 3, y - 3, x + 3, y + 3), fill=color)
    draw.ellipse((pos(0)[0] - 8, pos(0)[1] - 8, pos(0)[0] + 8, pos(0)[1] + 8), fill=(31, 119, 180))
    last = game_map["n"] - 1
    draw.ellipse((pos(last)[0] - 8, pos(last)[1] - 8, pos(last)[0] + 8, pos(last)[1] + 8), fill=(214, 39, 40))
    img.save(charts_dir / filename)
    made.append(filename)


class _Canvas:
    def __init__(self, width: int, height: int, background: tuple[int, int, int] = (255, 255, 255)) -> None:
        self.width = width
        self.height = height
        self.pixels = bytearray(background * width * height)

    def set_pixel(self, x: int, y: int, color: tuple[int, int, int]) -> None:
        if 0 <= x < self.width and 0 <= y < self.height:
            idx = (y * self.width + x) * 3
            self.pixels[idx : idx + 3] = bytes(color)

    def line(self, x0: int, y0: int, x1: int, y1: int, color: tuple[int, int, int], width: int = 1) -> None:
        dx = abs(x1 - x0)
        dy = -abs(y1 - y0)
        sx = 1 if x0 < x1 else -1
        sy = 1 if y0 < y1 else -1
        err = dx + dy
        while True:
            radius = max(0, width // 2)
            for yy in range(y0 - radius, y0 + radius + 1):
                for xx in range(x0 - radius, x0 + radius + 1):
                    self.set_pixel(xx, yy, color)
            if x0 == x1 and y0 == y1:
                break
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x0 += sx
            if e2 <= dx:
                err += dx
                y0 += sy

    def rect(self, x0: int, y0: int, x1: int, y1: int, color: tuple[int, int, int]) -> None:
        left, right = sorted((max(0, x0), min(self.width - 1, x1)))
        top, bottom = sorted((max(0, y0), min(self.height - 1, y1)))
        for y in range(top, bottom + 1):
            start = (y * self.width + left) * 3
            end = (y * self.width + right + 1) * 3
            self.pixels[start:end] = bytes(color) * (right - left + 1)

    def circle(self, cx: int, cy: int, radius: int, color: tuple[int, int, int]) -> None:
        r2 = radius * radius
        for y in range(cy - radius, cy + radius + 1):
            for x in range(cx - radius, cx + radius + 1):
                if (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r2:
                    self.set_pixel(x, y, color)

    def text(self, x: int, y: int, text: str, color: tuple[int, int, int], scale: int = 1) -> None:
        cursor = x
        for char in text:
            if char == " ":
                cursor += 4 * scale
                continue
            glyph = _FONT_5X7.get(char.upper())
            if glyph:
                for row_idx, row in enumerate(glyph):
                    for col_idx, bit in enumerate(row):
                        if bit == "1":
                            self.rect(
                                cursor + col_idx * scale,
                                y + row_idx * scale,
                                cursor + (col_idx + 1) * scale - 1,
                                y + (row_idx + 1) * scale - 1,
                                color,
                            )
            cursor += 6 * scale

    def save_png(self, path: Path) -> None:
        rows = []
        stride = self.width * 3
        for y in range(self.height):
            rows.append(b"\x00" + bytes(self.pixels[y * stride : (y + 1) * stride]))
        raw = b"".join(rows)

        def chunk(kind: bytes, data: bytes) -> bytes:
            return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

        png = b"\x89PNG\r\n\x1a\n"
        png += chunk(b"IHDR", struct.pack(">IIBBBBB", self.width, self.height, 8, 2, 0, 0, 0))
        png += chunk(b"IDAT", zlib.compress(raw, 6))
        png += chunk(b"IEND", b"")
        path.write_bytes(png)


def _canvas_axes(canvas: _Canvas, plot: tuple[int, int, int, int]) -> None:
    left, top, right, bottom = plot
    grey = (180, 180, 180)
    grid = (230, 230, 230)
    canvas.line(left, top, right, top, grey)
    canvas.line(left, bottom, right, bottom, grey)
    canvas.line(left, top, left, bottom, grey)
    canvas.line(right, top, right, bottom, grey)
    for i in range(1, 5):
        y = top + (bottom - top) * i // 5
        canvas.line(left, y, right, y, grid)


def _canvas_tick_labels(canvas: _Canvas, plot: tuple[int, int, int, int], min_turn: int, max_turn: int, min_value: float, max_value: float) -> None:
    left, top, right, bottom = plot
    for i in range(0, 6):
        x = left + (right - left) * i // 5
        turn = min_turn + (max_turn - min_turn) * i / 5
        canvas.text(x - 8, bottom + 10, _format_tick(turn), (60, 60, 60))
        y = bottom - (bottom - top) * i // 5
        value = min_value + (max_value - min_value) * i / 5
        canvas.text(left - 45, y - 4, _format_tick(value), (60, 60, 60))


def _canvas_turn_marker(canvas: _Canvas, chart_data: dict, min_turn: int, max_turn: int, plot: tuple[int, int, int, int]) -> None:
    left, top, right, bottom = plot
    x_span = max(1, max_turn - min_turn)
    for point in chart_data.get("diagnosis", {}).get("turning_points", []):
        turn = point.get("turn")
        if turn is not None:
            x = left + int((int(turn) - min_turn) / x_span * (right - left))
            canvas.line(x, top, x, bottom, (80, 80, 80), 2)


def _canvas_legend(canvas: _Canvas, plot: tuple[int, int, int, int]) -> None:
    _, top, right, _ = plot
    canvas.rect(right - 150, top + 10, right - 110, top + 16, (31, 119, 180))
    canvas.rect(right - 150, top + 28, right - 110, top + 34, (214, 39, 40))


def _save_map_stdlib(charts_dir: Path, chart_data: dict, filename: str, combat: bool, made: list[str]) -> None:
    game_map = chart_data["map"]
    canvas = _Canvas(1120, 1120)
    margin = 60
    canvas.text(margin, 20, "Map Combat/Siege Zones" if combat else "Unit Movement Paths", (20, 20, 20), scale=2)
    canvas.text(canvas.width // 2 - 24, canvas.height - 35, "Map X", (20, 20, 20), scale=2)
    canvas.text(8, canvas.height // 2 - 8, "Map Y", (20, 20, 20), scale=2)
    xs = game_map.get("x", [])
    ys = game_map.get("y", [])
    if not xs or not ys:
        canvas.save_png(charts_dir / filename)
        made.append(filename)
        return
    min_x, max_x = min(xs), max(xs)
    min_y, max_y = min(ys), max(ys)

    def pos(region: int) -> tuple[int, int]:
        x = margin + int((xs[region] - min_x) / max(1, max_x - min_x) * (canvas.width - margin * 2))
        y = canvas.height - margin - int((ys[region] - min_y) / max(1, max_y - min_y) * (canvas.height - margin * 2))
        return x, y

    for i, neighbors in enumerate(game_map.get("adjacency", [])):
        for j in neighbors:
            if 0 <= j < game_map["n"] and i < j:
                x0, y0 = pos(i)
                x1, y1 = pos(j)
                canvas.line(x0, y0, x1, y1, (220, 220, 220), 1)
    if combat:
        totals = {}
        for row in chart_data.get("combat_zones", []):
            location = row.get("location")
            if location is not None:
                totals[int(location)] = totals.get(int(location), 0) + (row.get("amount") or 0)
        for event in chart_data.get("events", []):
            if event.get("type") == "SIEGE" and event.get("location") is not None:
                location = int(event["location"])
                totals[location] = totals.get(location, 0) + (event.get("amount") or 0)
        for region, amount in totals.items():
            if 0 <= region < game_map["n"]:
                x, y = pos(region)
                radius = min(34, 5 + int(float(amount) ** 0.5) * 2)
                canvas.circle(x, y, radius, (148, 103, 189))
    else:
        for row in chart_data.get("unit_path_rows", []):
            path = [int(v) for v in str(row.get("path", "")).split() if v.isdigit()]
            color = (31, 119, 180) if row.get("side") == "A" else (214, 39, 40)
            points = [pos(p) for p in path if 0 <= p < game_map["n"]]
            for a, b in zip(points, points[1:]):
                canvas.line(a[0], a[1], b[0], b[1], color, 2)
    strongholds = set(game_map.get("strongholds", []))
    for i in range(game_map["n"]):
        x, y = pos(i)
        canvas.circle(x, y, 3, (255, 191, 0) if i in strongholds else (120, 120, 120))
    canvas.circle(*pos(0), 8, (31, 119, 180))
    canvas.circle(*pos(game_map["n"] - 1), 8, (214, 39, 40))
    canvas.save_png(charts_dir / filename)
    made.append(filename)


_FONT_5X7 = {
    "0": ["01110", "10001", "10011", "10101", "11001", "10001", "01110"],
    "1": ["00100", "01100", "00100", "00100", "00100", "00100", "01110"],
    "2": ["01110", "10001", "00001", "00010", "00100", "01000", "11111"],
    "3": ["11110", "00001", "00001", "01110", "00001", "00001", "11110"],
    "4": ["00010", "00110", "01010", "10010", "11111", "00010", "00010"],
    "5": ["11111", "10000", "10000", "11110", "00001", "00001", "11110"],
    "6": ["00110", "01000", "10000", "11110", "10001", "10001", "01110"],
    "7": ["11111", "00001", "00010", "00100", "01000", "01000", "01000"],
    "8": ["01110", "10001", "10001", "01110", "10001", "10001", "01110"],
    "9": ["01110", "10001", "10001", "01111", "00001", "00010", "11100"],
    ".": ["00000", "00000", "00000", "00000", "00000", "01100", "01100"],
    "-": ["00000", "00000", "00000", "11111", "00000", "00000", "00000"],
    "/": ["00001", "00010", "00010", "00100", "01000", "01000", "10000"],
    "A": ["01110", "10001", "10001", "11111", "10001", "10001", "10001"],
    "B": ["11110", "10001", "10001", "11110", "10001", "10001", "11110"],
    "C": ["01110", "10001", "10000", "10000", "10000", "10001", "01110"],
    "D": ["11110", "10001", "10001", "10001", "10001", "10001", "11110"],
    "E": ["11111", "10000", "10000", "11110", "10000", "10000", "11111"],
    "F": ["11111", "10000", "10000", "11110", "10000", "10000", "10000"],
    "G": ["01110", "10001", "10000", "10111", "10001", "10001", "01111"],
    "H": ["10001", "10001", "10001", "11111", "10001", "10001", "10001"],
    "I": ["01110", "00100", "00100", "00100", "00100", "00100", "01110"],
    "J": ["00001", "00001", "00001", "00001", "10001", "10001", "01110"],
    "K": ["10001", "10010", "10100", "11000", "10100", "10010", "10001"],
    "L": ["10000", "10000", "10000", "10000", "10000", "10000", "11111"],
    "M": ["10001", "11011", "10101", "10101", "10001", "10001", "10001"],
    "N": ["10001", "11001", "10101", "10011", "10001", "10001", "10001"],
    "O": ["01110", "10001", "10001", "10001", "10001", "10001", "01110"],
    "P": ["11110", "10001", "10001", "11110", "10000", "10000", "10000"],
    "Q": ["01110", "10001", "10001", "10001", "10101", "10010", "01101"],
    "R": ["11110", "10001", "10001", "11110", "10100", "10010", "10001"],
    "S": ["01111", "10000", "10000", "01110", "00001", "00001", "11110"],
    "T": ["11111", "00100", "00100", "00100", "00100", "00100", "00100"],
    "U": ["10001", "10001", "10001", "10001", "10001", "10001", "01110"],
    "V": ["10001", "10001", "10001", "10001", "10001", "01010", "00100"],
    "W": ["10001", "10001", "10001", "10101", "10101", "10101", "01010"],
    "X": ["10001", "10001", "01010", "00100", "01010", "10001", "10001"],
    "Y": ["10001", "10001", "01010", "00100", "00100", "00100", "00100"],
    "Z": ["11111", "00001", "00010", "00100", "01000", "10000", "11111"],
}


def _write_status(out_dir: Path, made: list[str], errors: list[str], renderer: str) -> None:
    status = {
        "charts_generated": made,
        "chart_count": len(made),
        "errors": errors,
        "renderer": renderer,
        "ok": bool(made) and (renderer in {"pillow_fallback", "stdlib_png_fallback"} or not errors),
    }
    (out_dir / "chart_generation_status.json").write_text(json.dumps(status, ensure_ascii=False, indent=2), encoding="utf-8")
