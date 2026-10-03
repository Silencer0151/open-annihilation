#!/usr/bin/env python3
# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

"""Generate the facts and schemas of the standard hack pages from the registry.

Reads src/data/mod-profile/registry/hack-registry.yaml and keeps two kinds of
Markdown in step with it:

  docs/mods/standard-hacks/<hack id>.md
      one page per hack, whose first line, its heading, is the hack's title.
      A missing page is created as a skeleton: the title, the facts block, the headings Description, Configuration
      example and Details with a placeholder each for the prose, and the
      heading Full configuration schema with the schema block. The facts block
      gives the hack's id, its area, scope, the machines that run it, its number of
      parameters and whether the engine implements it; the schema block gives
      how a profile switches it on, every parameter with its type, unit,
      allowed values, list length, 3.1c baseline, default, who may adjust it,
      scope and the data key that overrides it, the presets, the constraints,
      the data keys that need the hack on, and a YAML block with every
      parameter at its default.
  docs/mods/README.md
      the standard hacks table: one row per hack, grouped by area, with its
      title linking to its page, its id, one sentence on what it does, its
      scope and its status. Areas and the hacks within each follow their
      titles alphabetically, as Developer Mode lists them. A missing file is
      created holding a title and the table.

The titles of the hacks and their areas are the registry's (title and
area-titles). Besides a page's heading, only the text between a
'<!-- BEGIN GENERATED: name -->' line and its '<!-- END GENERATED: name -->'
line is written; everything else on a page is prose and stays as it is. A page
or README that has lost a block's markers or its heading is an error, as is a
hack with no sentence in SENTENCES (the registry's summaries are notes for the
engine's developers; the table says in one sentence what each hack does for a
player). A page in docs/mods/standard-hacks that names no hack is reported as
stale.

  gen_mod_docs.py              write the pages and the table
  gen_mod_docs.py --check      write nothing; exit 1 when a block or page differs or is stale
  gen_mod_docs.py --self-test  check the tool itself on a small registry
"""

import argparse
import re
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import oamod_yaml  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
REGISTRY = ROOT / "src/data/mod-profile/registry/hack-registry.yaml"
DOCS = ROOT / "docs/mods"
PAGES = "standard-hacks"

