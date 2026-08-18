#!/usr/bin/env python3
"""
wigle_fetch.py - Generate a large realistic SSID list for ghost_device firmware

Sources (mix any combination):
  --wigle   Fetch from WiGLE API by lat/lon
  --csv     Parse a local WiGLE CSV export
  --gen N   Generate N synthetic SSIDs using realistic patterns
  --all     Do all three and merge

Output: ssid_list.h for the ESP32 Arduino sketch

Usage examples:

  # WiGLE API only
  python3 wigle_fetch.py --wigle --lat 40.0 --lon -105.2 \
      --api-name MYNAME --api-token MYTOKEN

  # CSV export + 1000 generated
  python3 wigle_fetch.py --csv ~/WigleWifi.csv --gen 1000

  # Generate 2000 synthetic SSIDs (no WiGLE needed)
  python3 wigle_fetch.py --gen 2000

  # Everything, max coverage
  python3 wigle_fetch.py --wigle --lat 40.0 --lon -105.2 \
      --api-name NAME --api-token TOKEN \
      --csv ~/WigleWifi.csv --gen 1500 --max 3000

Get WiGLE API credentials: https://wigle.net/account
"""

import argparse
import sys
import re
import time
import random
import base64
import string
from pathlib import Path

try:
    import requests
    HAS_REQUESTS = True
except ImportError:
    HAS_REQUESTS = False

# ── SSID cleaning ──────────────────────────────────────────────────────────────

SSID_BLACKLIST_PREFIX = [
    'DIRECT-',    # Wi-Fi Direct (printers, TVs, etc. - not phone-like)
]
SSID_BLACKLIST_EXACT = {
    '', ' ', 'hidden', 'HIDDEN', '<hidden>',
}
SSID_EXCLUDE_RE = [
    re.compile(r'^\s*$'),
    re.compile(r'^.{0,1}$'),           # too short
    re.compile(r'^.{33,}$'),           # too long for SSID spec (>32 bytes)
    re.compile(r'[\x00-\x1f\x7f-\xff]'), # non-printable
]

def clean_ssid(ssid: str) -> str | None:
    if not ssid:
        return None
    ssid = ssid.strip()
    if ssid in SSID_BLACKLIST_EXACT:
        return None
    for prefix in SSID_BLACKLIST_PREFIX:
        if ssid.startswith(prefix):
            return None
    for rx in SSID_EXCLUDE_RE:
        if rx.search(ssid):
            return None
    if len(ssid.encode('utf-8')) > 32:
        return None
    return ssid

# ── Synthetic SSID generation ──────────────────────────────────────────────────

# Last names for residential SSIDs
LAST_NAMES = [
    "Smith","Johnson","Williams","Brown","Jones","Garcia","Miller","Davis",
    "Rodriguez","Martinez","Hernandez","Lopez","Gonzalez","Wilson","Anderson",
    "Thomas","Taylor","Moore","Jackson","Martin","Lee","Perez","Thompson",
    "White","Harris","Sanchez","Clark","Ramirez","Lewis","Robinson","Walker",
    "Young","Allen","King","Wright","Scott","Torres","Nguyen","Hill","Flores",
    "Green","Adams","Nelson","Baker","Hall","Rivera","Campbell","Mitchell",
    "Carter","Roberts","Gomez","Phillips","Evans","Turner","Diaz","Parker",
    "Cruz","Edwards","Collins","Reyes","Stewart","Morris","Morales","Murphy",
    "Cook","Rogers","Gutierrez","Ortiz","Morgan","Cooper","Peterson","Bailey",
    "Reed","Kelly","Howard","Ramos","Kim","Cox","Ward","Richardson","Watson",
    "Brooks","Chavez","Wood","James","Bennett","Gray","Mendoza","Ruiz",
    "Hughes","Price","Alvarez","Castillo","Sanders","Patel","Myers","Long",
    "Ross","Foster","Jimenez","Powell","Jenkins","Perry","Russell","Sullivan",
    "Bell","Coleman","Butler","Henderson","Barnes","Gonzales","Fisher","Vasquez",
    "Simmons","Romero","Jordan","Patterson","Alexander","Hamilton","Graham",
]

