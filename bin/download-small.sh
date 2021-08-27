#!/bin/bash

set -e

data_dir=$1

mkdir -p $data_dir

wget http://socialnetworks.mpi-sws.mpg.de/data/youtube-links.txt.gz -O $data_dir/youtube.txt.gz
echo "check md5sum ..."
echo "1e5410e18de2ffaa06fb1ebf935cb98b $data_dir/youtube.txt.gz" | md5sum -c
echo "unzip the file ..."
gunzip -c $data_dir/youtube.txt.gz > $data_dir/youtube.txt

wget https://snap.stanford.edu/data/twitter-2010.txt.gz -O $data_dir/twitter.txt.gz
echo "check md5sum ..."
echo "6b78fabf1ae6eacc14ac2e6e4092ef6b $data_dir/twitter.txt.gz" | md5sum -c
echo "unzip the file ..."
gunzip -c $data_dir/twitter.txt.gz > $data_dir/twitter.txt

wget https://snap.stanford.edu/data/bigdata/communities/com-friendster.ungraph.txt.gz -O $data_dir/friendster.txt.gz
echo "check md5sum ..."
echo "2445fbb75bf194dac43f2f6d2ddfb1c8 $data_dir/friendster.txt.gz" | md5sum -c
echo "unzip the file ..."
gunzip -c $data_dir/friendster.txt.gz > $data_dir/friendster.txt

echo "dataset downloading completed"