# What each hack does, in one sentence, for the standard hacks table.
SENTENCES = {
    "ai.income-multipliers":
        "Sets the computer player's production and reclaim income multipliers for each difficulty.",
    "ai.difficulty-names":
        "Reorders the difficulty names, so each difficulty selects a different AI profile.",
    "ai.squad5-factory-tick":
        "The AI commander's third task runs the factory and economy tick, so computer players build and fire "
        "stockpile weapons.",
    "ai.factory-tick-filter":
        "The AI factory tick skips buildings with queued background orders and switches power on any building "
        "that uses enough energy.",
    "ai.builder-withhold-threshold":
        "An AI commander stops taking builder jobs once the computer player owns a set number of builders.",
    "ai.commander-keeps-orders-when-damaged":
        "A damaged AI commander keeps its orders instead of clearing its queue.",
    "ai.attack-wave-size":
        "Sets how many units the AI's land and naval attack groups gather before they attack.",
    "ai.patrol-group-size":
        "Sets how many units the AI's patrol group gathers before it scouts a map edge.",
    "ai.patrol-null-enemy-skip":
        "When the AI's forces have no position, its patrol goes to a random map edge instead of attacking a "
        "missing enemy.",
    "ai.squad-assignment":
        "Idle AI units are grouped into squads and given standing orders by role.",
    "ai.nearest-enemy-filter":
        "AI land attack groups ignore fully submerged enemies when they choose the nearest target.",
    "orders.selective-weapon-occupy":
        "An attack order occupies only the ordered weapon, leaving the others free to choose their own targets.",
    "orders.weapons-free-while-busy":
        "A builder's weapons stay free to fire while it builds, repairs, reclaims or captures.",
    "orders.repairing-state-target-activity":
        "The repair order's finishing check looks at the target's activity, not the repairing unit's.",
    "orders.reclaim-command-any-unit":
        "In Reclaim mode the cursor offers reclaim over any unit; the reclaim still refuses what it cannot take.",
    "orders.resurrector-reclaims-features":
        "The Reclaim command reclaims a wreck even when the builder can resurrect it; right-click still "
        "resurrects.",
    "orders.build-site-kickout":
        "Players may place buildings over their own units, which then clear the site, and blocked builders "
        "retry longer.",
    "orders.con-patrol-guard-options":
        "Each player chooses what patrolling and guarding builders do, for each standing move order.",
    "air.no-repair-retreat-flag":
        "Aircraft with a chosen unit flag never break off to find a repair pad when damaged.",
    "air.guard-respects-hold-position":
        "Guarding aircraft engage an attacker only when their move order is not Hold Position.",
    "weapons.retarget-out-of-range":
        "A weapon drops a target that leaves its range and acquires a new one.",
    "weapons.high-arc-ballistic":
        "Ballistic weapons fire on the high arc when the flat arc is below the barrel's minimum angle.",
    "weapons.timed-shell-detonation":
        "A ballistic shell whose timer runs out explodes, unless its weapon opts out, instead of fizzling.",
    "weapons.vlaunch-before-turret":
        "A weapon that is both vertical-launch and turreted fires as a vertical-launch weapon.",
    "repair.rate":
        "Repair heals in proportion to the builder's work time and the unit's build time instead of one hit "
        "point a step.",
    "repair.healtime-self-heal":
        "Self-repair skips unfinished units and takes its pace and amount from the unit's heal time.",
    "veterancy.model":
        "Veterancy levels come from per-type kill thresholds, with adjustable bonuses, caps and accuracy.",
    "economy.deterministic-wind":
        "Wind changes come from one generator seeded the same on every machine.",
    "economy.stats-exclude-shared-income":
        "Resources received from allies no longer count as produced in the statistics.",
    "intel.allied-los-sharing":
        "Allies share line of sight, radar and sonar coverage and unit visibility.",
    "intel.allied-jammers-ignored":
        "Jammers of the viewer and the viewer's allies no longer jam the viewer's radar.",
    "setup.team-start-positions":
        "Start positions are given out by team, with team-mates next to each other.",
    "setup.map-scripted-units":
        "Maps may place units for each player at the start and at set times, with a neutral computer player "
        "for unowned ones.",
    "setup.allow-start-with-ai":
        "A multiplayer game may start with one human and computer players.",
    "setup.multiple-local-ai":
        "One machine may add several computer players to a multiplayer game.",
    "setup.ai-player-name-format":
        "Computer player names include the hosting player's name and slot.",
    "setup.commander-warp":
        "Adds an opt-in start in which the game waits while each player places their commander.",
    "setup.recorder-prebuilt-base":
        "Adds opt-in chat commands that build a prebuilt base for each player at the start.",
    "teams.team-number-alliances":
        "Team numbers set alliances: team-mates ally, other teams do not, and the host can assign teams.",
    "teams.allied-victory-kept":
        "The battle room keeps a player's Allied Victory choice whatever their team.",
    "teams.alliance-menu-all-game-types":
        "The alliance menu opens in every game type, so players can ally computer players outside multiplayer.",
    "sharing.structure-gift-rate-limit":
        "Structure gifts are sent in batches, and a batch over the limit waits.",
    "sharing.take-requires-live-commander":
        "Taking over another player's units is refused while a destroyed commander is still in the world.",
    "sharing.recorder-take-give":
        "Adds opt-in chat commands with which a player lets named players take their units when they go silent.",
    "units.id-reuse-delay":
        "A dead unit's slot is not reused until a delay has passed since its death.",
    "units.ignore-null-death-record":
        "A unit death record for unit 0 is ignored.",
    "units.skip-empty-yardmap":
        "A unit with an empty yard map or no footprint builds no yard map.",
    "units.water-state-rules":
        "A unit's water state is worked out in a new order, and units created under water start submerged.",
    "units.placement-by-builder":
        "The selected builder decides whether a build-menu click places on the map or queues in a factory.",
    "units.mobile-unit-yardmap":
        "Mobile units read their yard map too, not only buildings.",
    "units.build-rotation":
        "Buildings may be placed facing any of four directions when their definition allows it.",
    "units.init-cloaked-after-build":
        "An unfinished unit neither cloaks nor pays for its cloak.",
    "console.ai-control-cheat-group":
        "The +AI and +Control commands join the cheat commands, so they work in multiplayer games with cheats on.",
    "console.lostype-cheat-group":
        "The +LOSType command needs cheats on.",
    "console.atm-amount":
        "Sets how much metal and energy +ATM adds.",
    "console.key-remaps":
        "Moves the repeat-last-command key and adds a second key for the watch toggle.",
    "console.game-speed-range":
        "Keeps the game speed within a set range, which recorder commands can lock for everyone.",
    "network.chat-extension-channel":
        "Chat packets carry private add-on messages to the handlers that take them.",
    "network.vercheck":
        "Clients challenge each other to compare their game data and report any mismatch.",
    "network.vote-reject":
        "Removing a timed-out or unwanted player becomes a vote of the players.",
    "network.lag-guard":
        "While no remote player is heard from, the simulation slows to one step an interval and pausing is "
        "blocked.",
    "network.commander-start-sync":
        "At a set tick each machine sends where its commanders stand, and the others place them there.",
    "network.preserve-remote-player-colour":
        "A remote player's colour and position slot survive player updates during a game.",
    "network.session-desc-clear":
        "The host clears one field of the session description before publishing it.",
    "network.host-stays-as-watcher":
        "A defeated host stays in the game as a watcher, so the session goes on.",
    "network.recorder-session-commands":
        "Adds opt-in recorder chat commands for pausing, starting, watching and choosing random maps.",
    "recorder.ta-demo-recorder":
        "The game recorder is present: it records games and carries recorder messages between players.",
    "recorder.ten-player-replay":
        "A recording of a full ten-player game can be replayed and watched.",
    "ui.megamap":
        "Adds a full-screen strategic map with category icons and sensor rings.",
    "ui.whiteboard":
        "Allies draw lines, dots and text markers on the map for each other.",
    "ui.resource-panel":
        "Watchers see every player's resources and can switch their view to a player and follow it.",
    "ui.camera-sharing":
        "Allies' camera positions show on the minimap, and watchers can follow a player's camera.",
    "ui.allied-unit-display":
        "The minimap and info panel show allied units and their details.",
    "ui.selection-shortcuts":
        "Adds selection shortcuts: same type, filters while dragging and larger queue steps.",
    "ui.build-tools":
        "Adds line and ring building, dragging of queued orders, and a warning colour when units must clear a "
        "site.",
    "ui.click-snap":
        "Build and reclaim cursors snap to the nearest metal spot or feature.",
    "ui.build-preview":
        "Shows the building under the build cursor before it is placed.",
    "ui.options-dialog":
        "Adds a settings dialog and options page for the added settings, and a chat macro key.",
    "ui.share-dialog-and-lobby-buttons":
        "Expands the share dialog and adds buttons to the battle room.",
    "ui.text-rendering":
        "Sends and reads chat as UTF-8, for every language, and can force the message log's backdrop on.",
    "ui.veterancy-label":
        "The info panel shows a unit's veterancy level as a number.",
    "ui.map-features-ignore-los":
        "Features the map places are drawn whether or not they are in line of sight.",
    "ui.unit-voice-fixes":
        "Changes when the reclaim and landing voices play.",
    "ui.effects-tweaks":
        "Changes explosion effects: end-smoke weapons add the explosion, and the extra smoke puff can go.",
    "ui.interface-fixes":
        "A set of small display fixes, each chosen by name.",
    "ui.endgame-stats":
        "End-of-game statistics list players who dropped or were removed.",
    "ui.audio":
        "Audio changes: 3D sound, music from numbered or found files, and limits on announcements.",
    "ui.display-modes":
        "Display changes: a 1024x768 minimum, the menu resolution and screenshots.",
    "ui.external-exports":
        "Exports live unit state and the battle room's state for other programs.",
}

