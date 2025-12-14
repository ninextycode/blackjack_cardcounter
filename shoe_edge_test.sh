#!/bin/bash

LOG_DIR="logs/shoe_edge_test"

# Create log directory if it doesn't exist
mkdir -p "$LOG_DIR"

while true; do
    # Find the next available log number
    counter=0
    while [ -f "$LOG_DIR/log_$(printf '%04d' $counter).txt" ]; do
        ((counter++))
    done

    LOG_FILE="$LOG_DIR/log_$(printf '%04d' $counter).txt"

    echo "Running blackjack_exec, saving output to $LOG_FILE"
    ./build/bin/blackjack_exec 2>&1 | tee "$LOG_FILE"
    echo "Output saved to $LOG_FILE"
    echo ""
done
