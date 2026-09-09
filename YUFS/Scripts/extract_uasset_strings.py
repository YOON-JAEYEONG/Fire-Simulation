from pathlib import Path
import re
import sys


def extract_strings(data: bytes):
    ascii_strings = [m.group().decode("ascii", "ignore") for m in re.finditer(rb"[ -~]{4,}", data)]
    utf16_strings = []
    for match in re.finditer(rb"(?:[ -~]\x00){4,}", data):
        utf16_strings.append(match.group().decode("utf-16le", "ignore"))
    return ascii_strings + utf16_strings


for filename in sys.argv[1:]:
    path = Path(filename)
    print(f"===== {path} =====")
    strings = extract_strings(path.read_bytes())
    for index, value in enumerate(strings):
        if any(token.lower() in value.lower() for token in ("exit", "uaid", "label", "actor", "main", "emergency")):
            start = max(0, index - 3)
            end = min(len(strings), index + 4)
            for nearby in strings[start:end]:
                print(nearby)
            print("---")