# The machines that run a hack, by the registry's ownership classes.
OWNERS = {
    "all-machines": "every machine in the game",
    "owner-only": "the machine that owns the unit or player concerned",
    "host": "the host's machine",
    "local-view": "each machine, for its own view",
    "all-machines by construction": "every machine, each reaching the same result",
}
SCOPES = {
    "sim": "sim: part of the profile hash; every machine must agree",
    "view": "view: local display only; each player may differ",
}
ADJUSTABLE = {
    "fixed": "only the profile sets it",
    "install": "the player's settings may set it when the profile binds it under `settings`",
    "match": "as `install`, and the host may also change it for one game",
}
PLAIN = re.compile(r"^[A-Za-z][A-Za-z0-9_-]*$")
RESERVED_WORDS = {"true", "false", "null", "~"}


class DocsError(Exception):
    pass


# ---------------------------------------------------------------------------------------------- values
def yaml_value(v):
    """A value as a profile writes it: flow lists, quoted strings unless plainly a word."""
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, (int, float)):
        return repr(v)
    if isinstance(v, list):
        return "[" + ", ".join(yaml_value(x) for x in v) + "]"
    if isinstance(v, str):
        if PLAIN.match(v) and v.lower() not in RESERVED_WORDS:
            return v
        return '"' + v.replace("\\", "\\\\").replace('"', '\\"') + '"'
    raise DocsError(f"cannot write {v!r} as YAML")


