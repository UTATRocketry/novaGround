#!/bin/bash

SESSION="NovaGround"
SESSIONEXISTS=$(tmux list-sessions | grep $SESSION)

# Only create tmux session if it doesn't already exist
#if [ "$SESSIONEXISTS" = "" ]
tmux has-session -t $SESSION
if [ $? != 0 ]
then
    # Start New Session
    tmux new-session -d -s $SESSION
    tmux rename-window -t 0 'Main'

    # Start programs
    tmux send-keys -t 'Main' 'cd ~/Desktop/novaGround' C-m
    tmux send-keys -t 'Main' './build/novaGround' C-m
fi

# Attach Session, on the Main window
tmux attach-session -t $SESSION:0