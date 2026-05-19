#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later

# mirakc-arib
# Copyright (C) 2019 masnagam
#
# This program is free software; you can redistribute it and/or modify it under the terms of the
# GNU General Public License as published by the Free Software Foundation; either version 2 of the
# License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
# without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See
# the GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along with this program; if
# not, write to the Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
# 02110-1301, USA.

set -eu

cd "$(dirname "$0")/.."

envs=()

# Tweak the run for pre-commit:
# - Ignore any local ENABLE_LINTERS override so that all linters specified in
#   .mega-linter.yml always run.
# - Lower the log level to keep the output focused on warnings and errors.
# - Apply fixes so they can be staged for the commit.
if [ "${MEGALINTER_PRE_COMMIT:-}" = 1 ]; then
  unset ENABLE_LINTERS

  envs+=(
    -e "LOG_LEVEL=warning"
    -e "APPLY_FIXES=all"
  )
fi

# When file paths are passed as arguments, lint only those files in both
# pre-commit and manual runs.
# MegaLinter expects MEGALINTER_FILES_TO_LINT to be a comma-separated list.
#
# Project-only linters are skipped because they cannot run on a file subset.
if [ "$#" -gt 0 ]; then
  files=$(
    IFS=,
    printf '%s' "$*"
  )
  envs+=(
    -e SKIP_CLI_LINT_MODES=project
    -e "MEGALINTER_FILES_TO_LINT=${files}"
  )
fi

# `docker compose pull` always prints a "Skipped" line for an already-present image.
# To keep the pull command's output empty unless an actual download happens,
# we check for the image ourselves and pull only when missing locally.
image=$(docker compose -f compose.mega-linter.yml config --images megalinter 2>/dev/null)
if ! docker image inspect "${image}" >/dev/null 2>&1; then
  docker compose \
    -f compose.mega-linter.yml \
    pull \
    megalinter
fi
# On bash older than 4.4 (e.g. the bash 3.2 shipped with macOS), a plain
# `"${envs[@]}"` on an empty array triggers an unbound-variable error under `set -u`.
# The `${envs[@]+...}` form avoids this: it expands to nothing when `envs` is empty.
exec docker compose \
  --progress quiet \
  -f compose.mega-linter.yml \
  run --rm \
  ${envs[@]+"${envs[@]}"} \
  megalinter
