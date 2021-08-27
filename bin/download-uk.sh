#!/bin/bash

set -e

data_dir=$1
data_dir_rp=`realpath ${data_dir}`

mkdir -p $data_dir

wget http://data.law.di.unimi.it/webdata/uk-union-2006-06-2007-05/uk-union-2006-06-2007-05-underlying.graph -O $data_dir/uk-union.graph
echo "check md5sum ..."
echo "1059d9703845ae730063933a0b1efde7 $data_dir/uk-union.graph" | md5sum -c

wget http://data.law.di.unimi.it/webdata/uk-union-2006-06-2007-05/uk-union-2006-06-2007-05-underlying.properties -O $data_dir/uk-union.properties
echo "check md5sum ..."
echo "4dd217d18ab33b5766dc4be2e93f4c0b $data_dir/uk-union.properties" | md5sum -c

git clone https://github.com/Mogami95/graph-xll.git
cd graph-xll
java -cp "lib/*:bin" BV2Ascii $data_dir_rp/uk-union > $data_dir_rp/uk-union.txt
cd ..
echo "check md5sum ..."
echo "275f08a9e6864e70d9470945df1f2444 $data_dir/uk-union.txt" | md5sum -c

echo "dataset downloading completed"