def code(text):
    """Text in backticks, safe inside a table cell."""
    text = str(text).replace("|", "\\|")
    return f"`` {text} ``" if "`" in text else f"`{text}`"


def runs_on(ownership):
    """The registry's ownership in words."""
    def one(part):
        part = part.strip()
        if part not in OWNERS:
            raise DocsError(f"unknown ownership {part!r}")
        return OWNERS[part]
    if ownership in OWNERS:
        text = OWNERS[ownership]
    elif " / " in ownership:
        text = " or ".join(one(p) for p in ownership.split(" / ")) + ", depending on the case"
    else:
        text = ", and ".join(one(p) for p in ownership.split(" + "))
    return text[0].upper() + text[1:] + "."


def allowed(spec):
    """The values a parameter accepts, in words."""
    t = spec["type"]
    parts = []
    if t == "bool":
        parts.append("`true`, `false`")
    if "values" in spec:
        parts.append(", ".join(code(v) for v in spec["values"]))
    lo, hi = spec.get("min"), spec.get("max")
    if lo is not None and hi is not None:
        parts.append(f"{code(yaml_value(lo))} to {code(yaml_value(hi))}")
    elif lo is not None:
        parts.append(f"at least {code(yaml_value(lo))}")
    elif hi is not None:
        parts.append(f"at most {code(yaml_value(hi))}")
    elif t in ("int", "int-or-none", "list<int>"):
        parts.append("any integer")
    elif t in ("decimal", "list<decimal>"):
        parts.append("any number")
    if t == "int-or-none":
        parts.append("or `none`")
    if spec.get("multiple-of"):
        parts.append(f"a multiple of {code(yaml_value(spec['multiple-of']))}")
    if t in ("string", "list<string>"):
        if spec.get("max-length") is not None:
            parts.append(f"at most {spec['max-length']} characters")
        if spec.get("pattern"):
            parts.append(f"matching {code(spec['pattern'])}")
        if not spec.get("max-length") and not spec.get("pattern"):
            parts.append("any text")
    if t == "set<enum>":
        parts.append("each at most once")
    elif spec.get("distinct"):
        parts.append("no item twice")
    if spec.get("order") == "ascending":
        parts.append("ascending")
    return "; ".join(parts)


def length(spec):
    n = spec.get("length")
    if isinstance(n, list):
        return f"{n[0]} to {n[1]}"
    if isinstance(n, int):
        return str(n)
    return "any" if spec["type"].startswith("list<") or spec["type"] == "set<enum>" else "-"


def overridden_by(spec, entries):
    out = []
    for k, what in (("per-unit", "unit"), ("per-weapon", "weapon")):
        ref = spec.get(k)
        if ref:
            usual = entries.get(ref, {}).get("standard-key")
            out.append(f"{what} key {code(ref)}" + (f" (usually {code(usual)})" if usual else ""))
    return "; ".join(out) or "-"


def hacks(reg):
    return {k: e for k, e in reg["entries"].items() if e.get("kind") == "hack"}


def title_order(title):
    """The key that sorts titles alphabetically, as Developer Mode lists them."""
    return (title.casefold(), title)


def area_title(reg, area):
    return reg.get("area-titles", {}).get(area, area)


# ---------------------------------------------------------------------------------------------- blocks
def begin(name):
    return f"<!-- BEGIN GENERATED: {name} -->"


def end(name):
    return f"<!-- END GENERATED: {name} -->"


def block(name, body):
    return f"{begin(name)}\n{body.rstrip()}\n{end(name)}"


