#!/bin/bash

# Check if log file is provided
if [ $# -ne 1 ]; then
    echo "Usage: $0 <logfile>"
    exit 1
fi

LOG_FILE="$1"
FILTER_PATTERN="REQ : 1 6 0 "

# Initialize variables
previous_entry=""
count=0

# Process the log file
grep "$FILTER_PATTERN" "$LOG_FILE" | while read -r line; do
    # Extract timestamp and the relevant part of the request
    timestamp=$(echo "$line" | awk '{print $1}')
    req_pattern=$(echo "$line" | awk -F "$FILTER_PATTERN" '{print $2}')

    # If this is the first entry or a new pattern is found, print the count and the line
    if [[ "$req_pattern" != "$previous_entry" ]]; then
        if [[ $count -ne 0 ]]; then
            echo "$count $previous_line"
        fi
        count=1
        previous_entry="$req_pattern"
        previous_line="$line"
    else
        ((count++))
    fi
done

# Print the last counted group
if [[ $count -ne 0 ]]; then
    echo "$count $previous_line"
fi
