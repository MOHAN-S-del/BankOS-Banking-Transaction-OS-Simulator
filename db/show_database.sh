#!/usr/bin/env bash
# ==============================================================================
# BankOS: Interactive Database & SQL JOIN Inspector for Faculty Presentation
# ==============================================================================

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
python3 "$DIR/show_database.py" "$@"
