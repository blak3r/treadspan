#!/bin/bash

# Check if log file is provided
if [ $# -ne 1 ]; then
    echo "Usage: $0 <logfile>"
    exit 1
fi

LOG_FILE="$1"

# Extract REQ patterns, count unique occurrences, and sort
awk -F 'REQ :' '/REQ/ {print $2}' "$LOG_FILE" | sort | uniq -c | sort -nr

awk -F 'RESP:' '/RESP/ {print $2}' "$LOG_FILE" | sort | uniq -c | sort -nr