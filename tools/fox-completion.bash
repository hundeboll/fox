#!/bin/bash

fox_source=$(readlink -f $(dirname ${BASH_SOURCE:-$0})/..)

_fox_flag() {
    local cur defines targets
    cur="${COMP_WORDS[COMP_CWORD]}"

    defines=$(cat $fox_source/src/fox.cpp \
        | grep "^DEFINE")

    targets=$(echo "$defines" \
        | sed -ne 's/^DEFINE_[^(]*(\([^,]*\).*/--\1/p'; \
        echo "$defines" \
        | sed -ne 's/^DEFINE_bool(\([^,]*\).*/--no\1/p';)
    COMPREPLY=($(compgen -W "$targets" -- "$cur"))
    return 0
}

complete -F _fox_flag -f fox