def replace_block(text, name, body, where):
    """The text with the named block's contents replaced; DocsError when its markers are missing."""
    b, e = text.find(begin(name)), text.find(end(name))
    if b < 0 or e < b or text.count(begin(name)) != 1 or text.count(end(name)) != 1:
        raise DocsError(f"{where}: needs exactly one '{begin(name)}' ... '{end(name)}' pair")
    return text[:b] + block(name, body) + text[e + len(end(name)):]


def facts(eid, e, reg):
    area = eid.split(".")[0]
    rows = [
        ("Hack id", code(eid)),
        ("Area", f"{area_title(reg, area)} (`{area}`)"),
        ("Scope", SCOPES[e["scope"]]),
        ("Runs on", runs_on(e["ownership"])),
        ("Parameters", str(len(e["params"]))),
        ("Status", "implemented" if e["implemented"] else
         "not yet implemented: a profile that turns it on is refused"),
    ]
    return "| Fact | Value |\n| --- | --- |\n" + "".join(f"| {k} | {v} |\n" for k, v in rows)


def schema(eid, e, entries):
    params = e["params"]
    out = [f"Hack id {code(eid)}, written under the profile's `hacks` block.", ""]
    ways = [(f"{eid}: true", "On, every parameter at its default." if params else "On.")]
    if params:
        first = next(iter(params))
        ways.append((f"{eid}: {{{first}: {yaml_value(params[first]['default'])}}}",
                     "On, the parameters named set and the rest at their defaults."))
    presets = e.get("presets", {})
    if presets:
        name = next(iter(presets))
        ways.append((f"{eid}: {{preset: {name}}}",
                     "On, starting from a preset; parameters named beside `preset` replace its values."))
    sh = e.get("shorthand")
    if sh:
        ways.append((f"{eid}: {yaml_value(params[sh]['default'])}", f"On, the shorthand: a bare value sets `{sh}`."))
    ways.append((f"{eid}: false", "Off, the same as leaving it out: 3.1c behaviour."))
    out += ["| Write | Means |", "| --- | --- |"] + [f"| {code(w)} | {m} |" for w, m in ways] + [""]

    out += ["### Parameters", ""]
    if not params:
        out += ["This hack has no parameters.", ""]
    else:
        out += ["| Parameter | Type | Unit | Allowed | Length | Baseline (3.1c) | Default | Adjustable | Scope "
                "| Overridden by |",
                "| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |"]
        for p, s in params.items():
            cells = [code(p), code(s["type"]), s.get("unit", "-").replace("|", "\\|"), allowed(s), length(s),
                     code(yaml_value(s["baseline"])), code(yaml_value(s["default"])), code(s["adjustable"]),
                     code(s["scope"]), overridden_by(s, entries)]
            out.append("| " + " | ".join(cells) + " |")
        out.append("")
        used = [a for a in ADJUSTABLE if any(s["adjustable"] == a for s in params.values())]
        out += ["Adjustable: " + "; ".join(f"`{a}`: {ADJUSTABLE[a]}" for a in used) + ".", ""]
        notes = []
        for p, s in params.items():
            if s.get("setting"):
                where = ", ".join(f"the {('settings-file value' if k == 'ini' else 'registry value')} "
                                  f"{code(v)}" for k, v in s["setting"].items())
                notes.append(f"`{p}` reads {where} when the profile binds it under `settings`.")
            if s.get("setting-transform"):
                notes.append(f"`{p}` turns a bound value into the parameter by the rule "
                             f"`{s['setting-transform']}`.")
            if s.get("default-from"):
                df = s["default-from"]
                notes.append(f"When nothing sets `{p}`, it is {yaml_value(df['factor'])} times `{df['param']}`.")
            if s.get("clamp-between"):
                lo, hi = s["clamp-between"]
                notes.append(f"A bound or match value of `{p}` is clamped between `{lo}` and `{hi}`.")
        if notes:
            out += [f"- {n}" for n in notes] + [""]

    out += ["### Presets", ""]
    if presets:
        out += ["| Preset | Values |", "| --- | --- |"]
        for name, values in presets.items():
            text = ", ".join(f"{k}: {yaml_value(v)}" for k, v in values.items())
            out.append(f"| {code(name)} | {code('{' + text + '}')} |")
        out.append("")
        if "baseline" in presets:
            out += ["`baseline` sets every parameter to its 3.1c value: the hack is on and plays as 3.1c.", ""]
    else:
        out += ["This hack has no presets.", ""]

    out += ["### Shorthand", ""]
    if sh:
        out += [f"The hack has one parameter, so a bare value sets `{sh}`: "
                f"{code(eid + ': ' + yaml_value(params[sh]['default']))} is "
                f"{code(eid + ': {' + sh + ': ' + yaml_value(params[sh]['default']) + '}')}.", ""]
    elif params:
        out += ["This hack takes no shorthand: write `true`, `false` or a parameter map.", ""]
    else:
        out += ["This hack has no parameters to set: write `true` or `false`.", ""]

    if e.get("constraints"):
        out += ["### Constraints", "", "After resolution the values must keep:", ""]
        out += [f"- {code(c)}" for c in e["constraints"]] + [""]

    keys = [(k, d) for k, d in entries.items() if d.get("kind") == "data-key" and d.get("requires") == eid]
    if keys:
        out += ["### Data keys", "", "These data keys are read only while the hack is on:", "",
                "| Data key | File | Usual key | Overrides |", "| --- | --- | --- | --- |"]
        for k, d in keys:
            ov = code(d["overrides"].rsplit(".", 1)[1]) if d.get("overrides") else "-"
            out.append(f"| {code(k)} | {d['file']} | {code(d.get('standard-key', '-'))} | {ov} |")
        out.append("")

    out += ["### Every parameter at its default", "", "```yaml", "hacks:"]
    if params:
        out.append(f"  {eid}:")
        out += [f"    {p}: {yaml_value(s['default'])}" for p, s in params.items()]
    else:
        out.append(f"  {eid}: true")
    out += ["```"]
    return "\n".join(out)


