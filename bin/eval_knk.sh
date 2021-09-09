#!/bin/bash

set -e

data_dir=$1
output_dir=$2
app=$3 # deepwalk | node2vec

if [ "$app" = "node2vec" ]; then
    app_flags="-p 2 -q 0.5"
else
    app_flags=""
fi

thread=`nproc`
length=80
iteration=10

datasets=("youtube" "twitter" "friendster" "uk-union" "yahoo")
vertex_set=(1138499 41652230 65608366 131814559 720242173)
rate_set=("1" "0.1" "0.1" "0.05" "0.01")
program="./KnightKing/build/bin/${app}"

echo "================================================="
echo "eval_knk.sh $1 $2 $3"
echo `date`
echo "data_dir: ${data_dir}"
echo "output_dir: ${output_dir}"
echo "app: ${app} ${app_flags}"
echo "thread: ${thread}"
echo "length: ${length}"
echo "iteration: ${iteration}"
echo "================================================="

temp_graph_path="./${data_dir}/knk.temp.data"
mkdir -p ${output_dir}

step_time_arr=()
for graph_i in "${!datasets[@]}" 
do
    echo "================================================="
    graph=${datasets[$graph_i]}
    vertex=${vertex_set[$graph_i]}
    walker=$((${vertex} * ${iteration}))
    original_input_file=`realpath ${data_dir}/${graph}.txt`

    if [ -f "$original_input_file" ]; then
        input_file=${temp_graph_path}
        cmd="./bin/format_knk -i ${original_input_file} -o ${input_file}"
        echo ${cmd}
        eval ${cmd}

        output_file_prefix=${output_dir}/${graph}.t${thread}
        cmd="${vtune_cmd} ${program} -v ${vertex} -g ${input_file} --make-undirected -w ${walker} -l ${length} -s unweighted -r ${rate_set[$graph_i]} ${app_flags} >${output_file_prefix}.out 2>${output_file_prefix}.errors"
        echo ${cmd}
        eval ${cmd}

        total_time=`tail -n 1 ${output_file_prefix}.out | grep -o '[0-9.]\+'`
        step_time=`python -c "print(\"%.3f\" % (${total_time} * 10**9 * ${thread} / (${walker} * ${length})))"`
        step_time_arr+=(${step_time})
        echo "time: $step_time ns"
    else
        step_time_arr+=("NaN")
        echo "$original_input_file does not exist. Skip evaluation of $graph"
    fi
done

rm ${temp_graph_path}

echo "================================================="
echo `date`
for graph_i in "${!datasets[@]}" 
do
    graph=${datasets[$graph_i]}
    step_time=${step_time_arr[$graph_i]}
    if [ "$step_time" = "NaN" ]; then
        echo "${graph} skipped"
    else
        echo -e "${graph}\t${step_time} ns"
    fi
done
echo "================================================="