# First names for hotspot-style SSIDs
FIRST_NAMES = [
    "James","John","Robert","Michael","David","William","Richard","Thomas",
    "Charles","Christopher","Matthew","Anthony","Mark","Donald","Steven",
    "Paul","Andrew","Kenneth","Kevin","Brian","Mary","Patricia","Jennifer",
    "Linda","Barbara","Susan","Jessica","Sarah","Karen","Lisa","Nancy",
    "Ashley","Emily","Amanda","Melissa","Deborah","Stephanie","Rebecca",
    "Sharon","Laura","Cynthia","Kathleen","Amy","Angela","Shirley","Anna",
    "Emma","Brenda","Pamela","Emma","Taylor","Madison","Olivia","Isabella",
    "Sophia","Ava","Mia","Charlotte","Amelia","Harper","Evelyn","Abigail",
    "Liam","Noah","Oliver","Elijah","James","Aiden","Lucas","Mason","Ethan",
    "Sebastian","Logan","Jackson","Carter","Luke","Jayden","Owen","Dylan",
    "Ryan","Nathan","Aaron","Jose","Isaiah","Eli","Joshua","Connor","Caleb",
]

# ISP name patterns with hex suffix placeholders
ISP_PATTERNS = [
    "XFINITY-{hex4}",    "xfinity-{hex4}",     "XFINITY-{hex6}",
    "Comcast-2G-{hex4}", "Comcast-5G-{hex4}",  "ComcastWiFi-{hex4}",
    "Spectrum-2G-{hex4}","Spectrum-5G-{hex4}", "SpectrumWiFi-{hex4}",
    "MySpectrumWiFi-{hex4}","Spectrum-{hex4}",
    "ATT-WIFI-{hex4}",   "ATTWifi-{hex4}",     "att-wifi-{hex4}",
    "ATT-WiFi-{hex4}",   "ATTUNITVERSE-{hex2}",
    "CenturyLink-{hex4}","CenturyLink2-{hex4}","CenturyLink5G-{hex4}",
    "Cox-WiFi-{hex4}",   "CoxWifi{hex4}",      "Cox-{hex4}",
    "Verizon-FIOS-{hex4}","FIOS-{hex4}",        "VerizonWiFi-{hex4}",
    "FrontierWifi-{hex4}","Frontier-2G-{hex4}",
    "NETGEAR-{hex4}",    "NETGEAR{hex2}{hex2}", "NETGEAR-{hex4}-5G",
    "TP-LINK_{hex4}",    "TP-LINK_{hex2}{hex2}","TP-Link_{hex4}",
    "ASUS_{hex4}",       "ASUS_RT-{hex4}",      "AsusWiFi-{hex4}",
    "dlink-{hex4}",      "D-Link-{hex4}",        "DLINK-{hex4}",
    "Linksys-{hex4}",    "linksys{hex4}",        "Linksys{hex2}{hex2}",
    "Orbi-{hex4}",       "Orbi-5G-{hex4}",       "ORBI{hex2}-{hex4}",
    "eero-{hex4}",       "eeroNetwork-{hex4}",
    "HUAWEI-{hex4}",     "ZTE_{hex4}",            "Belkin.{hex4}",
    "Arris-{hex4}",      "Motorola-{hex4}",       "Technicolor-{hex4}",
    "2WIRE-{hex4}",      "SBG{hex4}",             "SBG6580-{hex4}",
    "Shaw-{hex4}",       "Rogers-{hex4}",          "Bell{hex4}",
    "Telus-{hex4}",      "Vodafone-{hex4}",        "BT-Hub-{hex4}",
    "Sky{hex4}",         "Virgin-{hex4}",          "EE-{hex4}",
    "CLARO-{hex4}",      "Telmex-{hex4}",
    "Starlink-{hex6}",
]

# Residential pattern templates
RESIDENTIAL_PATTERNS = [
    "{last}_WiFi",       "{last}_Network",      "{last}WiFi",
    "{last}Net",         "{last}_2G",           "{last}_5G",
    "{last}_Home",       "{last}Home",          "{last}-WiFi",
    "{last}-Net",        "{last}-Home",         "The {last}s",
    "{last} Family",     "{last} Wifi",         "{last} Network",
    "{last}s",           "{last}s WiFi",        "{last}s Network",
    "Casa {last}",       "{last} Casa",
]

# Apartment patterns
APT_PATTERNS = [
    "Apt{num3}",    "Apt-{num3}",    "Apt {num3}",
    "Unit{num3}",   "Unit-{num3}",   "#{num3}",
    "Apt{num3}-WiFi","Suite{num3}",   "Unit{alpha}{num1}",
    "Apt-{alpha}{num1}","Floor{num1}-WiFi","{num3}-WiFi",
    "{num3}_{alpha}","Rm{num3}",      "Room{num3}",
]

# Hotspot style
HOTSPOT_PATTERNS = [
    "{first}'s iPhone",  "{first}s iPhone",   "{first}'s Phone",
    "{first}'s iPad",    "{first}'s MacBook",  "{first}'s Hotspot",
    "{first}-iPhone",    "{first}-Android",    "{first}iPhone",
    "{first}'s Galaxy",  "{first}'s Pixel",    "{first}'s Note",
    "{first} iPhone",    "{first} Phone",
]