def titled(text, title, where):
    """The page with its heading, its first line, made the hack's title; DocsError when it has none."""
    first, sep, rest = text.partition("\n")
    if not first.startswith("# "):
        raise DocsError(f"{where}: needs its heading, '# <title>', as its first line")
    return f"# {title}" + sep + rest


def skeleton(title):
    return "\n".join([
        f"# {title}", "",
        block("facts", ""), "",
        "## Description", "", "<!-- WRITE: brief description -->", "",
        "## Configuration example", "", "<!-- WRITE: configuration example -->", "",
        "## Details", "", "<!-- WRITE: details and implementation notes -->", "",
        "## Full configuration schema", "",
        block("schema", ""), "",
    ])


def table(reg, sentences):
    rows = {}
    for eid, e in hacks(reg).items():
        rows.setdefault(eid.split(".")[0], []).append((eid, e))
    out = []
    for area in sorted(rows, key=lambda a: title_order(area_title(reg, a))):
        out += [f"### {area_title(reg, area)}", "", "| Hack | Id | What it does | Scope | Status |",
                "| --- | --- | --- | --- | --- |"]
        for eid, e in sorted(rows[area], key=lambda item: title_order(item[1]["title"])):
            status = "implemented" if e["implemented"] else "not yet implemented"
            out.append(f"| [{e['title']}]({PAGES}/{eid}.md) | {code(eid)} | {sentences[eid]} | {e['scope']} "
                       f"| {status} |")
        out.append("")
    return "\n".join(out)


# ---------------------------------------------------------------------------------------------- files
def expected(reg, docs, sentences):
    """Every generated file's path and expected text, from its current text; raises DocsError."""
    entries = reg["entries"]
    problems = []
    for eid, e in hacks(reg).items():
        if eid.split(".")[0] not in reg.get("area-titles", {}):
            problems.append(f"registry {eid}: area {eid.split('.')[0]} has no name in area-titles")
        if not e.get("title"):
            problems.append(f"registry {eid}: no title")
        if eid not in sentences:
            problems.append(f"registry {eid}: no sentence in SENTENCES")
    if problems:
        raise DocsError("\n".join(problems))
    files = {}
    for eid, e in hacks(reg).items():
        path = docs / PAGES / f"{eid}.md"
        text = path.read_text(encoding="utf-8") if path.exists() else skeleton(e["title"])
        text = titled(text, e["title"], path)
        text = replace_block(text, "facts", facts(eid, e, reg), path)
        text = replace_block(text, "schema", schema(eid, e, entries), path)
        files[path] = text
    readme = docs / "README.md"
    text = readme.read_text(encoding="utf-8") if readme.exists() else \
        "# Mods\n\n" + block("hacks table", "") + "\n"
    files[readme] = replace_block(text, "hacks table", table(reg, sentences), readme)
    return files


