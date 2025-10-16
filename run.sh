#!/bin/bash

# Run VN.cpp 5 times in different terminals and store the output in different files
for i in {1..5}
do
  #compile the code
  g++ VN.cpp -o VN
  gnome-terminal -- bash -c "./VN  "10.51.19.186" > output_$i.txt"
done