#!/usr/bin/env python3
"""Kernel .config helpers for the PC kernel.

  kconfig.py merge OUT BASE [FRAGMENT...]   BASE with each fragment applied in order (later wins)
  kconfig.py check REQUIRED CONFIG [IGNORED] list what CONFIG misses from REQUIRED (exit 1 if any),
                                            skipping the option names listed in IGNORED

REQUIRED is Android's android-base.config: options that must be built in (=y) or must stay unset.
"""
import re
import sys

LINE = re.compile(r'^(CONFIG_\w+)=(.*)$')
UNSET = re.compile(r'^# (CONFIG_\w+) is not set$')


def parse_config(text):
    config = {}
    for line in text.splitlines():
        value = LINE.match(line)
        unset = UNSET.match(line)
        config.update({value.group(1): value.group(2)} if value else {})
        config.update({unset.group(1): None} if unset else {})
    return config


def merge(base, *fragments):
    merged = dict(base)
    for fragment in fragments:
        merged.update(fragment)
    return merged


def render(config):
    return ''.join(f'# {name} is not set\n' if value is None else f'{name}={value}\n'
                   for name, value in config.items())


def find_unmet(required, actual, ignored=frozenset()):
    return [(name, wanted, actual.get(name)) for name, wanted in required.items()
            if name not in ignored and actual.get(name) != wanted]


def read_names(path):
    return {line.split('#')[0].strip() for line in open(path)} - {''}


def read(path):
    with open(path) as f:
        return parse_config(f.read())


def run_merge(out, base, *fragments):
    with open(out, 'w') as f:
        f.write(render(merge(read(base), *map(read, fragments))))
    return 0


def run_check(required, config, ignored_list=None):
    unmet = find_unmet(read(required), read(config), read_names(ignored_list) if ignored_list else frozenset())
    for name, wanted, got in unmet:
        print(f'{name}: wanted {wanted or "unset"}, got {got or "unset"}')
    return 1 if unmet else 0


COMMANDS = {'merge': run_merge, 'check': run_check}


if __name__ == '__main__':
    sys.exit(COMMANDS[sys.argv[1]](*sys.argv[2:]))
