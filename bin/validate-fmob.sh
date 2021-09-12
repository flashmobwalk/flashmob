#!/bin/bash

set -e

data_dir=$1
graph_opt=$2

if [ "$graph_opt" = "small" ]; then
    datasets=("youtube" "twitter" "friendster")
elif [ "$graph_opt" = "large" ]; then
    datasets=("uk-union" "yahoo")
elif [ "$graph_opt" = "all" ]; then
    datasets=("youtube" "twitter" "friendster" "uk-union" "yahoo")
fi

program="./bin/test_dataset"

echo "================================================="
echo "validate.sh $1"
echo `date`
echo "data_dir: ${data_dir}"
echo "graph_opt: ${graph_opt}"
echo "================================================="

for graph_i in "${!datasets[@]}" 
do
    graph=${datasets[$graph_i]}
    input_file=`realpath ${data_dir}/${graph}.txt`

    if [ -f "$input_file" ]; then
        cmd="${program} -f text -g ${input_file}"
        echo ${cmd}
        eval ${cmd}
    else
        echo "$input_file does not exist. Skip validation of $graph"
    fi
done

echo "================================================="
echo `date`
echo "================================================="
