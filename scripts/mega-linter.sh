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

# On pre-commit runs, always apply available fixes and keep the output focused on
# warnings and errors.
if [ "${MEGALINTER_PRE_COMMIT:-}" = 1 ]; then
  # Ignore any local ENABLE_LINTERS override,
  # so that pre-commit always runs all linters specified in .mega-linter.yml.
  unset ENABLE_LINTERS

  envs+=(
    -e "APPLY_FIXES=all"
    -e "LOG_LEVEL=warning"
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

# Pull the image only when it is missing locally.
# `docker compose pull` always prints a "Skipped" line for an already-present
# image, so check for the image ourselves and pull only when missing. This keeps
# the output of the pull command empty unless an actual download happens.
image=$(docker compose -f compose.mega-linter.yml config --images megalinter 2>/dev/null)
if ! docker image inspect "${image}" >/dev/null 2>&1; then
  docker compose \
    -f compose.mega-linter.yml \
    pull \
    megalinter
fi
exec docker compose \
  --progress quiet \
  -f compose.mega-linter.yml \
  run --rm \
  "${envs[@]}" \
  megalinter