def stale_pages(reg, docs):
    folder = docs / PAGES
    if not folder.is_dir():
        return []
    ids = set(hacks(reg))
    return sorted(p for p in folder.glob("*.md") if p.stem not in ids and p.name != "README.md")


def sync(reg, docs, sentences, check, root):
    """Writes (or with check, compares) every generated file; returns the exit status."""
    try:
        files = expected(reg, docs, sentences)
    except DocsError as e:
        print(f"gen_mod_docs: {e}", file=sys.stderr)
        return 2

    def rel(p):
        try:
            return str(p.relative_to(root))
        except ValueError:
            return str(p)

    differ = []
    for path, text in files.items():
        current = path.read_text(encoding="utf-8") if path.exists() else None
        if current != text:
            differ.append(path)
            if not check:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text, encoding="utf-8")
    stale = stale_pages(reg, docs)
    for p in stale:
        print(f"gen_mod_docs: {rel(p)} names no hack in the registry", file=sys.stderr)
    if check:
        if differ:
            print("gen_mod_docs: these pages differ from the registry; run tools/gen_mod_docs.py:",
                  file=sys.stderr)
            for p in differ:
                print(f"  {rel(p)}", file=sys.stderr)
        if differ or stale:
            return 1
        print("gen_mod_docs: every generated block matches the registry")
        return 0
    for p in differ:
        print(f"wrote {rel(p)}")
    return 1 if stale else 0


