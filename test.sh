#!/bin/bash
send_to() {
    data="$1"
    dest="$2"

    (sleep $(($RANDOM%5)); echo "$data") | nc "$dest" 8888
}


for (( i = 0; i < 50; i++ )); do
    send_to "Data from $i" localhost &
done
