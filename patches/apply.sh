#!/bin/bash
# Applies the patches this device carries for projects we do not fork (patches/<project path>/*.patch,
# in name order, with git am so the original author stays). Run from the top of the tree after every
# `repo sync`: a patch already in a project is skipped.
#   device/pc/pc_x86_64/patches/apply.sh
set -euo pipefail
TOP=$(cd "$(dirname "$0")/../../../.." && pwd)
HERE=$(cd "$(dirname "$0")" && pwd)
cd "$HERE"
find . -name '*.patch' -printf '%h\n' | sort -u | while read -r dir; do
	project=${dir#./}
	for patch in "$HERE/$project"/*.patch; do
		subject=$(sed -n 's/^Subject: \(\[PATCH[^]]*\] \)\?//p' "$patch" | head -1)
		if git -C "$TOP/$project" log --format=%s -200 | grep -qxF -- "$subject"; then
			echo "$project: already has \"$subject\""
			continue
		fi
		git -C "$TOP/$project" am -q --3way "$patch"
		echo "$project: applied \"$subject\""
	done
done