# ---------------------------------------------------------------------------------------------- self-test
def self_test():
    """Checks the pages, the table, the blocks and the check mode on a small registry."""
    failures = []
    reg = {"area-titles": {"units": "Units", "ui": "Interface", "setup": "Game Setup"}, "entries": {
        "units.example": {
            "kind": "hack", "implemented": True, "area": "units", "title": "Example", "summary": "s", "ownership": "owner-only",
            "scope": "sim", "shorthand": "ticks",
            "params": {"ticks": {"type": "int", "min": 0, "max": 10, "unit": "ticks", "baseline": 0, "default": 5,
                                 "adjustable": "match", "scope": "sim", "per-unit": "units.example-ticks"}},
            "presets": {"baseline": {"ticks": 0}}},
        "units.example-ticks": {"kind": "data-key", "file": "unit", "standard-key": "ExampleTicks",
                                "overrides": "units.example.ticks", "requires": "units.example"},
        "ui.plain": {"kind": "hack", "implemented": False, "area": "ui", "title": "Plain", "summary": "s",
                     "ownership": "local-view + host", "scope": "view", "params": {}},
        "setup.named": {
            "kind": "hack", "implemented": True, "area": "setup", "title": "Named", "summary": "s",
            "ownership": "host",
            "scope": "sim",
            "params": {"format": {"type": "string", "max-length": 8, "baseline": "AI:%s", "default": "",
                                  "adjustable": "fixed", "scope": "sim"},
                       "rows": {"type": "list<decimal>", "length": 2, "baseline": [0.5, 1.0], "default": [1.5, 2.0],
                                "adjustable": "fixed", "scope": "sim"}}},
    }}
    sentences = {"units.example": "Does an example.", "ui.plain": "Draws.", "setup.named": "Names."}

    for v, text in ((True, "true"), (5, "5"), (0.5, "0.5"), ([1, 2], "[1, 2]"), ("none", "none"),
                    ("AI:%s", '"AI:%s"'), ("", '""'), (".tad", '".tad"'), ("true", '"true"'), ("1.0", '"1.0"')):
        if yaml_value(v) != text:
            failures.append(f"{v!r} is written {yaml_value(v)!r}, expected {text!r}")
    for eid, e in hacks(reg).items():
        written = schema(eid, e, reg["entries"]).split("```yaml\n", 1)[1].split("```", 1)[0]
        read = oamod_yaml.loads(written)["hacks"][eid]
        want = {p: s["default"] for p, s in e["params"].items()} or True
        if read != want:
            failures.append(f"{eid}: the default block reads back as {read!r}, expected {want!r}")
    if runs_on("local-view + host") != "Each machine, for its own view, and the host's machine.":
        failures.append(f"ownership is described wrong: {runs_on('local-view + host')}")
    page = schema("units.example", reg["entries"]["units.example"], reg["entries"])
    for needle in ("`units.example: 5`", "usually `ExampleTicks`", "| `baseline` |", "`0` to `10`",
                   "the host may also change it", "| `units.example-ticks` | unit |"):
        if needle not in page:
            failures.append(f"the schema of units.example lacks {needle!r}")

    with tempfile.TemporaryDirectory() as tmp:
        docs = Path(tmp) / "docs"
        quiet = open(Path(tmp) / "log", "w")
        saved = sys.stdout, sys.stderr
        sys.stdout = sys.stderr = quiet
        try:
            first_check = sync(reg, docs, sentences, True, Path(tmp))
            written = sync(reg, docs, sentences, False, Path(tmp))
            clean = sync(reg, docs, sentences, True, Path(tmp))
            page_path = docs / PAGES / "units.example.md"
            prose = page_path.read_text().replace("<!-- WRITE: brief description -->", "It does an example.")
            page_path.write_text(prose)
            kept = sync(reg, docs, sentences, True, Path(tmp))
            page_path.write_text(prose.replace("| Parameters | 1 |", "| Parameters | 2 |"))
            edited = sync(reg, docs, sentences, True, Path(tmp))
            page_path.write_text(prose.replace("# Example", "# units.example"))
            heading = sync(reg, docs, sentences, True, Path(tmp))
            sync(reg, docs, sentences, False, Path(tmp))
            restored = page_path.read_text() == prose
            titled_first = prose.startswith("# Example\n")
            (docs / PAGES / "units.gone.md").write_text("# units.gone\n")
            stale = sync(reg, docs, sentences, True, Path(tmp))
            (docs / PAGES / "units.gone.md").unlink()
            page_path.write_text(prose.replace(end("schema"), ""))
            broken = sync(reg, docs, sentences, True, Path(tmp))
            page_path.write_text(prose.replace("# Example\n", "Example\n"))
            headless = sync(reg, docs, sentences, True, Path(tmp))
            page_path.write_text(prose)
            unnamed = sync(reg, docs, {"units.example": "x", "ui.plain": "y"}, True, Path(tmp))
            readme = (docs / "README.md").read_text()
        finally:
            sys.stdout, sys.stderr = saved
            quiet.close()
        for label, got, want in (("a check before writing", first_check, 1), ("writing", written, 0),
                                 ("a check after writing", clean, 0), ("a check after prose", kept, 0),
                                 ("a check after a generated block changed", edited, 1),
                                 ("a check after the heading changed", heading, 1),
                                 ("a check of a page without a heading", headless, 2),
                                 ("a check with a stale page", stale, 1),
                                 ("a check with a lost marker", broken, 2),
                                 ("a check with a hack without a sentence", unnamed, 2)):
            if got != want:
                failures.append(f"{label} returned {got}, expected {want}")
        if not restored:
            failures.append("writing did not keep the prose and restore the generated block and heading")
        if not titled_first:
            failures.append("a new page is not headed by the hack's title")
        for needle in ("# Mods", "### Units", "### Game Setup", "### Interface",
                       "| [Plain](standard-hacks/ui.plain.md) | `ui.plain` | Draws. | view | not yet implemented |"):
            if needle not in readme:
                failures.append(f"the README lacks {needle!r}")
        if not readme.index("### Game Setup") < readme.index("### Interface") < readme.index("### Units"):
            failures.append("the table does not follow the areas' titles alphabetically")

    for f in failures:
        print("FAIL", f)
    print(f"gen_mod_docs self-test: {len(failures)} failures")
    return 1 if failures else 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="write nothing; exit 1 when a block differs")
    parser.add_argument("--self-test", action="store_true", help="check the tool itself on a small registry")
    args = parser.parse_args(argv)
    if args.self_test:
        return self_test()
    try:
        reg = oamod_yaml.load_file(REGISTRY)
    except oamod_yaml.OamodYamlError as e:
        print(f"gen_mod_docs: {e}", file=sys.stderr)
        return 2
    return sync(reg, DOCS, SENTENCES, args.check, ROOT)


if __name__ == "__main__":
    sys.exit(main())
