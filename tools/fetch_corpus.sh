#!/usr/bin/env bash
# fetch_corpus.sh -- download "The Complete Works of William Shakespeare" into data/raw/
#
# Source : GitHub mirror of http://shakespeare.mit.edu/ (repo: TheMITTech/shakespeare)
# Usage  : bash tools/fetch_corpus.sh [--force]
#          --force  replace an existing data/raw/ (the old copy is kept as
#                   data/raw.bak.<timestamp>)
#
# After a successful run:
#   data/raw/**/*.html   complete mirror of the site (one folder per work)
#   data/raw/SOURCE.txt  repo URL, exact commit, fetch date, file counts

set -euo pipefail

REPO_URL="https://github.com/TheMITTech/shakespeare.git"
REPO_WEB="https://github.com/TheMITTech/shakespeare"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
RAW_DIR="${PROJ_DIR}/data/raw"

FORCE=0
if [[ "${1:-}" == "--force" ]]; then FORCE=1; fi

# sanity guard: only ever touch <proj>/data/raw
if [[ "${RAW_DIR}" != */data/raw ]]; then
  echo "refusing to touch unexpected path: ${RAW_DIR}" >&2
  exit 1
fi

if [[ -d "${RAW_DIR}" && -n "$(ls -A "${RAW_DIR}" 2>/dev/null || true)" ]]; then
  if [[ ${FORCE} -eq 1 ]]; then
    BACKUP_DIR="${RAW_DIR}.bak.$(date +%Y%m%d%H%M%S)"
    mv "${RAW_DIR}" "${BACKUP_DIR}"
    echo "==> existing data/raw/ moved to ${BACKUP_DIR}"
  else
    echo "data/raw/ already exists -- re-run with --force to refresh it" >&2
    exit 1
  fi
fi

CLONE_DIR="$(mktemp -d)"
trap 'rm -rf "${CLONE_DIR}"' EXIT

echo "==> cloning ${REPO_URL}"
git clone --depth 1 --quiet "${REPO_URL}" "${CLONE_DIR}/repo"

COMMIT_SHA="$(git -C "${CLONE_DIR}/repo" rev-parse HEAD)"
COMMIT_INFO="$(git -C "${CLONE_DIR}/repo" log -1 --date=short --format='%h %ad %s')"

mkdir -p "${RAW_DIR}"
git -C "${CLONE_DIR}/repo" archive --format=tar HEAD | tar -x -C "${RAW_DIR}"

HTML_COUNT="$(find "${RAW_DIR}" -name '*.html' | wc -l | tr -d ' ')"

cat > "${RAW_DIR}/SOURCE.txt" <<EOF
repo        : ${REPO_WEB}
mirror of   : http://shakespeare.mit.edu/
commit      : ${COMMIT_SHA}
commit info : ${COMMIT_INFO}
fetched on  : $(date +%F)
html files  : ${HTML_COUNT}
EOF

echo "==> commit: ${COMMIT_INFO}"
echo "==> done: ${HTML_COUNT} html files in ${RAW_DIR}"
