#!/bin/bash

SESSION="NovaGround"
SESSIONEXISTS=$(tmux list-sessions | grep $SESSION)

# Only create tmux session if it doesn't already exist
#if [ "$SESSIONEXISTS" = "" ]
# Optional FAS RS-422 serial port.  Set FAS_PORT in the environment or the
# systemd unit (Environment="FAS_PORT=/dev/ttyUSB0") to enable the direct
# FMC bridge link.  Leave unset to run without FAS hardware.
#FAS_ARGS=""
#if [ -n "${FAS_PORT}" ]; then
#    FAS_ARGS="--fas-port ${FAS_PORT}"
#    if [ -n "${FAS_BAUD}" ]; then
#        FAS_ARGS="${FAS_ARGS} --fas-baud ${FAS_BAUD}"
#    fi
#fi

tmux has-session -t $SESSION
if [ $? != 0 ]
then
    # Start New Session
    tmux new-session -d -s $SESSION
    tmux rename-window -t 0 'Main'

    # Start programs
    tmux send-keys -t 'Main' 'cd ~/Desktop/novaGround' C-m
    tmux send-keys -t 'Main' "./build/novaGround  --broker 192.168.137.1 --backend 192.168.137.1:8000 --fas-port /dev/ttyUSB0" C-m
fi

# Attach Session, on the Main window
tmux attach-session -t $SESSION:0