# Generic home patterns
GENERIC_PATTERNS = [
    "Home-{hex4}",       "HomeNet-{hex4}",     "HomeWiFi-{hex4}",
    "MyHome-{hex4}",     "MyWiFi-{hex4}",      "Network-{hex4}",
    "WiFi-{hex4}",       "Wireless-{hex4}",    "LAN-{hex4}",
    "{num4}","WIFI-{num4}","NET-{num4}",
    "{last}{num4}",      "{last}-{num4}",
    "HOME-{hex4}",       "PRIV-{hex4}",        "SEC-{hex4}",
]

def _hex(n: int) -> str:
    return ''.join(random.choices('0123456789ABCDEF', k=n))

def _num(n: int) -> str:
    return ''.join(random.choices('0123456789', k=n))

def _alpha() -> str:
    return random.choice('ABCDEFGHJKLMNPQRSTUVWXYZ')

def expand(template: str) -> str:
    s = template
    s = s.replace('{hex6}', _hex(6))
    s = s.replace('{hex4}', _hex(4))
    s = s.replace('{hex2}', _hex(2))
    s = s.replace('{num4}', _num(4))
    s = s.replace('{num3}', str(random.randint(100, 999)))
    s = s.replace('{num1}', str(random.randint(1, 9)))
    s = s.replace('{last}',  random.choice(LAST_NAMES))
    s = s.replace('{first}', random.choice(FIRST_NAMES))
    s = s.replace('{alpha}', _alpha())
    return s

def generate_ssids(count: int) -> list[str]:
    """Generate realistic synthetic SSIDs."""
    all_patterns = (
        ISP_PATTERNS * 4 +           # ISP patterns weighted heavily
        RESIDENTIAL_PATTERNS * 3 +
        APT_PATTERNS * 2 +
        HOTSPOT_PATTERNS * 2 +
        GENERIC_PATTERNS * 2 +
    )
    seen = set()
    results = []
    attempts = 0
    max_attempts = count * 10

    while len(results) < count and attempts < max_attempts:
        attempts += 1
        template = random.choice(all_patterns)
        ssid = expand(template)
        ssid = clean_ssid(ssid)
        if ssid and ssid not in seen:
            seen.add(ssid)
            results.append(ssid)

    print(f"[*] Generated {len(results)} synthetic SSIDs")
    return results

# ── WiGLE API fetch ────────────────────────────────────────────────────────────

def fetch_wigle_api(lat: float, lon: float, radius: float,
                    api_name: str, api_token: str,
                    max_results: int = 1000) -> list[str]:
    if not HAS_REQUESTS:
        print("ERROR: pip install requests", file=sys.stderr)
        sys.exit(1)

    credentials = base64.b64encode(f"{api_name}:{api_token}".encode()).decode()
    headers = {
        "Authorization": f"Basic {credentials}",
        "Accept": "application/json",
        "User-Agent": "ghost_device_builder/2.0",
    }

    ssids = []
    seen = set()
    start = 0
    per_page = 100

    print(f"[*] WiGLE API: lat={lat} lon={lon} r={radius} max={max_results}")

    while len(ssids) < max_results:
        params = {
            "latrange1": lat - radius,
            "latrange2": lat + radius,
            "longrange1": lon - radius,
            "longrange2": lon + radius,
            "freenet": "false",
            "paynet": "false",
            "resultsPerPage": per_page,
            "first": start,
        }
        try:
            resp = requests.get("https://api.wigle.net/api/v2/network/search",
                                headers=headers, params=params, timeout=30)
        except requests.RequestException as e:
            print(f"[!] {e}", file=sys.stderr)
            break

        if resp.status_code == 401:
            print("[!] Auth failed", file=sys.stderr); sys.exit(1)
        if resp.status_code == 429:
            print("[!] Rate limited, waiting 60s"); time.sleep(60); continue
        if resp.status_code != 200:
            print(f"[!] HTTP {resp.status_code}"); break

        data = resp.json()
        results = data.get("results", [])
        if not results:
            break

        for net in results:
            ssid = clean_ssid(net.get("ssid", ""))
            if ssid and ssid not in seen:
                seen.add(ssid)
                ssids.append(ssid)

        total = data.get("totalResults", 0)
        print(f"  WiGLE: {len(ssids)} SSIDs (of {total} available)")
        start += per_page
        if start >= total or start >= max_results:
            break
        time.sleep(0.5)

    return ssids

