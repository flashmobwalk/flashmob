#!/bin/bash

set -e

input_dir=$1
data_dir=$2

mkdir -p $data_dir

echo "check md5sum ..."
echo "a466d004003e74174fe6a9e7455f598c $input_dir/ydata-yaltavista-webmap-v1_0_links-1.txt.gz" | md5sum -c
echo "79cc8ea7e0f203382b06af6222115a8b $input_dir/ydata-yaltavista-webmap-v1_0_links-2.txt.gz" | md5sum -c

echo "unzip files ..."
gunzip -cdk $input_dir/ydata-yaltavista-webmap-v1_0_links-1.txt.gz > $data_dir/yahoo-temp-1.txt
gunzip -cdk $input_dir/ydata-yaltavista-webmap-v1_0_links-2.txt.gz > $data_dir/yahoo-temp-2.txt

echo "change graph format ..."
./bin/format_yahoo -i $data_dir/yahoo-temp-1.txt -i $data_dir/yahoo-temp-2.txt -t $data_dir/yahoo.txt

rm $data_dir/yahoo-temp-1.txt
rm $data_dir/yahoo-temp-2.txt

echo "dataset preparing completed"
