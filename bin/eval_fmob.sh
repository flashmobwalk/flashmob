#!/bin/bash

set -e

data_dir=$1
output_dir=$2
app=$3 # deepwalk | node2vec

if [ "$app" = "node2vec" ]; then
    app_flags="-p 2 -q 0.5"
    prog="node2vec"
else
    app_flags=""
    prog="deepwalk"
fi

datasets=("youtube" "twitter" "friendster" "uk-union" "yahoo")
num_epoch=10
walk_len=80

program="./bin/${prog}"

echo "================================================="
echo "eval_fmob.sh $1 $2 $3"
echo `date`
echo "data_dir: ${data_dir}"
echo "output_dir: ${output_dir}"
echo "app: ${app} ${app_flags}"
echo "length: ${walk_len}"
echo "epoch: ${num_epoch}"
echo "================================================="

mkdir -p ${output_dir}

for graph_i in "${!datasets[@]}" 
do
    graph=${datasets[$graph_i]}
    input_file=`realpath ${data_dir}/${graph}.txt`
    output_file_prefix=${output_dir}/${graph}

    if [ -f "$input_file" ]; then
        cmd="${program} -f text -g ${input_file} -e ${num_epoch} -l ${walk_len} ${app_flags} 2>${output_file_prefix}.out.txt"
        echo ${cmd}
        eval ${cmd}
    else
        echo "$input_file does not exist. Skip evaluation of $graph"
    fi
done

echo "================================================="
echo `date`
echo "================================================="