# ── CSV loader ─────────────────────────────────────────────────────────────────

def load_wigle_csv(csv_path: str) -> list[str]:
    ssids = []
    seen = set()

    with open(csv_path, 'r', encoding='utf-8', errors='replace') as f:
        lines = f.readlines()

    ssid_col = 0
    data_start = 0
    for i, line in enumerate(lines):
        if 'SSID' in line and 'BSSID' in line:
            cols = [c.strip().strip('"') for c in line.split(',')]
            ssid_col = cols.index('SSID') if 'SSID' in cols else 0
            data_start = i + 1
            break

    print(f"[*] CSV: {csv_path} (col {ssid_col})")

    for line in lines[data_start:]:
        # Simple CSV split (handles quoted fields)
        parts = next(__import__('csv').reader([line]))
        if ssid_col < len(parts):
            ssid = clean_ssid(parts[ssid_col])
            if ssid and ssid not in seen:
                seen.add(ssid)
                ssids.append(ssid)

    print(f"[*] CSV: loaded {len(ssids)} SSIDs")
    return ssids

# ── Header writer ──────────────────────────────────────────────────────────────

def write_header(ssids: list[str], output: str, shuffle: bool = True):
    if not ssids:
        print("[!] Empty SSID list", file=sys.stderr); sys.exit(1)

    ssids = list(dict.fromkeys(ssids))  # deduplicate, preserve order
    if shuffle:
        random.shuffle(ssids)           # mix sources together

    lines = [
        "#pragma once",
        "// Auto-generated by tools/wigle_fetch.py — do not edit",
        f"// Total SSIDs: {len(ssids)}",
        "",
        f"#define NUM_SSIDS {len(ssids)}",
        "",
        "static const char* const ssid_list[] = {",
    ]
    for ssid in ssids:
        escaped = ssid.replace('\\', '\\\\').replace('"', '\\"')
        lines.append(f'    "{escaped}",')
    lines.append("};")
    lines.append("")

    Path(output).write_text('\n'.join(lines), encoding='utf-8')
    print(f"[+] Wrote {len(ssids)} SSIDs → {output}")
    sz = Path(output).stat().st_size
    print(f"[+] File size: {sz:,} bytes  (flash usage on ESP32: ~{sz:,} bytes)")

# ── CLI ────────────────────────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser(
        description="Build ssid_list.h from WiGLE API, CSV, and/or generated SSIDs",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    ap.add_argument("--output", "-o", default="../ssid_list.h")
    ap.add_argument("--max", type=int, default=2000, help="Max total SSIDs (default 2000)")
    ap.add_argument("--no-shuffle", action="store_true", help="Keep SSIDs in source order")

    # Sources
    ap.add_argument("--wigle",  action="store_true", help="Fetch from WiGLE API")
    ap.add_argument("--csv",    metavar="FILE",       help="Load WiGLE CSV export")
    ap.add_argument("--gen",    type=int, metavar="N",help="Generate N synthetic SSIDs")

    # WiGLE API params
    ap.add_argument("--lat",       type=float)
    ap.add_argument("--lon",       type=float)
    ap.add_argument("--radius",    type=float, default=0.05,
                    help="Search radius in degrees (default 0.05 ≈ 5km)")
    ap.add_argument("--api-name",  metavar="NAME")
    ap.add_argument("--api-token", metavar="TOKEN")

    args = ap.parse_args()

    if not args.wigle and not args.csv and not args.gen:
        ap.error("Specify at least one source: --wigle, --csv, or --gen N")

    all_ssids = []

    if args.wigle:
        if not args.lat or not args.lon:
            ap.error("--lat and --lon required with --wigle")
        if not args.api_name or not args.api_token:
            ap.error("--api-name and --api-token required with --wigle")
        all_ssids += fetch_wigle_api(args.lat, args.lon, args.radius,
                                     args.api_name, args.api_token, args.max)

    if args.csv:
        all_ssids += load_wigle_csv(args.csv)

    if args.gen:
        all_ssids += generate_ssids(args.gen)

    print(f"[*] Total before dedup: {len(all_ssids)}")

    # Deduplicate (case-insensitive aware but preserve original case)
    seen_lower = set()
    deduped = []
    for s in all_ssids:
        sl = s.lower()
        if sl not in seen_lower:
            seen_lower.add(sl)
            deduped.append(s)

    print(f"[*] After dedup: {len(deduped)}")

    if len(deduped) > args.max:
        deduped = deduped[:args.max]
        print(f"[*] Trimmed to {args.max}")

    write_header(deduped, args.output, shuffle=not args.no_shuffle)

if __name__ == "__main__":
    main()
