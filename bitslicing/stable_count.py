from common import *

def options_function(center, oncount, unkcount):
    # center is one of OFF, ON, UNKNOWN

    if center == ON:
        lower = oncount - 1
        upper = oncount - 1 + unkcount
        r = range(lower, upper + 1)

        if upper < 2: return ABORT
        if lower > 3: return ABORT
        return StableOptions(not 2 in r, not 3 in r, True, True, True, True, True, True)

    if center == OFF:
        lower = oncount
        upper = oncount + unkcount
        r = range(lower, upper + 1)

        if lower == 3 and upper == 3: return ABORT
        if lower > 6: return ABORT
        return StableOptions(True, True, not 0 in r, not 1 in r, not 2 in r, not 4 in r, not 5 in r, not 6 in r)

    if center == UNKNOWN:
        lower = oncount
        upper = oncount + unkcount - 1
        r = range(lower, upper + 1)

        if lower > 6: return ABORT
        return StableOptions(not 2 in r, not 3 in r, not 0 in r, not 1 in r, not 2 in r, not 4 in r, not 5 in r, not 6 in r)

def emit_boolean(state, live_count, unknown_count, result):
    inputs = int2bin(state, 2) + \
        int2bin(live_count, 3) + int2bin(unknown_count, 4)
    if result == ABORT:
        outputs = "--------" + "1"
    else:
        outputs = result.espresso_str() + "0"

    return f"{inputs} {outputs}\n"

def emit_rule(live_count, unknown_count):
    result = ""

    result += emit_boolean(OFF, live_count, unknown_count,
                           options_function(OFF, live_count, unknown_count))
    if live_count > 0:
        result += emit_boolean(ON, live_count, unknown_count,
                               options_function(ON, live_count, unknown_count))

    if unknown_count > 0:
        result += emit_boolean(UNKNOWN, live_count, unknown_count,
                               options_function(UNKNOWN, live_count, unknown_count))

    return result

innames = ["stateunk", "stateon", "on2", "on1", "on0", "unk3", "unk2", "unk1", "unk0"]
outnames = ["live2", "live3",
            "dead0", "dead1", "dead2", "dead4", "dead5", "dead6",
            "abort"]

data = f""".i {len(innames)}
.o {len(outnames)}
.type fr
"""

for live_count in range(0,6+1):
    for unknown_count in range(0,10-live_count):
        data += emit_rule(live_count, unknown_count)

run_espresso(data, innames, outnames)
