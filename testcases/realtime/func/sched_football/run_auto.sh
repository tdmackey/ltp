#! /bin/bash

if [ ! $SCRIPTS_DIR ]; then
        # assume we're running standalone
        export SCRIPTS_DIR=../../scripts/
fi

source $SCRIPTS_DIR/setenv.sh

$SCRIPTS_DIR/run_c_files.sh "sched_football"

LOG_FILE="$LOG_DIR/$LOG_FORMAT-sched_football.log"
PYTHONPATH=../../  python parse-football.py $LOG_FILE 2>&1 | tee -a $LOG_FILE